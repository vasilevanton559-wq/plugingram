"""Export a clean GitHub-ready Plugingram source tree."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
WORKSPACE = TOOLS.parent
SRC = WORKSPACE / "plugingram"
DEST = Path(os.environ["PLUGINGRAM_EXPORT"]) if os.environ.get("PLUGINGRAM_EXPORT") else (
    Path.home() / "Desktop" / "Plugingram"
)

EXCLUDE_DIRS = {
    "out",
    ".vs",
    ".vscode",
    ".cache",
    ".idea",
    "__pycache__",
    "tdata",
    "tdumps",
    "DebugLogs",
    "cmake-build-debug",
    "ipch",
    ".git",
    ".ai",
    "node_modules",
}

EXCLUDE_FILES = {
    "state.json",
    ".env",
    ".env.local",
    "plugingram_api_credentials_local.py",
    "custom_api_id.h",
    "alpha_private.h",
}


def should_skip_dir(name: str) -> bool:
    return name in EXCLUDE_DIRS or name.endswith(".xcodeproj")


def copy_tree(src: Path, dst: Path) -> None:
    for root, dirs, files in os.walk(src):
        root_path = Path(root)
        rel = root_path.relative_to(src)
        dirs[:] = [d for d in dirs if not should_skip_dir(d)]
        # Skip nested build outputs named Release/Debug under Telegram only if huge? Keep sources.
        target_root = dst / rel
        target_root.mkdir(parents=True, exist_ok=True)
        for name in files:
            if name in EXCLUDE_FILES:
                continue
            if name.endswith((".user", ".pyc", ".pdb", ".VC.db")):
                continue
            if name.endswith(".log"):
                continue
            # Skip generated VS project noise if present
            if name.endswith((".vcxproj", ".vcxproj.filters", ".sln", ".suo")):
                continue
            src_file = root_path / name
            dst_file = target_root / name
            # Skip plugin store runtime index under plugins/store if desired — keep samples.
            if rel.parts[:2] == ("plugins",) and name == "state.json":
                continue
            shutil.copy2(src_file, dst_file)


def main() -> int:
    if not SRC.is_dir():
        print("Missing source:", SRC)
        return 1

    if DEST.exists():
        print("Removing previous export:", DEST)
        shutil.rmtree(DEST)
    DEST.mkdir(parents=True)

    print("Copying sources:", SRC, "->", DEST)
    copy_tree(SRC, DEST)

    # Portable tools into export/tools
    tools_dest = DEST / "tools"
    tools_dest.mkdir(parents=True, exist_ok=True)
    for name in [
        "plugingram_paths.py",
        "plugingram_build_exe.py",
        "plugingram_build_only.py",
        "plugingram_vcenv_runner.py",
        "plugingram_prepare_wrapper.py",
        "plugingram_write_qt_cache_key.py",
        "create_plugingram_plugin.py",
        "gen_phone_blur_icon.py",
        "gen_plugingram_icon.py",
        "plugingram_api_credentials_local.example.py",
        "plugingram_build_qt.bat",
        "plugingram_finish_qt_install.bat",
        "plugingram_resume_qt.bat",
        "rebuild_rnnoise.bat",
        "export_plugingram_github.py",
        "plugingram_package_release.py",
        "README.md",
    ]:
        src = TOOLS / name
        if src.exists():
            shutil.copy2(src, tools_dest / name)

    # Ensure plugins/noise stays, drop empty phone-blur leftovers
    legacy = DEST / "plugins" / "phone-blur"
    if legacy.exists():
        shutil.rmtree(legacy)
    state = DEST / "plugins" / "state.json"
    if state.exists():
        state.unlink()

    # Sanity: no absolute user path leftovers in exported helpers
    # (skip this script — it contains the detector strings themselves).
    bad = []
    skip_scan = {"export_plugingram_github.py"}
    for path in tools_dest.rglob("*"):
        if not path.is_file() or path.name in skip_scan:
            continue
        if path.suffix.lower() not in {".py", ".bat", ".cmd", ".ps1", ".md"}:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        lowered = text.lower()
        if "c:\\users\\" in lowered or "c:/users/" in lowered:
            bad.append(path)
            continue
        if "desktop\\tg" in lowered or "desktop/tg" in lowered:
            bad.append(path)

    # Marker for GitHub readiness
    (DEST / ".plugingram-export").write_text(
        "pre-beta export\n", encoding="utf-8"
    )

    print("EXPORT_OK:", DEST)
    if bad:
        print("WARN hardcoded paths still present:")
        for p in bad:
            print(" ", p)
        return 2

    # Quick checklist
    required = [
        DEST / "README.md",
        DEST / "SECURITY.md",
        DEST / "LEGAL",
        DEST / "LICENSE",
        DEST / "Telegram",
        DEST / "plugins" / "noise" / "plugingram.json",
        DEST / "tools" / "plugingram_paths.py",
    ]
    missing = [str(p) for p in required if not p.exists()]
    if missing:
        print("MISSING:", *missing, sep="\n ")
        return 3
    print("CHECKLIST_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
