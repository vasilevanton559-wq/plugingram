import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from plugingram_paths import apply_common_build_env  # noqa: E402


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: plugingram_vcenv_runner.py <cwd> <command> [args...]")
        return 2

    apply_common_build_env(keep_strawberry_c_last=True)

    cwd = pathlib.Path(sys.argv[1])
    command = sys.argv[2:]
    result = subprocess.run(command, cwd=cwd)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
