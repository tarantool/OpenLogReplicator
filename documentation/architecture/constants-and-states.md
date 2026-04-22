# Константы и состояния OpenLogReplicator

## Состояния Metadata (репликация)

```mermaid
stateDiagram-v2
    [*] --> READY
    READY --> STARTING: run()
    STARTING --> REPLICATING: readerOpen OK
    STARTING --> READY: ошибка запуска
    REPLICATING --> [*]: shutdown

    classDef ready fill:#fff3cd,stroke:#f57f17,stroke-width:2px
    classDef starting fill:#bbdefb,stroke:#1565c0,stroke-width:2px
    classDef replicating fill:#c8e6c9,stroke:#2e7d32,stroke-width:3px
    classDef end fill:#ffcdd2,stroke:#c62828,stroke-width:2px

    class READY ready
    class STARTING starting
    class REPLICATING replicating
```

## Состояния Reader (поток чтения)

```mermaid
stateDiagram-v2
    [*] --> SLEEPING: инициализация

    SLEEPING --> CHECK: checkRedoLog()<br/>от Replicator
    CHECK --> SLEEPING: redoOpen() завершён<br/>ret = OK / ERROR

    SLEEPING --> UPDATE: updateRedoLog()<br/>от Replicator
    UPDATE --> SLEEPING: reloadHeader() завершён

    SLEEPING --> READ: setStatusRead()<br/>от Parser
    READ --> READ: read1()/read2()<br/>чтение блоков
    READ --> SLEEPING: EOF / FINISHED /<br/>OVERWRITTEN / SHUTDOWN

    SLEEPING --> [*]: shutdown

    classDef sleeping fill:#e3f2fd,stroke:#1565c0,stroke-width:2px
    classDef check fill:#fff9c4,stroke:#f57f17,stroke-width:2px
    classDef update fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px
    classDef read fill:#c8e6c9,stroke:#2e7d32,stroke-width:3px
    classDef end fill:#ffcdd2,stroke:#c62828,stroke-width:2px

    class SLEEPING sleeping
    class CHECK check
    class UPDATE update
    class READ read
```

## Результаты чтения Reader (REDO_CODE)

```mermaid
graph TB
    subgraph Success["✅ Успех"]
        OK["OK (0)"]
    end

    subgraph Normal["ℹ️ Нормальное завершение"]
        FINISHED["FINISHED (2)<br/>Конец файла"]
        STOPPED["STOPPED (3)<br/>Неожиданный конец"]
        SHUTDOWN["SHUTDOWN (4)<br/>Завершение работы"]
        EMPTY["EMPTY (5)<br/>Пустой блок"]
    end

    subgraph Switch["🔄 Log switch"]
        OVERWRITTEN["OVERWRITTEN (1)<br/>Лог перезаписан"]
    end

    subgraph Errors["❌ Ошибки"]
        ERR_READ["ERROR_READ (6)<br/>Ошибка чтения"]
        ERR_WRITE["ERROR_WRITE (7)<br/>Ошибка записи"]
        ERR_SEQ["ERROR_SEQUENCE (8)<br/>Несовпадение sequence#"]
        ERR_CRC["ERROR_CRC (9)<br/>Несовпадение checksum"]
        ERR_BLK["ERROR_BLOCK (10)<br/>Несовпадение номера блока"]
        ERR_DATA["ERROR_BAD_DATA (11)<br/>Неверные данные заголовка"]
        ERR["ERROR (12)<br/>Общая ошибка"]
    end

    style Success fill:#81c784,stroke:#2e7d32,stroke-width:3px
    style OK fill:#a5d6a7,stroke:#388e3c,stroke-width:2px
    style Normal fill:#64b5f6,stroke:#1565c0,stroke-width:2px
    style Switch fill:#ffb74d,stroke:#ef6c00,stroke-width:2px
    style Errors fill:#e57373,stroke:#c62828,stroke-width:2px
```

## Контексты потока Thread (профилирование)

