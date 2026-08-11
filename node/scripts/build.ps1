<#
.SYNOPSIS
    Builds the Endstone Node.js layer and stages it into a server directory.

.DESCRIPTION
    One command to get a working "Endstone with Node.js" on Windows:

      1. fetches a checksum-pinned libnode if one is not supplied
      2. builds the Node host   (endstone_node_host.dll -> libnode)
      3. builds the Endstone plugin (endstone_nodejs.dll -> endstone::endstone, clang-cl)
      4. stages both plus libnode into <server>/plugins

    Endstone itself is NOT rebuilt. The Node layer is a plugin, not a patch: it attaches to a stock
    Endstone through the public plugin API, so an official wheel works as-is. Install one with
    `pip install endstone` and run `endstone -s <server>`.

.PARAMETER ServerDir
    Server directory to stage into. Omit to build without staging.

.PARAMETER LibnodeRoot
    An existing libnode distribution. Omit to fetch a pinned prebuilt automatically.

.EXAMPLE
    .\node\scripts\build.ps1 -ServerDir .\bedrock_server
#>
[CmdletBinding()]
param(
    [string]$ServerDir,
    [string]$LibnodeRoot,
    [string]$BuildDir = "build",
    [ValidateSet("Release", "RelWithDebInfo", "Debug")]
    [string]$Configuration = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$nodeDir = Join-Path $repo "node"

function Step($message) { Write-Host "`n==> $message" -ForegroundColor Cyan }
function Fail($message) { Write-Host "ERROR: $message" -ForegroundColor Red; exit 1 }

# --- toolchain -------------------------------------------------------------------------------------
Step "Locating toolchain"
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Fail "vswhere.exe not found. Visual Studio with the C++ workload is required for the Windows SDK and MSVC STL."
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { Fail "No Visual Studio installation with the C++ toolset was found." }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { Fail "vcvars64.bat not found under $vsPath." }

foreach ($tool in @("cmake", "ninja", "clang-cl", "python")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Fail "$tool is not on PATH. Endstone needs CMake, Ninja, clang-cl (LLVM 18+) and Python."
    }
}
Write-Host "  visual studio : $vsPath"
Write-Host "  clang-cl      : $((Get-Command clang-cl).Source)"

# Runs a command inside the VS developer environment; vcvars is the only way to get the SDK and STL
# paths that clang-cl needs.
function Invoke-VsCommand([string]$command) {
    # vcvars writes harmless notices to stderr, which Windows PowerShell turns into a terminating
    # error under ErrorActionPreference=Stop. Judge success by the exit code instead.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        cmd.exe /c "`"$vcvars`" >nul && $command"
    }
    finally {
        $ErrorActionPreference = $previous
    }
    if ($LASTEXITCODE -ne 0) { Fail "command failed (exit $LASTEXITCODE): $command" }
}

if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $repo $BuildDir }
# FetchContent nests expected-lite deeply enough that a long build path blows Windows' 260-character
# limit, and ninja fails with a bare "Filename longer than 260 characters".
if ($BuildDir.Length -gt 90) {
    Fail ("build path is too long ({0} chars) for Windows' 260-character limit:`n  {1}`n" -f $BuildDir.Length, $BuildDir) `
        + "Pass a shorter -BuildDir, e.g. -BuildDir C:\esbuild."
}
if ($Clean -and (Test-Path $BuildDir)) {
    Step "Cleaning $BuildDir"
    Remove-Item $BuildDir -Recurse -Force
}

# --- libnode ---------------------------------------------------------------------------------------
if (-not $LibnodeRoot) {
    Step "Fetching libnode (pinned, checksum-verified)"
    $LibnodeRoot = Join-Path $BuildDir "libnode"
    & python (Join-Path $nodeDir "scripts\fetch_libnode.py") --dest $LibnodeRoot --cache (Join-Path $BuildDir "libnode-cache")
    if ($LASTEXITCODE -ne 0) { Fail "fetch_libnode.py failed." }
}
else {
    Step "Using supplied libnode"
}
$LibnodeRoot = (Resolve-Path $LibnodeRoot).Path
if (-not (Test-Path (Join-Path $LibnodeRoot "include\node\node.h"))) {
    Fail "$LibnodeRoot does not look like a libnode distribution (no include/node/node.h)."
}
Write-Host "  libnode: $LibnodeRoot"

# --- host ------------------------------------------------------------------------------------------
# Built with the toolchain libnode itself uses. On Windows that is the MSVC ABI either way, so the
# default compiler is fine; on Linux this half must use gcc/libstdc++ (see node/README.md).
Step "Building the Node host"
$hostBuild = Join-Path $BuildDir "node-host"
Invoke-VsCommand "cmake -S `"$nodeDir`" -B `"$hostBuild`" -G Ninja -DCMAKE_BUILD_TYPE=$Configuration -DENDSTONE_NODE_BUILD_PLUGIN=OFF -DENDSTONE_NODE_LIBNODE_ROOT=`"$LibnodeRoot`""
Invoke-VsCommand "cmake --build `"$hostBuild`""

# --- plugin ----------------------------------------------------------------------------------------
# clang-cl to match how Endstone itself is built.
Step "Building the Endstone plugin"
$pluginBuild = Join-Path $BuildDir "node-plugin"
Invoke-VsCommand "cmake -S `"$nodeDir`" -B `"$pluginBuild`" -G Ninja -DCMAKE_BUILD_TYPE=$Configuration -DENDSTONE_NODE_BUILD_HOST=OFF -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER_TYPE=LLD"
Invoke-VsCommand "cmake --build `"$pluginBuild`""

$pluginDll = Join-Path $pluginBuild "plugin\endstone_nodejs.dll"
$hostDll = Join-Path $hostBuild "host\endstone_node_host.dll"
$libnodeDll = Join-Path $hostBuild "host\libnode.dll"
foreach ($artifact in @($pluginDll, $hostDll, $libnodeDll)) {
    if (-not (Test-Path $artifact)) { Fail "expected build output missing: $artifact" }
}

Step "Build complete"
Write-Host "  $pluginDll"
Write-Host "  $hostDll"
Write-Host "  $libnodeDll"

# --- stage -----------------------------------------------------------------------------------------
if (-not $ServerDir) {
    Write-Host "`nNo -ServerDir given; nothing staged. Re-run with -ServerDir <path> to install." -ForegroundColor Yellow
    exit 0
}

Step "Staging into $ServerDir"
$plugins = Join-Path $ServerDir "plugins"
$nodeData = Join-Path $plugins "nodejs"
New-Item -ItemType Directory -Force -Path $nodeData | Out-Null

Copy-Item $pluginDll $plugins -Force
Copy-Item $hostDll, $libnodeDll $nodeData -Force

Write-Host "  plugins\endstone_nodejs.dll"
Write-Host "  plugins\nodejs\endstone_node_host.dll"
Write-Host "  plugins\nodejs\libnode.dll"

# The packet decoder reads this at runtime for each packet's field layout. Regenerate it with
# node\scripts\generate_protocol.py after a BDS version bump.
$protocol = Join-Path $PSScriptRoot "..\protocol\protocol.json"
if (Test-Path $protocol) {
    Copy-Item $protocol $nodeData -Force
    Write-Host "  plugins\nodejs\protocol.json"
}
Write-Host @"

Done. Start the server with:

    endstone -s "$ServerDir"

Drop JavaScript plugins into $plugins as either a folder with package.json or a single .js file.
If a plugin declares dependencies, run "npm install" inside its folder first.

For editor completions and type checking, install the types in your plugin folder:
    npm install --save-dev @endstone-js/server
"@
