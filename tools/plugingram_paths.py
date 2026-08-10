"""Portable path resolution for Plugingram build helpers.

Supports two layouts:
  1) Dev workspace:  <root>/tools  +  <root>/plugingram/Telegram  +  <root>/Libraries
  2) Repo export:    <repo>/tools  +  <repo>/Telegram
     Libraries/ThirdParty are looked up via PLUGINGRAM_WORKSPACE or sibling folders.
"""
from __future__ import annotations

import os
import pathlib
import subprocess
import sys
from typing import Iterable


TOOLS_DIR = pathlib.Path(__file__).resolve().parent


def _first_existing(candidates: Iterable[pathlib.Path]) -> pathlib.Path | None:
    for path in candidates:
        if path and path.exists():
            return path
    return None


def detect_layout() -> dict[str, pathlib.Path]:
    env_root = os.environ.get("PLUGINGRAM_ROOT") or os.environ.get("PLUGINGRAM_WORKSPACE")
    if env_root:
        root = pathlib.Path(env_root).resolve()
        if (root / "plugingram" / "Telegram").is_dir():
            return {
                "workspace": root,
                "repo": root / "plugingram",
                "telegram": root / "plugingram" / "Telegram",
                "out": root / "plugingram" / "out",
                "tools": TOOLS_DIR,
            }
        if (root / "Telegram").is_dir():
            return {
                "workspace": root,
                "repo": root,
                "telegram": root / "Telegram",
                "out": root / "out",
                "tools": TOOLS_DIR,
            }

    # Repo export: tools/ sits next to Telegram/
    repo = TOOLS_DIR.parent
    if (repo / "Telegram").is_dir():
        workspace = repo
        parent = repo.parent
        if (parent / "Libraries").is_dir() or (parent / "ThirdParty").is_dir():
            workspace = parent
        return {
            "workspace": workspace,
            "repo": repo,
            "telegram": repo / "Telegram",
            "out": repo / "out",
            "tools": TOOLS_DIR,
        }

    # Dev workspace: tools/ sits next to plugingram/
    workspace = TOOLS_DIR.parent
    if (workspace / "plugingram" / "Telegram").is_dir():
        return {
            "workspace": workspace,
            "repo": workspace / "plugingram",
            "telegram": workspace / "plugingram" / "Telegram",
            "out": workspace / "plugingram" / "out",
            "tools": TOOLS_DIR,
        }

    raise RuntimeError(
        "Cannot locate Plugingram sources. Expected either:\n"
        f"  {TOOLS_DIR.parent / 'Telegram'}\n"
        f"  {TOOLS_DIR.parent / 'plugingram' / 'Telegram'}\n"
        "Or set PLUGINGRAM_ROOT / PLUGINGRAM_WORKSPACE."
    )


_LAYOUT = detect_layout()
WORKSPACE: pathlib.Path = _LAYOUT["workspace"]
REPO_DIR: pathlib.Path = _LAYOUT["repo"]
TELEGRAM_DIR: pathlib.Path = _LAYOUT["telegram"]
OUT_DIR: pathlib.Path = _LAYOUT["out"]
CONFIGURE_BAT: pathlib.Path = TELEGRAM_DIR / "configure.bat"


def libraries_dir() -> pathlib.Path:
    env = os.environ.get("PLUGINGRAM_LIBRARIES")
    if env:
        return pathlib.Path(env).resolve()
    found = _first_existing(
        [
            WORKSPACE / "Libraries" / "win64",
            REPO_DIR / "Libraries" / "win64",
            WORKSPACE / "Libraries",
            # Common sibling next to a Desktop export folder.
            REPO_DIR.parent / "TG" / "Libraries" / "win64",
            REPO_DIR.parent / "Libraries" / "win64",
        ]
    )
    if found:
        return found
    return WORKSPACE / "Libraries" / "win64"


def third_party_dir() -> pathlib.Path:
    env = os.environ.get("PLUGINGRAM_THIRDPARTY")
    if env:
        return pathlib.Path(env).resolve()
    found = _first_existing(
        [
            WORKSPACE / "ThirdParty",
            REPO_DIR / "ThirdParty",
        ]
    )
    return found or (WORKSPACE / "ThirdParty")


def pkg_config_path() -> str:
    return str(libraries_dir() / "local" / "lib" / "pkgconfig")


