#!/usr/bin/env python3
"""Create a drop-in Plugingram plugin folder (no full manifest required).

Examples:
  python create_plugingram_plugin.py theme my-ocean
  python create_plugingram_plugin.py ui hello-panel
  python create_plugingram_plugin.py utility quick-tools
  python create_plugingram_plugin.py one-file neon-theme
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

_TOOLS = Path(__file__).resolve().parent
_ROOT = _TOOLS.parent
if (_ROOT / "plugins").is_dir() and (_ROOT / "Telegram").is_dir():
    PLUGINS = _ROOT / "plugins"  # repo export layout
else:
    PLUGINS = _ROOT / "plugingram" / "plugins"  # TG workspace layout


def slug(value: str) -> str:
    value = value.strip().lower()
    value = re.sub(r"[^a-z0-9._-]+", "-", value)
    value = re.sub(r"-{2,}", "-", value).strip("-.")
    return value or "plugin"


def humanize(value: str) -> str:
    parts = re.split(r"[-_\s]+", value.strip())
    return " ".join(p[:1].upper() + p[1:] for p in parts if p) or "Plugin"


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    print("created:", path)


def make_theme(folder: Path, name: str) -> None:
    write(
        folder / "theme.json",
        json.dumps(
            {
                "palette": {
                    "accent": "#3DDC97",
                    "background": "#0B1410",
                    "surface": "#132019",
                    "text": "#E8FFF4",
                    "windowBgActive": "#3DDC97",
                    "dialogsBg": "#132019",
                    "msgInBg": "#1A2B22",
                    "msgOutBg": "#234536",
                }
            },
            indent=2,
            ensure_ascii=False,
        )
        + "\n",
    )
    print(f"Theme ready: {folder.name}")
    print("Open Settings → Plugins → refresh, then toggle it on.")


def make_ui(folder: Path, name: str) -> None:
    write(
        folder / "ui.json",
        json.dumps(
            {
                "panels": [
                    {
                        "id": f"{folder.name}-panel",
                        "title": name,
                        "placement": "settings.sidebar",
                    }
                ],
                "actions": [
                    {
                        "id": f"{folder.name}.hello",
                        "label": f"Run {name}",
                        "description": "Demo action from a drop-in UI plugin",
                    }
                ],
            },
            indent=2,
            ensure_ascii=False,
        )
        + "\n",
    )


def make_utility(folder: Path, name: str) -> None:
    write(
        folder / "utility.json",
        json.dumps(
            {
                "name": name,
                "description": f"{name} utility",
                "commands": [
                    {
                        "id": f"{folder.name}.run",
                        "title": f"Run {name}",
                        "description": "Demo utility command",
                    }
                ],
            },
            indent=2,
            ensure_ascii=False,
        )
        + "\n",
    )


def make_one_file(folder: Path, name: str) -> None:
    write(
        folder / "plugin.json",
        json.dumps(
            {
                "name": name,
                "description": "One-file theme plugin",
                "palette": {
                    "accent": "#FF8A3D",
                    "background": "#16120F",
                    "surface": "#241C16",
                    "text": "#FFF6EE",
                },
            },
            indent=2,
            ensure_ascii=False,
        )
        + "\n",
    )


def main(argv: list[str]) -> int:
    if len(argv) < 3 or argv[1] in {"-h", "--help"}:
        print(__doc__)
        return 0

    kind = argv[1].lower()
    name_raw = argv[2]
    folder_name = slug(name_raw)
    display = humanize(name_raw)
    folder = PLUGINS / folder_name
    if folder.exists() and any(folder.iterdir()):
        print(f"Folder already exists and is not empty: {folder}")
        return 1

    folder.mkdir(parents=True, exist_ok=True)
    makers = {
        "theme": make_theme,
        "ui": make_ui,
        "ui_extension": make_ui,
        "utility": make_utility,
        "one-file": make_one_file,
        "plugin": make_one_file,
    }
    maker = makers.get(kind)
    if not maker:
        print(f"Unknown kind: {kind}. Use theme|ui|utility|one-file")
        return 1

    maker(folder, display)
    print(f"Folder: {folder}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