```mermaid
graph TB
    subgraph Contexts["⏱️ Thread::CONTEXT"]
        CPU["🔥 CPU<br/>Вычисления"]
        OS["🖥️ OS<br/>Системные вызовы"]
        MUTEX["🔒 MUTEX<br/>Ожидание мьютекса"]
        WAIT["⏳ WAIT<br/>Ожидание условия"]
        SLEEP["💤 SLEEP<br/>Усыпление"]
        MEM["🧠 MEM<br/>Операции с памятью"]
        TRAN["💾 TRAN<br/>Операции с транзакциями"]
        CHKPT["💿 CHKPT<br/>Операции checkpoint"]
    end

    CPU <--> OS
    CPU <--> MUTEX
    CPU <--> WAIT
    WAIT <--> SLEEP
    CPU <--> MEM
    CPU <--> TRAN
    CPU <--> CHKPT

    classDef cpu fill:#ffcc80,stroke:#ef6c00,stroke-width:3px
    classDef os fill:#90caf9,stroke:#1565c0,stroke-width:2px
    classDef mutex fill:#ef9a9a,stroke:#c62828,stroke-width:2px
    classDef wait fill:#fff3cd,stroke:#f57f17,stroke-width:2px
    classDef sleep fill:#e1bee7,stroke:#6a1b9a,stroke-width:2px
    classDef mem fill:#c8e6c9,stroke:#2e7d32,stroke-width:2px
    classDef tran fill:#b39ddb,stroke:#4527a0,stroke-width:2px
    classDef chkpt fill:#80cbc4,stroke:#00695c,stroke-width:2px

    class CPU cpu
    class OS os
    class MUTEX mutex
    class WAIT wait
    class SLEEP sleep
    class MEM mem
    class TRAN tran
    class CHKPT chkpt
```

## Типы операций Oracle Redo (OpCode)

```mermaid
graph TB
    subgraph TransactionOps["Управление транзакциями"]
        OC050B["050B: Begin Transaction (KTUXV)"]
        OC0501["0501: Undo Record (KTUDH/KTUDV)"]
        OC0502["0502: Undo Done (KTUCF)"]
        OC0504["0504: Commit / Rollback (KTCOMM)"]
        OC0506["0506: Rollback"]
    end

    subgraph DMLOps["DML операции (KDO)"]
        OC0B02["0B02: INSERT (IRP)"]
        OC0B03["0B03: DELETE (DRP)"]
        OC0B04["0B04: UPDATE (URP)"]
        OC0B05["0B05: UPDATE multi-col (ORP)"]
        OC0B06["0B06: DELETE + header (DRP)"]
        OC0B08["0B08: Row Lock (LKR)"]
        OC0B0B["0B0B: Supplemental Log"]
        OC0B0C["0B0C: Compressed block"]
        OC0B10["0B10: Compressed data"]
        OC0B16["0B16: Extended KDO"]
    end

    subgraph DDLOps["DDL операции"]
        OC0513["0513: DDL"]
        OC0514["0514: DDL continuation"]
        OC1801["1801: DDL statement"]
    end

    subgraph LOBOps["LOB операции"]
        OC0A02["0A02: LOB Data"]
        OC0A08["0A08: LOB Index"]
        OC0A12["0A12: LOB operations"]
        OC1A02["1A02: LOB (new format)"]
        OC1A06["1A06: LOB (extended)"]
    end

    subgraph DirectPath["Direct Path"]
        OC1301["1301: Direct Path Load"]
    end

    style TransactionOps fill:#d4edda,stroke:#155724
    style DMLOps fill:#cce5ff,stroke:#004085
    style DDLOps fill:#fff3cd,stroke:#856404
    style LOBOps fill:#e2d5f1,stroke:#6f42c1
    style DirectPath fill:#f8d7da,stroke:#721c24
```

## Флаги Row (RedoLogRecord::FB_*)

```mermaid
graph LR
    subgraph RowFlags["🏷️ Флаги строки (byte)"]
        FBN["FB_N 0x01<br/>NULL"]
        FBP["FB_P 0x02<br/>Piece"]
        FBL["FB_L 0x04<br/>Last piece"]
        FBF["FB_F 0x08<br/>First piece"]
        FBD["FB_D 0x10<br/>Deleted"]
        FBH["FB_H 0x20<br/>Head piece"]
        FBC["FB_C 0x40<br/>Cluster key"]
        FBK["FB_K 0x80<br/>Row continues"]
    end

    classDef flag_null fill:#e3f2fd,stroke:#1565c0,stroke-width:2px
    classDef flag_piece fill:#fff3cd,stroke:#f57f17,stroke-width:2px
    classDef flag_last fill:#c8e6c9,stroke:#2e7d32,stroke-width:2px
    classDef flag_first fill:#ffcc80,stroke:#ef6c00,stroke-width:2px
    classDef flag_del fill:#ef9a9a,stroke:#c62828,stroke-width:2px
    classDef flag_head fill:#e1bee7,stroke:#6a1b9a,stroke-width:2px
    classDef flag_cluster fill:#b2dfdb,stroke:#00695c,stroke-width:2px
    classDef flag_cont fill:#bbdefb,stroke:#1565c0,stroke-width:2px

    class FBN flag_null
    class FBP flag_piece
    class FBL flag_last
    class FBF flag_first
    class FBD flag_del
    class FBH flag_head
    class FBC flag_cluster
    class FBK flag_cont
```

