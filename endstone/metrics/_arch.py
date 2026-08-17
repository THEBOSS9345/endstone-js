import os
import platform

_ELF_MACHINES = {
    0x03: "i386",
    0x28: "arm",
    0x3E: "amd64",
    0xB7: "aarch64",
    0xF3: "riscv64",
}


# Report what Java's os.arch would, so the bStats buckets line up.
_ARCH_ALIASES = {
    "x86_64": "amd64",
    "arm64": "aarch64",
}


def normalise_arch(arch: str) -> str:
    arch = arch.lower()
    return _ARCH_ALIASES.get(arch, arch)


def detect_host_arch() -> str | None:
    """Real host arch, seen through any emulation layer. None if it can't be determined."""
    if platform.system() == "Windows":
        # Only set when the process is emulated (e.g. x64 on an ARM64 host).
        arch = os.environ.get("PROCESSOR_ARCHITEW6432")
        return normalise_arch(arch) if arch else None

    # qemu-user fakes /proc/self/exe to point at the guest binary, but never touches /proc/thread-self.
    try:
        with open("/proc/thread-self/exe", "rb") as f:
            header = f.read(20)
        if header[:4] == b"\x7fELF":
            byteorder = "little" if header[5] == 1 else "big"
            machine = _ELF_MACHINES.get(int.from_bytes(header[18:20], byteorder))
            if machine is not None:
                return machine
    except OSError:
        pass

    # Rosetta fakes uname and /proc/cpuinfo, but leaves /sys alone.
    try:
        with open("/sys/firmware/devicetree/base/cpus/cpu@0/compatible", "rb") as f:
            if f.read().startswith(b"arm,arm-v8"):
                return "aarch64"
    except OSError:
        pass

    return None


def host_arch() -> str:
    """Real host arch, falling back to what the kernel reports."""
    return detect_host_arch() or normalise_arch(platform.machine())
