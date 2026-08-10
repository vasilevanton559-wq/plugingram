# Windows: сборка Plugingram (pre-beta)

## Требования

Как в [`building-win.md`](building-win.md):

- Visual Studio 2022/2026  
- Windows SDK `10.0.26100.0`  
- Python 3.10+  
- Git  
- Подготовленное дерево `Libraries/` (через `Telegram/build/prepare` или уже собранные libs)

## Исходники плагинов в дереве

- `Telegram/cmake/td_plugins.cmake`  
- `Telegram/SourceFiles/plugin_system/*`  
- `Telegram/SourceFiles/settings/sections/settings_plugins.*`  
- `plugins/` — встроенный Noise и документация  

## Сборка

Из workspace (`Libraries/`, `plugingram/` или экспорт с `tools/`):

```bat
python tools\plugingram_build_exe.py
```

Только пересборка:

```bat
python tools\plugingram_build_only.py
```

Результат: `out\Release\Plugingram.exe`

## Runtime

Рядом с exe создаётся `plugins/` и при работе — `plugins/state.json`.  
Логи: `{workingDir}\logs\`.
