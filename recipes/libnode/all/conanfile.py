import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy, get, load, save
from conan.tools.layout import basic_layout


class LibnodeConan(ConanFile):
    name = "libnode"
    description = "Node.js built as a shared library for embedding"
    license = "MIT"
    url = "https://github.com/EndstoneMC/endstone"
    homepage = "https://nodejs.org"
    topics = ("node", "nodejs", "javascript", "v8", "embedding")
    package_type = "shared-library"

    settings = "os", "arch", "compiler", "build_type"
    options = {"fPIC": [True, False]}
    default_options = {"fPIC": True}

    @property
    def _dest_cpu(self):
        return {"x86_64": "x64", "armv8": "arm64", "arm64": "arm64"}[str(self.settings.arch)]

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        # Node's build system picks its own C++ standard (-std=gnu++20) and its own standard library
        # (the compiler default, i.e. libstdc++ on Linux - there is no configure switch for it, see
        # nodejs/node#57817, closed "not planned"). Encoding cppstd/libcxx in the package id would
        # therefore be a lie: the same binary is produced regardless of what the consuming profile
        # asks for.
        #
        # IMPORTANT: on Linux this package is ALWAYS libstdc++, even when consumed from a libc++
        # profile such as Endstone's. Node's embedder API takes std::vector<std::string> and returns
        # std::shared_ptr, so it must only ever be called from a libstdc++ translation unit. See
        # node/README.md for the C ABI firewall that keeps Endstone's libc++ code away from it.
        self.settings.rm_safe("compiler.cppstd")
        self.settings.rm_safe("compiler.libcxx")

    def layout(self):
        basic_layout(self, src_folder="src")

    def validate(self):
        if str(self.settings.arch) not in ("x86_64", "armv8", "arm64"):
            raise ConanInvalidConfiguration(f"{self.ref} only supports x86_64 and arm64.")
        if self.settings.os not in ("Windows", "Linux"):
            raise ConanInvalidConfiguration(f"{self.ref} only supports Windows and Linux.")

    def build_requirements(self):
        if self.settings.os == "Windows":
            self.tool_requires("nasm/2.16.01")

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

        if self.settings.os == "Windows":
            # The Windows DLL build compiles node_mksnapshot expecting dllimport symbols while it is
            # statically linked against node_base, so linking fails. Forcing the static view for this
            # one tool is the fix used by metacall/libnode.
            mksnapshot = os.path.join(self.source_folder, "tools", "snapshot", "node_mksnapshot.cc")
            content = load(self, mksnapshot)
            if "#undef NODE_SHARED_MODE" not in content:
                save(self, mksnapshot, "#undef NODE_SHARED_MODE\n" + content)

    def build(self):
        jobs = os.cpu_count() or 4
        if self.settings.os == "Windows":
            # vcbuild is the only supported path on Windows; "dll" is the --shared equivalent and
            # selects the dynamic CRT (/MD), matching Endstone's compiler.runtime=dynamic.
            build_type = "debug" if self.settings.build_type == "Debug" else "release"
            self.run(f"vcbuild.bat {self._dest_cpu} dll {build_type}", cwd=self.source_folder)
        else:
            configure = [
                "./configure",
                "--shared",
                f"--dest-cpu {self._dest_cpu}",
                "--dest-os linux",
            ]
            if self.settings.build_type == "Debug":
                configure.append("--debug")
            self.run(" ".join(configure), cwd=self.source_folder)
            self.run(f"make -j{jobs}", cwd=self.source_folder)

    def package(self):
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))

        # Headers are laid out as include/node/** to match the official headers tarball, which is
        # what node/CMakeLists.txt and every addon toolchain expect.
        include = os.path.join(self.package_folder, "include", "node")
        copy(self, "*.h", src=os.path.join(self.source_folder, "src"), dst=include)
        for dep in ("v8", "uv"):
            copy(
                self,
                "*.h",
                src=os.path.join(self.source_folder, "deps", dep, "include"),
                dst=include,
                keep_path=True,
            )

        out = os.path.join(self.source_folder, "out", str(self.settings.build_type))
        if self.settings.os == "Windows":
            copy(self, "libnode.dll", src=out, dst=os.path.join(self.package_folder, "bin"), keep_path=False)
            copy(self, "libnode.lib", src=out, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        else:
            lib = os.path.join(self.package_folder, "lib")
            copy(self, "libnode.so*", src=out, dst=lib, keep_path=False)
            # Node only emits the SONAME-versioned file; consumers link -lnode, which needs the
            # unversioned name to exist alongside it.
            versioned = [f for f in os.listdir(lib) if f.startswith("libnode.so.")]
            if versioned:
                link = os.path.join(lib, "libnode.so")
                if not os.path.exists(link):
                    os.symlink(versioned[0], link)

    def package_info(self):
        self.cpp_info.includedirs = [os.path.join("include", "node")]
        self.cpp_info.libs = ["libnode"] if self.settings.os == "Windows" else ["node"]
        self.cpp_info.set_property("cmake_file_name", "libnode")
        self.cpp_info.set_property("cmake_target_name", "libnode::libnode")
        if self.settings.os == "Windows":
            # node.h only resolves NODE_EXTERN to dllimport for consumers that declare themselves an
            # extension; without this every symbol is dllexport and linking fails.
            self.cpp_info.defines = ["BUILDING_NODE_EXTENSION"]
