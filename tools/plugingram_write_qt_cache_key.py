"""Compute and write prepare.py cache key for qt_5.15.19 without rebuilding."""
import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from plugingram_paths import TELEGRAM_DIR  # noqa: E402

PREPARE_DIR = TELEGRAM_DIR / "build" / "prepare"
PREPARE_FILE = PREPARE_DIR / "prepare.py"
STAGE_NAME = "qt_5.15.19"


def main() -> int:
    os.chdir(PREPARE_DIR)
    # Avoid executing runStages(); load definitions only.
    source = PREPARE_FILE.read_text(encoding="utf-8")
    # Truncate at the final runStages invocation block.
    cut = source.find("\nif win:\n    currentCodePage")
    if cut < 0:
        cut = source.find("\nrunStages()")
    if cut < 0:
        raise RuntimeError("Could not locate prepare.py runStages tail")
    source = source[:cut] + "\n"

    ns: dict = {
        "__file__": str(PREPARE_FILE),
        "__name__": "__prepare_defs__",
        "sys": sys,
    }
    # prepare.py expects argv like the script itself.
    sys.argv = [str(PREPARE_FILE), "silent"]
    exec(compile(source, str(PREPARE_FILE), "exec"), ns)

    stages = ns["stages"]
    write_cache_key = ns["writeCacheKey"]
    compute_cache_key = ns["computeCacheKey"]
    check_cache_key = ns["checkCacheKey"]

    stage = next((s for s in stages if s["name"] == STAGE_NAME), None)
    if stage is None:
        raise RuntimeError(f"Stage not found: {STAGE_NAME}")
    stage["key"] = compute_cache_key(stage)
    write_cache_key(stage)
    status = check_cache_key(stage)
    print(f"WROTE {STAGE_NAME} key={stage['key']} status={status}")
    return 0 if status == "Good" else 1


if __name__ == "__main__":
    raise SystemExit(main())
