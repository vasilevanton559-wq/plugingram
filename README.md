# Plugingram

**Статус: pre-beta**

Неофициальный клиент на базе [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) **7.0.9** (Windows x64).  
Не связан с Telegram Messenger LLP. Лицензия: GPLv3 + OpenSSL exception (`LEGAL`, `LICENSE`).

Канал: [@plugingram_official](https://t.me/plugingram_official)

---

## Фиксированный функционал клиента

Ниже — то, что входит в сборку. Скачиваемые плагины из Store сюда не входят: у них свои описания в репозиториях авторов.

### 1. База Telegram Desktop 7.0.9
- Несколько аккаунтов, чаты, группы, каналы, секретные чаты  
- Папки чатов, архив, закреплённые диалоги  
- Сообщения: текст, фото, видео, документы, голосовые, видеосообщения, стикеры, GIF, реакции, опросы  
- Редактирование, ответы, пересылка, поиск по чатам  
- Звонки и групповые звонки (как в upstream)  
- Профиль, эмодзи-статус, stories (как в upstream)  
- Настройки: уведомления, приватность, данные и память, язык, прокси  
- Night mode и темы Telegram Desktop  
- Локальная база `tdata`, синхронизация MTProto  

### 2. Брендинг Plugingram
- Имя приложения и exe: **Plugingram**  
- В боковом меню: `@plugingram_official` → канал  
- Справа от названия **Plugingram** — метка **pre-beta**  
- Окно «О программе»: заголовок `Plugingram pre-beta`  

### 3. Система плагинов
- Раздел **Настройки → Плагины**  
- Список установленных: вкл/выкл, избранное, удаление (встроенные нельзя удалить)  
- **Store**: поиск публичных репозиториев GitHub с topic `plugingram-plugin`, Refresh / Install / Enable / Update  
- Каталог на диске рядом с exe: `plugins/`  
- Состояние: `plugins/state.json`  
- Переопределения путей: `PLUGINGRAM_PLUGINS`, `PLUGINGRAM_USE_PROJECT_PLUGINS=1`  
- Host API v2 для `plugin.js` (QuickJS):  
  - панели (`settings.sidebar`, `chat.composer`)  
  - кнопки composer (до 3)  
  - opacity окна (не ниже 0.35)  
  - цвета палитры (hex, лимит ключей)  
  - scale / font (с рестартом)  
  - chrome (filters / mainMenu / search)  
  - toast / alert  
  - язык (`lang.get` / `lang.set`)  
  - локальный storage в папке плагина  
  - `log`, `onAction`  
- Sandbox: нет доступа к сообщениям, аккаунту, сети; только папка плагина  
- Сбой плагина: toast с причиной → автоотключение примерно через 3 с  

### 4. Встроенный плагин Noise
- Id: `noise`, bundled, не удаляется  
- Блюр телефонов и кодов в профилях шумом спойлера Telegram  
- Клик по блюру — показать значение  
- Вкл/выкл в списке плагинов  
- Синхронизация с пунктом spoiler в контекстном меню номера  

### 5. Диагностика
- **Ctrl+Shift+I** — оверлей Diagnostics: версия, uptime, окна, память, сессия, MTP, плагины, пути, последние ошибки и действия  
- Кнопки: Open Logs, Refresh  
- Логи в `{workingDir}/logs/`:  
  - `session_YYYYMMDD.log`  
  - `errors.log`  
  - `last_actions.log`  
  - `crash_*.log` при аварийном завершении  

### 6. Сборка (Windows x64)
- Скрипты в `tools/` — пути от расположения репозитория / env (`PLUGINGRAM_ROOT`, `PLUGINGRAM_LIBRARIES`, …)  
- `python tools/plugingram_build_exe.py` — configure + Release  
- `python tools/plugingram_build_only.py` — только пересборка  
- `python tools/export_plugingram_github.py` — чистый исходник для GitHub  
- `python tools/plugingram_package_release.py` — zip из `out/Release`  
## Документация
- Плагины (API): [`plugins/README.md`](plugins/README.md)  
- Публикация в Store: [`plugin-store/README.md`](plugin-store/README.md)  
- Сборка Windows: [`docs/building-win.md`](docs/building-win.md)  
- Helpers: [`tools/README.md`](tools/README.md)  

## Структура

```
Telegram/       исходники клиента
plugins/        встроенный Noise + документация API
plugin-store/   как публиковать плагин в Store
tools/          скрипты сборки и экспорта
docs/           сборка и api credentials
```
