import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from plugingram_paths import TELEGRAM_DIR, apply_common_build_env  # noqa: E402

PREPARE_DIR = TELEGRAM_DIR / "build" / "prepare"
PREPARE_FILE = PREPARE_DIR / "prepare.py"

SKIP_STAGES = {"breakpad", "crashpad"}


def patch_skip_stages(text: str) -> str:
    marker = "def stage(name, commands, location = 'Libraries'):"
    if marker not in text:
        raise RuntimeError("prepare.py stage() definition not found")
    injection = (
        marker
        + "\n    if name in "
        + repr(sorted(SKIP_STAGES))
        + ":\n        return"
    )
    return text.replace(marker, injection, 1)


def patch_prepare_tail(text: str) -> str:
    old = """if win:
    currentCodePage = subprocess.run('chcp', capture_output=True, shell=True, text=True, env=modifiedEnv).stdout.strip().split()[-1]
    subprocess.run('chcp 65001 > nul', shell=True, env=modifiedEnv)
    runStages()
    subprocess.run('chcp ' + currentCodePage + ' > nul', shell=True, env=modifiedEnv)
else:
    runStages()
"""
    if old not in text:
        raise RuntimeError("prepare.py tail block not found")
    return text.replace(old, "runStages()\n")


def main() -> None:
    apply_common_build_env(keep_strawberry_c_last=True)
    os.environ["CC"] = "cl"
    os.environ["CXX"] = "cl"

    os.chdir(PREPARE_DIR)
    source = PREPARE_FILE.read_text(encoding="utf-8")
    source = patch_skip_stages(source)
    source = patch_prepare_tail(source)

    sys.argv = [str(PREPARE_FILE)] + sys.argv[1:]
    namespace = {
        "__file__": str(PREPARE_FILE),
        "__name__": "__main__",
    }
    exec(compile(source, str(PREPARE_FILE), "exec"), namespace)


if __name__ == "__main__":
    main()
