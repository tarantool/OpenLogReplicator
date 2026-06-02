# tests/scripts/

Скрипты конвейера регрессионного тестирования. Здесь сосредоточена основная
логика генерации фикстур — отдельно от общего обзора в [`../README.md`](../README.md),
который описывает *как запускать* тесты. Этот файл описывает, *что именно делают*
скрипты внутри.

| Скрипт | Роль |
|--------|------|
| [`oracle-setup.sh`](oracle-setup.sh) | Подготовка БД-контейнера: archivelog + supplemental logging + гранты (идемпотентно). |
| [`generate.sh`](generate.sh) | Главный конвейер: из одного SQL-сценария делает и валидирует одну фикстуру. |
| [`logminer2json.py`](logminer2json.py) | Конвертирует pipe-вывод LogMiner в канонический JSON (эталон). |
| [`compare.py`](compare.py) | Сравнивает эталон LogMiner с выводом OLR (type-aware). |

## Ключевые понятия

- **Эталон (reference)** — вывод **LogMiner** по тем же redo-логам. Это «правда»,
  относительно которой проверяется OLR.
- Проверка делается **на этапе генерации**: `generate.sh` прогоняет OLR и
  сравнивает его вывод с эталоном LogMiner (`compare.py`). Если не сошлось —
  сценарий падает. Вывод OLR при совпадении сохраняется в
  `3-generated/expected/<fixture>/output.json` как артефакт прогона.

## `generate.sh` — конвейер для одного сценария

```
./generate.sh <scenario>          # напр. ./generate.sh basic-crud
ORACLE_TARGET=xe-21 ./generate.sh basic-crud
```

Вход — `tests/0-inputs/<scenario>.sql`. Имя фикстуры — `<scenario>-<ORACLE_TARGET>`.
Всё происходит из одного контейнера `olr-test`: к Oracle — через `docker exec`
(sqlplus) и `docker cp`, а сам OLR запускается локально тем же бинарником.

### Поток (stages)

```
вход: tests/0-inputs/<scenario>.sql
   │
   │  [Stage 0]  только если в файле есть маркер `-- @DDL`:
   │             DBMS_LOGMNR_D.BUILD → словарь LogMiner пишется в redo-логи
   │             (режим DICT_FROM_REDO_LOGS; нужно, чтобы LogMiner понимал DDL)
   ▼
[Stage 1]  Выполнить SQL-сценарий в Oracle (sqlplus)
           ├─ из вывода берётся FIXTURE_SCN_START  (печатает сам сценарий)
           ├─ ALTER SYSTEM SWITCH LOGFILE ×2        (сбросить redo в архив)
           └─ END_SCN = current_scn из v$database
   ▼
[Stage 2]  Скопировать архивные redo-логи за [START_SCN, END_SCN]
           из контейнера Oracle  →  tests/3-generated/redo/<fixture>/
   ▼
[Stage 3]  Снять схему: gencfg.sql (пропатченный под TEST/SCHEMA_OWNER/START_SCN)
           →  tests/3-generated/schema/<fixture>/TEST-chkpt-<SCN>.json
   ▼
[Stage 4]  LogMiner по тем же логам  →  logminer2json.py
           →  logminer.json  =  ЭТАЛОН
   ▼
[Stage 5]  Запустить OLR в batch-режиме по захваченным redo-логам
           (config генерируется на лету: reader=batch, writer=file)
           →  olr_output.json
   ▼
[Stage 6]  compare.py:  logminer.json (эталон)  vs  olr_output.json
   │
   ├─ совпало ──► [Stage 7] сохранить GOLDEN:
   │                tests/3-generated/expected/<fixture>/output.json   ← вывод OLR
   │                tests/3-generated/expected/<fixture>/logminer-reference.json
   │                === PASS ===
   │
   └─ не совпало ─► work-каталог сохраняется для отладки, === FAIL ===, exit 1
```

Итог: фикстура — `tests/3-generated/`:
`redo/<fixture>/` (входные логи) + `schema/<fixture>/` (схема) + `expected/<fixture>/output.json` (golden).

### Переменные окружения

| Переменная | По умолчанию | Назначение |
|------------|--------------|------------|
| `ORACLE_TARGET` | `free-23` | Среда: `xe-21` или `free-23` (задаёт PDB по умолчанию: `XEPDB1` / `FREEPDB1`). |
| `ORACLE_CONTAINER` | `oracle` | Имя docker-контейнера Oracle. |
| `ORACLE_PASSWORD` | `oracle` | Пароль SYS/SYSTEM. |
| `DB_CONN` | `olr_test/olr_test@//localhost:1521/<PDB>` | Connect-строка тестового пользователя. |
| `SCHEMA_OWNER` | `OLR_TEST` | Схема-владелец таблиц (фильтр LogMiner и OLR). |
| `PDB_NAME` | по `ORACLE_TARGET` | Имя PDB. |

### Маркеры в SQL-сценариях

- `-- @DDL` в начале файла → включает Stage 0 и режим LogMiner
  `DICT_FROM_REDO_LOGS + DDL_DICT_TRACKING` (для сценариев с DDL).
- `-- @MID_SWITCH` → во время выполнения DML принудительно делается
  `SWITCH LOGFILE`, чтобы транзакция растянулась через границу архивлогов.
- Обязательно: сценарий печатает `FIXTURE_SCN_START: <scn>` и завершается `EXIT`.

## `logminer2json.py`

Парсит pipe-разделённый вывод LogMiner
(`SCN|OPERATION|SEG_OWNER|TABLE_NAME|XID|SQL_REDO|SQL_UNDO`) и превращает каждую
DML-операцию в одну JSON-строку с нормализованными полями
(`op`, `owner`, `table`, `xid`, `after`, `before`). Все значения колонок —
строки, чтобы сравнение не зависело от типа.

## `compare.py`

Сравнивает эталон LogMiner с выводом OLR:

- из OLR-вывода отбрасываются begin/commit/checkpoint-сообщения;
- операции маппятся `c→INSERT`, `u→UPDATE`, `d→DELETE`;
- сопоставление по порядку внутри каждой транзакции;
- сравнение имени таблицы и значений колонок — **type-aware** (`"100" == 100`,
  с учётом форматов дат/таймстампов Oracle).

Exit 0 — совпало, exit 1 — расхождение с отчётом diff.

## `oracle-setup.sh`

Приводит поднятый Oracle-контейнер в готовое для тестов состояние (вызывается из
`make up`). Идемпотентно — повторный запуск безопасен.

```bash
./oracle-setup.sh <xe-21|free-23>
```

Делает в одной sqlplus-сессии (состояние сохраняется между SHUTDOWN/STARTUP):
1. включает ARCHIVELOG (`SHUTDOWN` → `STARTUP MOUNT` → `ALTER DATABASE ARCHIVELOG` → `OPEN`);
2. открывает все PDB и `SAVE STATE`, включает supplemental logging, задаёт `db_recovery_file_dest_size`;
3. внутри PDB применяет канонический набор грантов из
   [`../../scripts/grants.sql`](../../scripts/grants.sql) (плейсхолдер `<USER>`
   подставляется на `APP_USER`, по умолчанию `olr_test`);
4. ждёт, пока healthcheck Oracle снова поднимется.