## Версии Oracle Redo

| Константа           | Значение     | Версия      |
|---------------------|--------------|-------------|
| `REDO_VERSION_12_1` | `0x0C100000` | Oracle 12.1 |
| `REDO_VERSION_12_2` | `0x0C200000` | Oracle 12.2 |
| `REDO_VERSION_18_0` | `0x12000000` | Oracle 18c  |
| `REDO_VERSION_19_0` | `0x13000000` | Oracle 19c  |
| `REDO_VERSION_23_0` | `0x17000000` | Oracle 23c  |

## Типы колонок Oracle (SysCol::COLTYPE)

```mermaid
graph TB
    subgraph CharTypes["Символьные"]
        VARCHAR["1: VARCHAR"]
        CHAR["96: CHAR"]
        CLOB["112: CLOB"]
        JSON["119: JSON"]
        XMLTYPE["58: XMLTYPE"]
    end

    subgraph NumTypes["Числовые"]
        NUMBER["2: NUMBER"]
        FLOAT["100: FLOAT"]
        DOUBLE["101: DOUBLE"]
        BINARY_F["100: BINARY_FLOAT"]
        BINARY_D["101: BINARY_DOUBLE"]
    end

    subgraph DateTypes["Дата/Время"]
        DATE["12: DATE"]
        TS["180: TIMESTAMP"]
        TS_TZ["181: TIMESTAMP WITH TZ"]
        TS_LTZ["231: TIMESTAMP WITH LOCAL TZ"]
        IYM["182: INTERVAL YEAR TO MONTH"]
        IDS["183: INTERVAL DAY TO SECOND"]
    end

    subgraph BinTypes["Бинарные"]
        RAW["23: RAW"]
        LONG_RAW["24: LONG_RAW"]
        BLOB["113: BLOB"]
        UROWID["208: UROWID"]
    end

    subgraph Other["Прочие"]
        LONG["8: LONG"]
        BOOLEAN["252: BOOLEAN"]
    end

    style CharTypes fill:#d4edda,stroke:#155724
    style NumTypes fill:#cce5ff,stroke:#004085
    style DateTypes fill:#fff3cd,stroke:#856404
    style BinTypes fill:#e2d5f1,stroke:#6f42c1
    style Other fill:#f8d7da,stroke:#721c24
```

## Формат вывода: параметры Format

```mermaid
graph TB
    subgraph FormatConfig["Format — конфигурация вывода"]
        direction TB
        DB_FMT["DB_FORMAT<br/>DEFAULT | ADD_DML | ADD_DDL | ALL"]
        TS_FMT["TIMESTAMP_FORMAT<br/>UNIX_NANO/MICRO/MILLI<br/>ISO8601 (16 вариантов)"]
        SCN_FMT["SCN_FORMAT<br/>NUMERIC | TEXT_HEX"]
        SCN_TYPE["SCN_TYPE (битовая маска)<br/>COMMIT_VALUE | BEGIN | DML<br/>COMMIT | DEBEZIUM"]
        XID_FMT["XID_FORMAT<br/>TEXT_HEX | TEXT_DEC |<br/>NUMERIC | TEXT_REVERSED"]
        CHAR_FMT["CHAR_FORMAT<br/>UTF8 | NOMAPPING | HEX"]
        COL_FMT["COLUMN_FORMAT<br/>CHANGED | FULL_INS_DEC | FULL_UPD"]
        SCHEMA_FMT["SCHEMA_FORMAT<br/>FULL | REPEATED | OBJ"]
        RID_FMT["RID_FORMAT<br/>SKIP | TEXT"]
        MSG_FMT["MESSAGE_FORMAT (битовая маска)<br/>FULL | ADD_SEQUENCES |<br/>SKIP_BEGIN | SKIP_COMMIT | ADD_OFFSET"]
        UNK_TYPE["UNKNOWN_TYPE<br/>HIDE | SHOW"]
        USER_TYPE["USER_TYPE (битовая маска)<br/>BEGIN | DML | COMMIT | DDL | DEBEZIUM"]
    end
```

