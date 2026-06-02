# Тестирование OpenLogReplicator-VK

Регрессионные тесты: для каждого SQL-сценария поднимается Oracle, выполняется SQL,
захватываются redo-логи, OLR запускается в batch-режиме, и его JSON-вывод
сверяется с эталоном LogMiner. Расхождение = падение теста.

## Структура

```
tests/
├── 0-inputs/           # SQL-сценарии
├── 1-environments/
│   ├── xe-21/          # Oracle XE 21c   (docker-compose)
│   └── free-23/        # Oracle Free 23c (docker-compose)
├── 3-generated/        # Сгенерированные redo / schema / expected
└── scripts/generate.sh # Конвейер генерации + валидации фикстуры
```

| Среда | Версия | Образ |
|-------|--------|-------|
| `xe-21`   | Oracle XE 21c   | `gvenzl/oracle-xe:21.3.0-slim-faststart` |
| `free-23` | Oracle Free 23c | `gvenzl/oracle-free:23-slim-faststart` |

## Запуск

Тесты выполняются **внутри образа `olr-test`** — его собирает соседний репо
[`openlogreplicator-docker-vk`](https://gitlab.corp.mail.ru/tarantool/cdc/v9/openlogreplicator-docker-vk)
через свой `Dockerfile` с `--build-arg WITHTESTS=1`. В CI это происходит автоматически
(см. `.gitlab-ci.yml`). Локально:

```bash
# 1. собрать test-образ (один раз).
cd ../openlogreplicator-docker-vk   # переходим в соседний docker-репозиторий
docker pull registry-gitlab.corp.mail.ru/tarantool/cdc/v9/openlogreplicator-docker-vk/openlogreplicator-vk-base:latest
# копируем исходники этого репозитория внутрь контекста сборки docker-репозитория
mkdir -p OpenLogReplicator && cp -r ../openlogreplicator/. OpenLogReplicator/
docker build -f Dockerfile -t olr-test:local \
    --build-arg BASE_IMAGE=registry-gitlab.corp.mail.ru/tarantool/cdc/v9/openlogreplicator-docker-vk/openlogreplicator-vk-base:latest \
    --build-arg WITHTESTS=1 \
    --build-arg BUILD_TYPE=Debug \
    --build-arg WITHORACLE=1 --build-arg WITHKAFKA=1 \
    --build-arg WITHPROTOBUF=1 --build-arg WITHPROMETHEUS=1 .

# 2. запустить тесты
docker run --rm -it --network host \
    -v /var/run/docker.sock:/var/run/docker.sock \
    --user root \
    --entrypoint bash \
    olr-test:local -lc \
    "cd /opt/OpenLogReplicator-local && make test ORACLE_TARGET=xe-21"
```

`test` = `up` → `testgen` → `down`. Целевая среда — `xe-21` или `free-23`.

Отдельные команды внутри запущенного контейнера. Сначала открыть shell в образе:

```bash
docker run --rm -it --network host \
    -v /var/run/docker.sock:/var/run/docker.sock \
    --user root --entrypoint bash olr-test:local
cd /opt/OpenLogReplicator-local
```

```bash
make up       ORACLE_TARGET=xe-21              # поднять oracle + archivelog/grants
make testgen  ORACLE_TARGET=xe-21              # сгенерировать + проверить все фикстуры
make down     ORACLE_TARGET=xe-21              # остановить oracle + снести volume
```

В консоли — `PASS:` / `ERROR:` по каждому сценарию. `testgen` для каждого
`0-inputs/*.sql` вызывает `scripts/generate.sh` (с таймаутом `CI_TESTGEN_TIMEOUT`, по умолчанию 300с).

## Что делает `generate.sh`

Из одного SQL-сценария делает фикстуру: выполняет SQL в Oracle → копирует
архивные redo-логи → снимает схему (`gencfg.sql`) → прогоняет LogMiner (эталон)
и OLR (batch) → сверяет их и, при совпадении, сохраняет **вывод OLR** как
golden (`3-generated/expected/<name>/output.json`).

> LogMiner здесь — независимый эталон для проверки на этапе генерации; в golden
> попадает именно вывод OLR.

Подробный разбор стадий, переменных окружения и маркеров SQL — в
[`scripts/README.md`](scripts/README.md).

## Новый сценарий

Создайте `0-inputs/<name>.sql`:

```sql
CREATE TABLE test_table (id NUMBER PRIMARY KEY, name VARCHAR2(100));
ALTER TABLE test_table ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

DECLARE v_scn NUMBER;
BEGIN
  SELECT current_scn INTO v_scn FROM v$database;
  dbms_output.put_line('FIXTURE_SCN_START: ' || v_scn);
END;
/

INSERT INTO test_table VALUES (1, 'test');
COMMIT;

EXIT;
```

Правила:

- Обязателен вывод `FIXTURE_SCN_START: <scn>` и завершающий `EXIT`.
- DDL-сценарии: добавьте `-- @DDL` в начало файла (включит `DICT_FROM_REDO_LOGS` + `DDL_DICT_TRACKING` в LogMiner).
- Транзакции через границы архивлогов: маркеры `-- @MID_SWITCH` и `DBMS_SESSION.SLEEP()` в нужных точках.
