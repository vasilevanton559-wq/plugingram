# Plugin System MVP

This MVP adds a controlled plugin surface to a Telegram Desktop fork.

## Runtime pieces

- `Telegram/SourceFiles/plugin_system/plugins_manager.*`
  Discovers modules in the local `plugins/` directory, persists enable/disable
  state, and loads the local store index contract.
- `Telegram/SourceFiles/plugin_system/plugins_manifest.*`
  Validates `manifest.json` files and normalizes module metadata.
- `Telegram/SourceFiles/plugin_system/plugins_store.*`
  Parses the local `plugins/store/index.json` catalog as a stand-in for a
  future backend.
- `Telegram/SourceFiles/settings/sections/settings_plugins.*`
  Exposes module management inside Telegram Desktop settings.

## Module contract

Each module lives in `plugins/<module-id>/` and must include `manifest.json`.

Supported module types in the MVP:

- `theme`
- `ui_extension`
- `utility`

Supported permissions in the MVP:

- `ui.modify`
- `commands.register`
- `settings.readwrite`
- `store.browse`

Example manifest:

```json
{
  "id": "sample-ui",
  "name": "Sample UI Extension",
  "version": "0.1.0",
  "author": "Cursor MVP",
  "description": "Example UI module",
  "type": "ui_extension",
  "entry": "ui.json",
  "permissions": ["ui.modify", "commands.register"]
}
```

## Persistence

The manager writes enable/disable state to `plugins/state.json`.

## Store contract

The MVP store is local-only and reads `plugins/store/index.json` with:

- `schemaVersion`
- `catalogTitle`
- `entries[]`

Each store entry includes:

- `id`
- `name`
- `version`
- `author`
- `description`
- `type`
- `minApiVersion`
- `packageUrl`
- `permissions`
- `tags`

## Next steps

- replace the local store index with a signed remote catalog;
- wire approved module types into real Telegram Desktop extension points;
- add version compatibility checks against Telegram builds and plugin API
  revisions;
- introduce signature verification before module activation.