## Протокол WriterStream (коды)

```mermaid
graph TB
    subgraph RequestCodes["RequestCode (клиент → сервер)"]
        REQ_INFO["0: INFO<br/>Запрос информации"]
        REQ_START["1: START<br/>Начать репликацию"]
        REQ_CONT["2: CONTINUE<br/>Продолжить"]
        REQ_CONF["3: CONFIRM<br/>Подтвердить получение"]
    end

    subgraph ResponseCodes["ResponseCode (сервер → клиент)"]
        RESP_READY["0: READY"]
        RESP_FAILED["1: FAILED_START"]
        RESP_STARTING["2: STARTING"]
        RESP_ALREADY["3: ALREADY_STARTED"]
        RESP_REPL["4: REPLICATE"]
        RESP_PAYLOAD["5: PAYLOAD"]
        RESP_INV_DB["6: INVALID_DATABASE"]
        RESP_INV_CMD["7: INVALID_COMMAND"]
    end

    subgraph OpCodes["Op (тип операции в выводе)"]
        OP_BEGIN["0: BEGIN"]
        OP_COMMIT["1: COMMIT"]
        OP_INSERT["2: INSERT"]
        OP_UPDATE["3: UPDATE"]
        OP_DELETE["4: DELETE"]
        OP_DDL["5: DDL"]
        OP_CHKPT["6: CHKPT"]
    end

    style RequestCodes fill:#cce5ff,stroke:#004085
    style ResponseCodes fill:#d4edda,stroke:#155724
    style OpCodes fill:#fff3cd,stroke:#856404
```

## Ключевые константы

### Ctx (инфраструктура)

| Константа             | Значение         | Описание                      |
|-----------------------|------------------|-------------------------------|
| `MEMORY_CHUNK_SIZE`   | 1,048,576 (1 MB) | Размер чанка памяти           |
| `MEMORY_CHUNK_MIN_MB` | 32               | Минимальный размер памяти     |
| `MIN_BLOCK_SIZE`      | 512              | Минимальный размер блока redo |
| `MEMORY_ALIGNMENT`    | 4,096            | Выравнивание памяти           |
| `COLUMN_LIMIT`        | 1,000            | Лимит колонок Oracle < 23     |
| `COLUMN_LIMIT_23_0`   | 4,096            | Лимит колонок Oracle 23+      |

### Reader

| Константа              | Значение | Описание                          |
|------------------------|----------|-----------------------------------|
| `PAGE_SIZE_MAX`        | 4,096    | Макс. размер страницы (заголовок) |
| `BAD_CDC_MAX_CNT`      | 20       | Макс. повторных проверок CRC      |
| `FLAGS_END`            | 0x0008   | End-of-redo stream                |
| `FLAGS_ASYNC`          | 0x0100   | Асинхронная передача арх. лога    |
| `FLAGS_NODATALOSS`     | 0x0200   | No data-loss mode                 |
| `FLAGS_RESYNC`         | 0x0800   | Режим ресинхронизации             |
| `FLAGS_CLOSEDTHREAD`   | 0x1000   | Закрытый поток архивирования      |
| `FLAGS_MAXPERFORMANCE` | 0x2000   | Max performance mode              |

### ReaderUdev

| Константа       | Значение        | Описание                                |
|-----------------|-----------------|-----------------------------------------|
| `MAX_READ_SIZE` | 4 * 1024 * 1024 | Макс. размер чтения (4 MB)              |
| `MAGIC_XOR`     | 0x000081a0      | XOR-магия для исправления заголовка ASM |

### Builder

| Константа          | Значение             | Описание                    |
|--------------------|----------------------|-----------------------------|
| `VALUE_BUFFER_MIN` | 1,048,576 (1 MB)     | Минимальный буфер значений  |
| `VALUE_BUFFER_MAX` | 4,294,967,296 (4 GB) | Максимальный буфер значений |

### Parser

