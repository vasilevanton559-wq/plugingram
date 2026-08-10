# Security notes (pre-beta)

## Do not commit

- Account data: `tdata/`, session dumps, crash dumps  
- Secrets: `.env`, private API credentials, bot tokens, signing keys  
- Local plugin state: `plugins/state.json`, per-plugin storage  
- Build outputs: `out/`, `*.exe`, `*.pdb`  

## API credentials

Свои `api_id` / `api_hash`: https://my.telegram.org  
Не отдавать публичную сборку с тестовыми ключами из `docs/api_credentials.md`.

Локальный файл для сборки релиза: `tools/plugingram_api_credentials_local.py`  
(пример: `tools/plugingram_api_credentials_local.example.py`).  
Его нельзя коммитить и нельзя класть в GitHub export.

## Reporting

Неофициальный форк. Уязвимости — приватно мейнтейнерам. Upstream Telegram Desktop: https://github.com/telegramdesktop/tdesktop
