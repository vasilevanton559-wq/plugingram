# Plugingram Plugins

**Статус клиента: pre-beta**

Плагины ставятся из **Store (GitHub)** или кладутся вручную в `plugins/`. В сборке по умолчанию есть только встроенный **Noise**.

Клиент по умолчанию хранит скачанные плагины рядом с exe:

`Plugingram.exe` → `plugins/` (пусто, пока ничего не установили)

Переопределения для разработки:

- `PLUGINGRAM_PLUGINS` — путь к каталогу плагинов  
- `PLUGINGRAM_USE_PROJECT_PLUGINS=1` — использовать `plugingram/plugins` из репозитория  

## Как опубликовать плагин (GitHub)

1. Public repo + topic `plugingram-plugin`  
2. В корне: `plugingram.json` + `plugin.js` (и `icon.png`)  
3. В клиенте: Plugins → Store → Refresh → Install → Enable  

См. `plugin-store/README.md`.

## Host API (`plugin.js`) — API v2

| API | Permission | Что делает |
|-----|------------|------------|
| `ui.addPanel({ id, title, description, placement, actions })` | `ui.modify` / `ui.composer` | Панель: `settings.sidebar` или `chat.composer` |
| `ui.addButton({ id, label, placement })` | `ui.composer` | Кнопка (по умолчанию `chat.composer`, max 3) |
| `ui.setOpacity(0.35…1.0)` | `ui.opacity` | Прозрачность окна |
| `ui.setColors({ accent, windowBg, … })` | `ui.theme` | Палитра (до 64 ключей) |
| `ui.clearColors()` | `ui.theme` | Сброс палитры |
| `ui.setScale(75…300)` | `ui.scale` | Scale (нужен рестарт) |
| `ui.setFont("Segoe UI")` | `ui.scale` | Шрифт (нужен рестарт) |
| `ui.setChrome({ filters, mainMenu, search })` | `ui.chrome` | Показать/скрыть chrome |
| `ui.toast(text)` | — | Toast |
| `ui.alert({ title, text })` | — | Box |
| `lang.get()` / `lang.set(id)` | `settings.readwrite` | Язык (лимит 2/process) |
| `onAction(id, fn)` | `commands.register` | Обработчик кнопки |
| `storage.get/set/remove(key)` | — | Локально в папке плагина |
| `log(msg)` | — | Лог |

Для `plugin.js` клиент сам выдаёт UI-права (`ui.theme`, `ui.opacity`, `ui.chrome`, `ui.composer`, …) — длинный список в JSON не обязателен.

**Safety:** ошибки/невозможные вызовы не роняют клиент. Покажется причина → через ~3 с плагин выключится.  
Тяжёлые изменения (цвета/тема) применяются с задержкой, вне текущего UI-стека. Эмодзи в label кнопок срезаются до текста.

### Пример

```js
function onEnable() {
  plugingram.ui.setOpacity(0.92);
  plugingram.ui.setColors({ accent: "#4F7FFF", windowBg: "#10141F" });
  plugingram.ui.setChrome({ filters: true, mainMenu: true, search: true });
  plugingram.ui.addButton({
    id: "hello",
    label: "Hi",
    placement: "chat.composer"
  });
  plugingram.onAction("hello", function () {
    plugingram.ui.toast("Hello from plugin");
  });
}

function onDisable() {
  plugingram.ui.clearColors();
  plugingram.ui.setOpacity(1);
  plugingram.ui.setChrome({ filters: true, mainMenu: true, search: true });
}
```

### Безопасность

Нет доступа к сообщениям, аккаунту, сети, файловой системе вне папки плагина.  
Opacity ≥ 0.35, composer ≤ 3 кнопок, цвета только hex, font/scale — allowlist/clamp.
