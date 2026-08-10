# The BDS server folder lives on the host, not in a Docker volume, so it can be edited directly:
# drop in plugins, edit server.properties, op yourself in permissions.json, keep the world across
# `docker rm`. The container only runs it.
$ServerDir = "C:\Projects\MCPE\endstone-server"

# Prebuilt libnode for the Linux container: headers/include plus linux/libnode.so*, which is all
# linux-serve.sh reads. Refetch with: python node\scripts\fetch_libnode.py --platform linux
$LibnodeDir = "C:\Projects\MCPE\endstone-libnode"

$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

if (-not (Test-Path (Join-Path $ServerDir "bedrock_server"))) {
    Write-Host "No bedrock_server in $ServerDir." -ForegroundColor Red
    Write-Host "Endstone downloads BDS on first run, so an empty folder is fine - but if you meant to"
    Write-Host "reuse an existing server, point `$ServerDir at it."
    if (-not (Test-Path $ServerDir)) { New-Item -ItemType Directory -Force -Path $ServerDir | Out-Null }
}

# Build
cmd.exe /c "`"$vcvars`" >nul && cmake --build C:\esbuild\node-plugin"

# Remove previous container
docker rm -f endstone-node-test 2>$null

# Start server
docker run -d -i `
    --name endstone-node-test `
    -p 19132:19132/udp `
    -p 19133:19133/udp `
    -v "C:\Projects\MCPE\endstone-js:/src:ro" `
    -v "${LibnodeDir}:/libnode:ro" `
    -v "${ServerDir}:/server" `
    endstone-node-spike:bds `
    bash /src/node/scripts/linux-serve.sh

Write-Host ""
Write-Host "Server started!"
Write-Host "Connect to: localhost:19132"
Write-Host ""
Write-Host "Server folder: $ServerDir  (edit it directly - plugins, properties, world)"
Write-Host "Container: endstone-node-test"
Write-Host "Use 'docker logs -f endstone-node-test' to view logs."
