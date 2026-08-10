#!/usr/bin/env python3
"""Create local store packages + GitHub plugin-store template."""
from __future__ import annotations

import json
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SAMPLES = {
	"store-ocean": {
		"folder": "store-ocean",
		"files": {
			"theme.json": json.dumps(
				{
					"palette": {
						"accent": "#3DDC97",
						"background": "#0B1410",
						"surface": "#132019",
						"text": "#E8FFF4",
						"windowBgActive": "#3DDC97",
						"dialogsBg": "#132019",
					}
				},
				indent=2,
			)
		},
		"meta": {
			"id": "store-ocean",
			"name": "Ocean Theme",
			"version": "1.0.0",
			"author": "Plugingram",
			"description": "Free drop-in ocean theme from the Plugingram GitHub store.",
			"type": "theme",
			"packageUrl": "packages/store-ocean.zip",
			"tags": ["theme", "free"],
			"permissions": ["ui.modify"],
		},
	},
	"store-hello": {
		"folder": "store-hello",
		"files": {
			"plugin.json": json.dumps(
				{
					"name": "Hello Panel",
					"description": "One-file UI panel from the store.",
					"panels": [
						{
							"id": "hello",
							"title": "Hello Store",
							"placement": "settings.sidebar",
						}
					],
					"actions": [{"id": "hello.run", "label": "Say hi"}],
				},
				indent=2,
			)
		},
		"meta": {
			"id": "store-hello",
			"name": "Hello Panel",
			"version": "1.0.0",
			"author": "Plugingram",
			"description": "Sample UI extension available in the store.",
			"type": "ui_extension",
			"packageUrl": "packages/store-hello.zip",
			"tags": ["ui", "free"],
			"permissions": ["ui.modify", "commands.register"],
		},
	},
}


def main() -> None:
	pkg = ROOT / "plugins" / "store" / "packages"
	pkg.mkdir(parents=True, exist_ok=True)
	entries = []
	for key, sample in SAMPLES.items():
		zpath = pkg / f"{key}.zip"
		with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as zf:
			for name, content in sample["files"].items():
				zf.writestr(f"{sample['folder']}/{name}", content)
		entries.append(sample["meta"])
		print("zip", zpath)

	catalog = {
		"schemaVersion": "1",
		"catalogTitle": "Plugingram Store",
		"entries": entries,
	}
	(ROOT / "plugins" / "store" / "index.json").write_text(
		json.dumps(catalog, indent=2, ensure_ascii=False) + "\n",
		encoding="utf-8",
	)

	gh = ROOT / "plugin-store"
	(gh / "files").mkdir(parents=True, exist_ok=True)
	for key in SAMPLES:
		src = pkg / f"{key}.zip"
		(gh / "files" / f"{key}.zip").write_bytes(src.read_bytes())

	remote_entries = []
	for e in entries:
		re = dict(e)
		re["packageUrl"] = (
			f"https://raw.githubusercontent.com/plugingram/plugin-store/main/files/{e['id']}.zip"
		)
		re["url"] = re["packageUrl"]
		remote_entries.append(re)

	(gh / "plugins.json").write_text(
		json.dumps(
			{
				"schemaVersion": "1",
				"catalogTitle": "Plugingram Community Store",
				"entries": remote_entries,
			},
			indent=2,
			ensure_ascii=False,
		)
		+ "\n",
		encoding="utf-8",
	)
	print("ok")


if __name__ == "__main__":
	main()
