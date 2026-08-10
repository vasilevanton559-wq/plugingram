"""Pack a clean Plugingram release and install into AppData like Telegram Desktop."""
from __future__ import annotations

import datetime as dt
import os
import shutil
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from plugingram_paths import OUT_DIR, REPO_DIR, WORKSPACE  # noqa: E402

RELEASE = OUT_DIR / "Release"
EXE = RELEASE / "Plugingram.exe"
DEST_ROOT = Path(
    os.environ.get(
        "PLUGINGRAM_PACKAGE_DIR",
        str(WORKSPACE / "dist"),
    )
)
APP_FOLDER_NAME = "Plugingram Desktop"


def appdata_install_dir() -> Path:
    appdata = os.environ.get("APPDATA")
    if not appdata:
        raise RuntimeError("APPDATA is not set")
    return Path(appdata) / APP_FOLDER_NAME


def create_desktop_shortcut(target_exe: Path, workdir: Path) -> Path:
    desktop = Path.home() / "Desktop"
    shortcut = desktop / f"{APP_FOLDER_NAME}.lnk"
    # PowerShell COM shortcut
    ps = f"""
$W = New-Object -ComObject WScript.Shell
$S = $W.CreateShortcut('{str(shortcut).replace("'", "''")}')
$S.TargetPath = '{str(target_exe).replace("'", "''")}'
$S.WorkingDirectory = '{str(workdir).replace("'", "''")}'
$S.Description = '{APP_FOLDER_NAME}'
$S.IconLocation = '{str(target_exe).replace("'", "''")},0'
$S.Save()
"""
    import subprocess

    subprocess.run(
        ["powershell", "-NoProfile", "-Command", ps],
        check=False,
    )
    return shortcut


def main() -> int:
    if not EXE.is_file():
        print("Missing:", EXE)
        return 1

    DEST_ROOT.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d")
    zip_path = DEST_ROOT / f"Plugingram-pre-beta-{stamp}-win64.zip"
    portable_dir = DEST_ROOT / f"Plugingram-pre-beta-{stamp}-win64"
    if zip_path.exists():
        zip_path.unlink()
    if portable_dir.exists():
        shutil.rmtree(portable_dir)
    portable_dir.mkdir(parents=True)

    # Clean zip contents (no user tdata/logs).
    shutil.copy2(EXE, portable_dir / "Plugingram.exe")
    readme = REPO_DIR / "README.md"
    if readme.is_file():
        shutil.copy2(readme, portable_dir / "README.md")
    (portable_dir / "INSTALL.txt").write_text(
        "Plugingram Desktop\n"
        "\n"
        "1. Run Plugingram.exe once.\n"
        "2. It installs into %AppData%\\Plugingram Desktop\\\n"
        "3. A Desktop shortcut is created automatically.\n"
        "4. Profile data (tdata, plugins, logs) stays only in AppData.\n",
        encoding="utf-8",
    )

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in portable_dir.rglob("*"):
            if path.is_file():
                zf.write(path, Path("Plugingram") / path.relative_to(portable_dir))

    # Install like Telegram Desktop: exe + runtime dirs under AppData.
    install_dir = appdata_install_dir()
    install_dir.mkdir(parents=True, exist_ok=True)
    (install_dir / "plugins").mkdir(exist_ok=True)
    (install_dir / "tdata").mkdir(exist_ok=True)
    installed_exe = install_dir / "Plugingram.exe"
    shutil.copy2(EXE, installed_exe)

    noise_src = REPO_DIR / "plugins" / "noise"
    if noise_src.is_dir():
        noise_dst = install_dir / "plugins" / "noise"
        if noise_dst.exists():
            shutil.rmtree(noise_dst)
        shutil.copytree(noise_src, noise_dst)

    shortcut = create_desktop_shortcut(installed_exe, install_dir)

    # Convenience copies
    desktop = Path.home() / "Desktop"
    desktop_zip = desktop / zip_path.name
    shutil.copy2(zip_path, desktop_zip)

    print("PACKAGE_OK:", zip_path)
    print("INSTALL_DIR:", install_dir)
    print("INSTALLED_EXE:", installed_exe)
    print("DESKTOP_SHORTCUT:", shortcut)
    print("DESKTOP_ZIP:", desktop_zip)
    print("SIZE_MB:", round(zip_path.stat().st_size / (1024 * 1024), 1))
    print("NOTE: Do not keep a loose Plugingram.exe on Desktop; use the shortcut.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
