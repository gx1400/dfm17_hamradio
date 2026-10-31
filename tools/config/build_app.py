#!/usr/bin/env python3
"""build_app.py: package dfmgui.py as a one-file app for this platform.

    pip install -r tools/requirements-app.txt
    python3 tools/config/build_app.py [--version v0.4.0]

Writes tools/config/_version.py with the firmware version (default: git
describe, as the Makefile stamps the firmware), so the app compares the
board's firmware with the release it came from, then runs PyInstaller.
The app lands in dist/dfm17-config-<version>-<os>-<arch>[.exe] (on macOS an
.app) and checks itself with --self-test OUT.json. Run by .github/workflows/app.yml for releases.
"""

import argparse
import os
import platform
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dfmcfg


def platform_tag():
    system = {"Darwin": "macos", "Windows": "windows"}.get(platform.system(), "linux")
    machine = platform.machine().lower()
    arch = {"amd64": "x64", "x86_64": "x64", "aarch64": "arm64"}.get(machine, machine)
    return f"{system}-{arch}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--version", help="the firmware version to stamp (default: git describe)")
    ap.add_argument("--dist", default="dist", help="output directory")
    a = ap.parse_args()

    version_file = os.path.join(HERE, "_version.py")
    if os.path.exists(version_file):
        os.remove(version_file)  # tool_version() must see git, not an old stamp
    version = a.version or dfmcfg.tool_version()
    if version == "unknown":
        sys.exit("can't tell the version: pass --version, or build in a git checkout")
    with open(version_file, "w") as f:
        f.write(f'"""written by build_app.py"""\n\nFW_VERSION = "{version}"\n')

    name = f"dfm17-config-{version}-{platform_tag()}"
    try:
        subprocess.run(
            [
                sys.executable,
                "-m",
                "PyInstaller",
                # macOS: an .app folder (one-file .app bundles are deprecated),
                # zipped by app.yml; elsewhere one executable
                "--onedir" if platform.system() == "Darwin" else "--onefile",
                "--windowed",
                "--noconfirm",
                "--clean",
                "--name",
                name,
                "--distpath",
                a.dist,
                "--workpath",
                os.path.join("build", "app"),
                "--specpath",
                os.path.join("build", "app"),
                "--paths",
                HERE,
                "--hidden-import",
                "_version",
                "--hidden-import",
                "fakeboard",
                # pyOCD finds its probes and targets through entry points and
                # data files, which PyInstaller doesn't see on its own
                "--collect-all",
                "pyocd",
                "--copy-metadata",
                "pyocd",
                "--collect-all",
                "libusb_package",
                "--collect-all",
                "cmsis_pack_manager",
                os.path.join(HERE, "dfmgui.py"),
            ],
            check=True,
        )
    finally:
        os.remove(version_file)  # a checkout keeps using git describe
    print(f"built {a.dist}/{name}")


if __name__ == "__main__":
    main()