| Константа            | Значение                    | Описание            |
|----------------------|-----------------------------|---------------------|
| `MAX_LWN_CHUNKS`     | 1024 / MEMORY_CHUNK_SIZE_MB | Макс. чанков LWN    |
| `MAX_RECORDS_IN_LWN` | 1,048,576                   | Макс. записей в LWN |

## Конфигурационные параметры по умолчанию

| Параметр               | По умолчанию | Описание                         |
|------------------------|--------------|----------------------------------|
| `checkpointIntervalS`  | 600          | Интервал чекпоинтов (секунды)    |
| `checkpointIntervalMb` | 500          | Интервал чекпоинтов (МБ)         |
| `checkpointKeep`       | 100          | Количество хранимых чекпоинтов   |
| `redoReadSleepUs`      | 50,000       | Задержка чтения redo (мкс)       |
| `redoVerifyDelayUs`    | 0            | Задержка верификации redo (мкс)  |
| `archReadSleepUs`      | 10,000,000   | Задержка чтения арх. логов (мкс) |
| `refreshIntervalUs`    | 10,000,000   | Интервал обновления (мкс)        |
| `pollIntervalUs`       | 100,000      | Интервал опроса writer (мкс)     |
| `queueSize`            | 65,536       | Размер очереди writer            |
| `archReadTries`        | 10           | Попыток чтения арх. логов        |
| `logLevel`             | INFO         | Уровень логирования              |
| `stopLogSwitches`      | 0            | Остановить после N log switches  |
| `stopCheckpoints`      | 0            | Остановить после N checkpoint    |
| `stopTransactions`     | 0            | Остановить после N транзакций    |

## Иерархия исключений

```mermaid
graph TB
    subgraph Exceptions["⚠️ Исключения OLR"]
        Boot["🚀 BootException<br/>Ошибки загрузки<br/>10001-10005"]
        Config["⚙️ ConfigurationException<br/>Ошибки конфигурации<br/>20001-30001"]
        Data["💾 DataException<br/>Ошибки данных<br/>20001+"]
        Network["🌐 NetworkException<br/>Сетевые ошибки<br/>10053-10066"]
        RedoLog["📄 RedoLogException<br/>Ошибки redo log<br/>50005-50061"]
        Runtime["⚡ RuntimeException<br/>Ошибки времени выполнения<br/>10001+, 50016"]
    end

    classDef exceptions fill:#ffebee,stroke:#c62828,stroke-width:2px
    classDef boot fill:#fff3cd,stroke:#f57f17,stroke-width:2px
    classDef config fill:#90caf9,stroke:#1565c0,stroke-width:2px
    classDef data fill:#c8e6c9,stroke:#2e7d32,stroke-width:2px
    classDef network fill:#b39ddb,stroke:#4527a0,stroke-width:2px
    classDef redolog fill:#ffcc80,stroke:#ef6c00,stroke-width:2px
    classDef runtime fill:#ef9a9a,stroke:#c62828,stroke-width:2px

    class Exceptions exceptions
    class Boot boot
    class Config config
    class Data data
    class Network network
    class RedoLog redolog
    class Runtime runtime
```

## Типы данных Oracle (typedef)

| Тип              | Базовый тип | Описание              |
|------------------|-------------|-----------------------|
| `typeResetlogs`  | uint32_t    | Resetlogs ID          |
| `typeActivation` | uint32_t    | Activation ID         |
| `typeDbId`       | uint32_t    | Database ID           |
| `typeConId`      | int16_t     | Container ID          |
| `typeUba`        | uint64_t    | Undo Block Address    |
| `typeUsn`        | int16_t     | Undo Segment Number   |
| `typeSlt`        | uint16_t    | Slot (транзакции)     |
| `typeSqn`        | uint32_t    | Sequence (транзакции) |
| `typeAfn`        | uint16_t    | Absolute File Number  |
| `typeDba`        | uint32_t    | Data Block Address    |
| `typeObj`        | uint32_t    | Object ID             |
| `typeDataObj`    | uint32_t    | Data Object ID        |
| `typeCol`        | int16_t     | Column Number         |
| `typeTs`         | uint32_t    | Tablespace Number     |
| `typeUser`       | uint32_t    | User ID               |
| `XidMap`         | uint64_t    | Key мапы транзакций   |
| `time_ut`        | int64_t     | Время (мкс)           |
