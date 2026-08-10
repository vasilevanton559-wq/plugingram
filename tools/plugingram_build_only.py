import os
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from plugingram_paths import (  # noqa: E402
    OUT_DIR,
    TELEGRAM_DIR,
    apply_common_build_env,
)


def main() -> int:
    apply_common_build_env()
    # /MP1 avoids MSVC PCH heap OOM (C1060/C3859) on large Telegram targets.
    os.environ["CL"] = "/MP1 /Zm200"
    build = [
        "cmake",
        "--build",
        str(OUT_DIR),
        "--config",
        "Release",
        "--target",
        "Telegram",
        "-j",
        "1",
    ]
    print("BUILD:", " ".join(build))
    code = subprocess.run(build, cwd=TELEGRAM_DIR).returncode
    exe = OUT_DIR / "Release" / "Plugingram.exe"
    if code == 0 and exe.exists():
        print("READY:", exe)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
