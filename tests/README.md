# Тестирование OpenLogReplicator

Регрессионные тесты: для каждого SQL-сценария поднимается Oracle, выполняется SQL,
захватываются redo-логи, OLR запускается в batch-режиме, и его JSON-вывод
сверяется с эталоном LogMiner. Расхождение приводит к падению теста.

## Структура

```
tests/
├── 0-inputs/           # SQL-сценарии
├── 1-environments/
│   ├── xe-21/          # Oracle XE 21c (Docker Compose)
│   └── free-23/        # Oracle Free 23c (Docker Compose)
├── 3-generated/        # Сгенерированные redo, schema и expected
└── scripts/generate.sh # Конвейер генерации и проверки фикстуры
```

| Среда | Версия | Образ |
|-------|--------|-------|
| `xe-21` | Oracle XE 21c | `gvenzl/oracle-xe:21.3.0-slim-faststart` |
| `free-23` | Oracle Free 23c | `gvenzl/oracle-free:23.5-slim-faststart` |

## Требования

- Docker с плагином Docker Compose;
- доступ к Docker daemon из тестового контейнера;
- базовый образ `ghcr.io/tarantool/openlogreplicator-base:latest`, опубликованный
  из репозитория
  [`tarantool/openlogreplicator-docker`](https://github.com/tarantool/openlogreplicator-docker).

## Запуск

Тесты выполняются внутри образа `openlogreplicator-test:local`. Его Dockerfile
находится в отдельном публичном репозитории
[`tarantool/openlogreplicator-docker`](https://github.com/tarantool/openlogreplicator-docker).
Следующие команды предполагают, что репозитории `openlogreplicator` и
`openlogreplicator-docker` клонированы в один каталог:

```bash
cd ../openlogreplicator-docker

docker pull ghcr.io/tarantool/openlogreplicator-base:latest

# Добавить исходники OLR в контекст сборки Docker-репозитория.
mkdir -p OpenLogReplicator
cp -R ../openlogreplicator/. OpenLogReplicator/

docker build -f Dockerfile -t openlogreplicator-test:local \
    --build-arg BASE_IMAGE=ghcr.io/tarantool/openlogreplicator-base:latest \
    --build-arg WITHTESTS=1 \
    --build-arg BUILD_TYPE=Debug \
    --build-arg WITHORACLE=1 \
    --build-arg WITHKAFKA=1 \
    --build-arg WITHPROTOBUF=1 \
    --build-arg WITHPROMETHEUS=1 .
```

Запустить полный тестовый цикл для Oracle XE 21c:

```bash
docker run --rm -it --network host \
    -v /var/run/docker.sock:/var/run/docker.sock \
    --user root \
    --entrypoint bash \
    openlogreplicator-test:local -lc \
    "cd /opt/OpenLogReplicator-local && make test ORACLE_TARGET=xe-21"
```

`make test` выполняет `up` → `testgen` → `down`. Поддерживаемые значения
`ORACLE_TARGET`: `xe-21` и `free-23`.

Для запуска отдельных этапов сначала откройте shell внутри тестового образа:

```bash
docker run --rm -it --network host \
    -v /var/run/docker.sock:/var/run/docker.sock \
    --user root \
    --entrypoint bash \
    openlogreplicator-test:local

cd /opt/OpenLogReplicator-local
```

Затем выполните нужный этап:

```bash
make up      ORACLE_TARGET=xe-21 # поднять Oracle и применить archivelog/grants
make testgen ORACLE_TARGET=xe-21 # сгенерировать и проверить все фикстуры
make down    ORACLE_TARGET=xe-21 # остановить Oracle и удалить volumes
```

В консоли выводится `PASS:` или `ERROR:` для каждого сценария. `testgen`
вызывает `tests/scripts/generate.sh` для каждого файла `tests/0-inputs/*.sql`.
Таймаут одного сценария задаётся переменной `TESTGEN_TIMEOUT`; значение по
умолчанию — 300 секунд.

## Что делает `generate.sh`

Из одного SQL-сценария скрипт создаёт фикстуру: выполняет SQL в Oracle,
копирует архивные redo-логи, снимает схему через `gencfg.sql`, запускает LogMiner
как эталон и сравнивает его вывод с результатом OLR в batch-режиме. При успешном
сравнении вывод OLR сохраняется как golden-файл в
`3-generated/expected/<fixture>/output.json`.

Подробное описание стадий, переменных окружения и маркеров SQL находится в
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

- обязателен вывод `FIXTURE_SCN_START: <scn>` и завершающий `EXIT`;
- для DDL-сценариев добавьте `-- @DDL` в начало файла;
- для переключения redo-лога во время сценария используйте `-- @MID_SWITCH`.
