# How to publish a Plugingram plugin on GitHub

**Client status: pre-beta** — store discovery and Host API may change.

Plugingram Store searches public GitHub repositories with the client topic. Anyone can publish; anyone can install.

## 1) Create a public repository

Put your plugin files in the repo (theme / UI JSON and optional `plugin.js`).

## 2) Add the discovery topic (required)

Repository → **About** → **Topics** → add:

`plugingram-plugin`

Optional extra topic: `plugingram`

Without this topic the client will not reliably find the repo.

## 3) Add `plugingram.json` in the repo root (recommended)

```json
{
  "id": "my-ocean-theme",
  "name": "Ocean Theme",
  "version": "1.0.0",
  "author": "@you",
  "description": "Ocean theme for Plugingram.",
  "type": "theme",
  "icon": "icon.png",
  "packageUrl": "https://github.com/YOU/REPO/releases/download/v1.0.0/plugin.zip",
  "repoUrl": "https://github.com/YOU/REPO",
  "tags": ["theme", "plugingram-plugin"],
  "permissions": ["ui.modify"]
}
```

### JavaScript plugins (functional)

Add `plugin.js` next to the JSON (aliases: `plugin-script.js`, `main.js`).  
The store zip/zipball already includes it — after Install + Enable the client runs the script in a sandbox (QuickJS).

```js
function onEnable() {
  plugingram.ui.addPanel({
    id: "tools",
    title: "My Tools",
    placement: "settings.sidebar",
    actions: [{ id: "go", label: "Go" }]
  });
  plugingram.onAction("go", function () {
    plugingram.ui.toast("Hello");
  });
}
function onDisable() {}
```

Recommended permissions: `script.run`, `ui.modify`, `commands.register`.

### Icon (optional)

Put an image in the repo and point to it:

- easiest: file `icon.png` (also `icon.jpg`, `logo.png`, `assets/icon.png`)
- or `"icon": "icon.png"` in `plugingram.json`
- or `"icon": "https://raw.githubusercontent.com/YOU/REPO/main/icon.png"`

The client shows it on the Installed plugins card instead of the default type icon.

### `packageUrl` options

1. **Best:** GitHub Release asset (zip of the plugin folder)
2. Relative path in the same repo, e.g. `files/plugin.zip`  
   → client resolves to `raw.githubusercontent.com/...`
3. If omitted: client downloads the whole repo zipball from GitHub

Only `https://github.com` / `*.githubusercontent.com` URLs are accepted.

## 4) Open Plugingram → Plugins → Store → Refresh

The client calls GitHub Search:

`topic:plugingram-plugin OR topic:plugingram OR plugingram-plugin`

Then loads each repo’s `plugingram.json` (or synthesizes an entry from the repo).

Use the **link icon** on a card to open the repository page in the browser.

## Safety

- HTTPS only, GitHub hosts only
- Zip-slip path checks
- Size limits
- Install lands in `plugins/<id>/` disabled until you enable it
- JS sandbox: no network, no account/session access, storage only inside the plugin folder
- Plugin id is pinned to the GitHub repo (no foreign id takeover on refresh)
