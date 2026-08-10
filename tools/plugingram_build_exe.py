import importlib.util
import os
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from plugingram_paths import (  # noqa: E402
    CONFIGURE_BAT,
    OUT_DIR,
    TELEGRAM_DIR,
    apply_common_build_env,
)

TOOLS = pathlib.Path(__file__).resolve().parent


def load_api_credentials() -> tuple[str, str]:
    env_id = os.environ.get("TDESKTOP_API_ID") or os.environ.get("PLUGINGRAM_API_ID")
    env_hash = os.environ.get("TDESKTOP_API_HASH") or os.environ.get("PLUGINGRAM_API_HASH")
    if env_id and env_hash:
        return env_id.strip(), env_hash.strip()

    local = TOOLS / "plugingram_api_credentials_local.py"
    if local.is_file():
        spec = importlib.util.spec_from_file_location(
            "plugingram_api_credentials_local", local
        )
        mod = importlib.util.module_from_spec(spec)
        assert spec and spec.loader
        spec.loader.exec_module(mod)
        api_id = str(getattr(mod, "API_ID", "")).strip()
        api_hash = str(getattr(mod, "API_HASH", "")).strip()
        if api_id and api_hash and "YOUR_API" not in api_id and "YOUR_API" not in api_hash:
            return api_id, api_hash

    raise SystemExit(
        "Missing API credentials. Create tools/plugingram_api_credentials_local.py "
        "(see plugingram_api_credentials_local.example.py) or set "
        "PLUGINGRAM_API_ID / PLUGINGRAM_API_HASH."
    )


def main() -> int:
    apply_common_build_env()
    os.environ["CC"] = "cl"
    os.environ["CXX"] = "cl"
    os.environ["PYTHONUNBUFFERED"] = "1"

    api_id, api_hash = load_api_credentials()
    print(f"API_ID={api_id}")

    if not CONFIGURE_BAT.exists():
        print("Missing", CONFIGURE_BAT)
        return 1

    configure = [
        str(CONFIGURE_BAT),
        "x64",
        "-D",
        f"TDESKTOP_API_ID={api_id}",
        "-D",
        f"TDESKTOP_API_HASH={api_hash}",
        "-D",
        "TDESKTOP_API_TEST=OFF",
        "-D",
        "DESKTOP_APP_DISABLE_CRASH_REPORTS=ON",
        "-D",
        "DESKTOP_APP_DISABLE_AUTOUPDATE=ON",
        "-D",
        "CMAKE_CONFIGURATION_TYPES=Release",
    ]
    print("CONFIGURE:", " ".join(configure))
    result = subprocess.run(configure, cwd=TELEGRAM_DIR)
    if result.returncode != 0:
        return result.returncode

    # High -j + MSVC /MP often hits C1002/C1060 (compiler heap) on Telegram sources.
    jobs = min(2, os.cpu_count() or 2)
    os.environ["CL"] = (os.environ.get("CL", "") + " /Zm200").strip()
    build = [
        "cmake",
        "--build",
        str(OUT_DIR),
        "--config",
        "Release",
        "--target",
        "Telegram",
        "-j",
        str(jobs),
    ]
    print("BUILD:", " ".join(build))
    result = subprocess.run(build, cwd=TELEGRAM_DIR)
    if result.returncode != 0:
        print("RETRY_BUILD_J1")
        build[-1] = "1"
        result = subprocess.run(build, cwd=TELEGRAM_DIR)
        if result.returncode != 0:
            return result.returncode

    exe = OUT_DIR / "Release" / "Plugingram.exe"
    alt = OUT_DIR / "Release" / "Telegram.exe"
    if exe.exists():
        print("READY:", exe)
    elif alt.exists():
        print("READY:", alt)
    else:
        print("Build finished but executable not found in", OUT_DIR / "Release")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