def find_vswhere() -> pathlib.Path | None:
    pf86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    candidate = pathlib.Path(pf86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    return candidate if candidate.is_file() else None


def find_vcvars64() -> pathlib.Path:
    env = os.environ.get("PLUGINGRAM_VCVARS")
    if env and pathlib.Path(env).is_file():
        return pathlib.Path(env)

    vswhere = find_vswhere()
    if vswhere:
        result = subprocess.run(
            [
                str(vswhere),
                "-latest",
                "-products",
                "*",
                "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-find",
                r"VC\Auxiliary\Build\vcvars64.bat",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        lines = [ln.strip() for ln in result.stdout.splitlines() if ln.strip()]
        if lines and pathlib.Path(lines[0]).is_file():
            return pathlib.Path(lines[0])

    pf86 = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    for edition in ("BuildTools", "Community", "Professional", "Enterprise"):
        candidate = (
            pf86
            / "Microsoft Visual Studio"
            / "2022"
            / edition
            / "VC"
            / "Auxiliary"
            / "Build"
            / "vcvars64.bat"
        )
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "vcvars64.bat not found. Install VS 2022 Build Tools "
        "or set PLUGINGRAM_VCVARS."
    )


def find_windows_kits_bin_x64() -> pathlib.Path | None:
    pf86 = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    root = pf86 / "Windows Kits" / "10" / "bin"
    if not root.is_dir():
        return None
    versions = sorted(
        [p for p in root.iterdir() if p.is_dir() and (p / "x64").is_dir()],
        reverse=True,
    )
    return (versions[0] / "x64") if versions else None


def find_vs_cmake_bin() -> pathlib.Path | None:
    vcvars = find_vcvars64()
    # .../VC/Auxiliary/Build/vcvars64.bat -> edition root
    edition_root = vcvars.parents[2]
    candidate = (
        edition_root
        / "Common7"
        / "IDE"
        / "CommonExtensions"
        / "Microsoft"
        / "CMake"
        / "CMake"
        / "bin"
    )
    return candidate if candidate.is_dir() else None


def find_vs_ninja_dir() -> pathlib.Path | None:
    vcvars = find_vcvars64()
    edition_root = vcvars.parents[2]
    candidate = (
        edition_root
        / "Common7"
        / "IDE"
        / "CommonExtensions"
        / "Microsoft"
        / "CMake"
        / "Ninja"
    )
    return candidate if candidate.is_dir() else None


def find_msbuild_amd64() -> pathlib.Path | None:
    vcvars = find_vcvars64()
    edition_root = vcvars.parents[2]
    candidate = edition_root / "MSBuild" / "Current" / "Bin" / "amd64"
    return candidate if candidate.is_dir() else None


def existing_path_strs(paths: Iterable[pathlib.Path | str | None]) -> list[str]:
    result: list[str] = []
    for item in paths:
        if not item:
            continue
        path = pathlib.Path(item)
        if path.exists():
            result.append(str(path))
    return result


def build_path_prefixes(*, include_python: bool = True) -> list[str]:
    tools = TOOLS_DIR
    third = third_party_dir()
    system32 = pathlib.Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
    prefixes = existing_path_strs(
        [
            system32,
            pathlib.Path(sys.executable).parent if include_python else None,
            third / "python" / "Scripts",
            third / "msys64" / "usr" / "bin",
            third / "jom",
            tools / "strawberry-perl" / "perl" / "bin",
            tools / "nasm" / "nasm-3.01",
            find_windows_kits_bin_x64(),
            find_msbuild_amd64(),
            tools / "mingit" / "cmd",
            tools / "mingit" / "mingw64" / "bin",
            find_vs_cmake_bin(),
            find_vs_ninja_dir(),
        ]
    )
    return prefixes


def strawberry_c_bin() -> str | None:
    path = TOOLS_DIR / "strawberry-perl" / "c" / "bin"
    return str(path) if path.is_dir() else None


def import_vcvars_env(vcvars_ver: str = "14.44") -> pathlib.Path:
    vcvars = find_vcvars64()
    comspec = str(pathlib.Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "cmd.exe")
    result = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-Command",
            (
                f"& '{comspec}' /d /s /c "
                f"'call \"{vcvars}\" -vcvars_ver={vcvars_ver} >nul && set'"
            ),
        ],
        capture_output=True,
        text=True,
        check=True,
    )
    for line in result.stdout.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ[key] = value
    return vcvars


def apply_common_build_env(*, keep_strawberry_c_last: bool = False) -> None:
    import_vcvars_env()
    os.environ["Platform"] = "x64"
    system32 = pathlib.Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
    os.environ["COMSPEC"] = str(system32 / "cmd.exe")

    filtered = [
        p
        for p in os.environ.get("Path", "").split(";")
        if p and ("strawberry-perl\\c\\bin" not in p.lower())
    ]
    prefixes = build_path_prefixes()
    path_parts = prefixes + filtered
    if keep_strawberry_c_last:
        c_bin = strawberry_c_bin()
        msys = third_party_dir() / "msys64" / "usr" / "bin"
        extras = [p for p in (c_bin, str(msys) if msys.is_dir() else None) if p]
        path_parts.extend(extras)
    os.environ["Path"] = ";".join(path_parts)
    os.environ["PKG_CONFIG_PATH"] = pkg_config_path()
