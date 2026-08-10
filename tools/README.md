# Plugingram tools (pre-beta)

Скрипты сборки и экспорта. Пути считаются от расположения `tools/` или из env, без привязки к конкретному ПК.

## Layout

- Dev workspace: `tools/` рядом с `plugingram/` и `Libraries/`  
- GitHub export: `tools/` рядом с `Telegram/` (`PLUGINGRAM_LIBRARIES` при необходимости)

## Env

| Variable | Purpose |
|----------|---------|
| `PLUGINGRAM_ROOT` / `PLUGINGRAM_WORKSPACE` | Корень workspace / репо |
| `PLUGINGRAM_LIBRARIES` | Путь к `Libraries/win64` |
| `PLUGINGRAM_THIRDPARTY` | Путь к `ThirdParty` |
| `PLUGINGRAM_VCVARS` | Полный путь к `vcvars64.bat` |
| `PLUGINGRAM_EXPORT` | Куда класть GitHub-export |

## Команды

```bat
python tools\plugingram_build_exe.py
python tools\plugingram_build_only.py
python tools\export_plugingram_github.py
python tools\plugingram_package_release.py
```
