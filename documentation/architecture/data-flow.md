# Потоки данных OpenLogReplicator

## Основной поток данных (end-to-end)

```mermaid
flowchart LR
    subgraph Sources["📥 Источники данных"]
        FS["💾 Файловая система<br/>Архивные / Online логи"]
        ASM["🗃️ Oracle ASM<br/>dbms_diskgroup"]
        ASMBlockDevice["🔌 Блочные устройства<br/>/dev/sdX + extent map"]
    end

    subgraph ReaderLayer["📖 Reader (Thread)"]
        R["Reader<br/>Чтение блоков redo log<br/>в кольцевой буфер"]
    end

    subgraph ParserLayer["🔍 Parser (в потоке Replicator)"]
        P["Parser<br/>Парсинг бинарных записей"]
        LWN["LWN (Log Write Number)<br/>Группировка и сортировка<br/>записей по SCN"]
        OC["OpCode обработчики<br/>0501: Undo 0504: Commit<br/>0B02: INSERT 0B04: UPDATE<br/>0B03: DELETE 0513: DDL"]
    end

    subgraph TxnLayer["💾 Транзакционный слой"]
        TB["TransactionBuffer<br/>Активные транзакции<br/>map Xid → Transaction"]
        TXN["Transaction<br/>Накопление redo записей<br/>до COMMIT/ROLLBACK"]
    end

    subgraph BuilderLayer["🏗️ Builder"]
        B["Builder<br/>Формирование выходного<br/>сообщения из транзакции"]
        BQ["BuilderQueue<br/>Очередь сообщений<br/>для Writer"]
    end

    subgraph WriterLayer["✍️ Writer (Thread)"]
        W["Writer<br/>Отправка сообщения"]
    end

    subgraph Outputs["📤 Выходы"]
        File["📄 Файл / stdout"]
        Kafka["🚀 Apache Kafka"]
        TCP["🌐 TCP стрим"]
        ZMQ["⚡ ZeroMQ"]
    end

    FS --> R
    ASM --> R
    ASMBlockDevice --> R
    R -->|" redoBufferList<br/>блоки по blockSize "| P
    P --> LWN
    LWN --> OC
    OC -->|appendToTransaction| TB
    TB --> TXN
    TXN -->|" flush() при COMMIT "| B
    B -->|BuilderMsg| BQ
    BQ -->|pollQueue| W
    W --> File
    W --> Kafka
    W --> TCP
    W --> ZMQ

    classDef sources fill:#fff3cd,stroke:#f57f17,stroke-width:2px
    classDef reader fill:#bbdefb,stroke:#1565c0,stroke-width:2px
    classDef parser fill:#c8e6c9,stroke:#2e7d32,stroke-width:2px
    classDef txn fill:#e1bee7,stroke:#6a1b9a,stroke-width:2px
    classDef builder fill:#ffe082,stroke:#ff8f00,stroke-width:2px
    classDef writer fill:#ffcc80,stroke:#ef6c00,stroke-width:2px
    classDef outputs fill:#b2dfdb,stroke:#00695c,stroke-width:2px

    class Sources,Sources sources
    class ReaderLayer,ReaderLayer reader
    class ParserLayer,ParserLayer parser
    class TxnLayer,TxnLayer txn
    class BuilderLayer,BuilderLayer builder
    class WriterLayer,WriterLayer writer
    class Outputs,Outputs outputs
```

## Детальный поток: Reader → Parser

```mermaid
sequenceDiagram
    participant Rep as Replicator (Thread)
    participant R as Reader (Thread)
    participant P as Parser
    Note over Rep, R: Инициализация
    Rep ->> R: readerCreate(group)
    Rep ->> R: initialize() - выделение redoBufferList
    Rep ->> R: ctx.spawnThread(reader)
    Note over Rep, R: Открытие файла
    Rep ->> R: checkRedoLog()
    R ->> R: status = CHECK
    R ->> R: redoOpen() virtual
    R -->> Rep: ret = OK / ERROR
    Note over Rep, R: Обновление заголовка
    Rep ->> R: updateRedoLog()
    R ->> R: status = UPDATE
    R ->> R: reloadHeader() — чтение 2 блоков заголовка
    R ->> R: извлечение: version, blockSize, firstScn, nextScn, thread...
    R -->> Rep: готово
    Note over Rep, R: Основной цикл чтения
    Rep ->> R: setStatusRead()
    loop Цикл чтения
        R ->> R: read1() — первичное чтение
        R ->> R: bufferAllocate() — выделение чанка
        R ->> R: redoRead(buf, offset, size) virtual
        R ->> R: checkBlockHeader() для каждого блока
        R ->> R: bufferEnd += goodBlocks * blockSize
        R -->> P: condParserSleeping.notify()
        P ->> R: getBufferStart и getBufferEnd
        P ->> P: парсинг redo records из буфера
        P ->> R: bufferFree(num) — освобождение чанка
        P ->> R: confirmReadData(start) — продвижение bufferStart
    end

    Note over R: EOF / Log switch
    R -->> Rep: ret = FINISHED / OVERWRITTEN
```

## Детальный поток: Parser → Builder → Writer

```mermaid
sequenceDiagram
    participant P as Parser
    participant TB as TransactionBuffer
    participant TXN as Transaction
    participant B as Builder
    participant BQ as BuilderQueue
    participant W as Writer (Thread)
    Note over P, TB: Обработка redo записи
    P ->> P: analyzeLwn() → OpCode::processXX()
    P ->> TB: findTransaction(xid)

    alt Новая транзакция (BEGIN)
        TB ->> TXN: new Transaction(xid)
        P ->> TXN: begin = true, beginScn, beginTimestamp
    else Существующая транзакция (DML)
        P ->> TXN: add(RedoLogRecord)
        TXN ->> TXN: добавление в TransactionChunk
    end

    alt LOB операция
        P ->> TXN: обновление LobCtx
    end

    Note over TXN, B: COMMIT — формирование сообщения
    P ->> TXN: flush()
    TXN ->> B: processBegin() — заголовок транзакции
    loop Каждая DML запись
        TXN ->> B: processInsert/Update/Delete()
        B ->> B: форматирование колонок (JSON/Protobuf)
    end
    TXN ->> B: processCommit()
    B ->> BQ: enqueue(BuilderMsg)
    TXN ->> TXN: purge() — очистка
    Note over BQ, W: Writer забирает сообщение
    W ->> BQ: pollQueue()
    W ->> W: sendMessage(BuilderMsg)
    W ->> W: confirmedScn обновление

    alt ROLLBACK
        P ->> TXN: rollbackLastOp() / purge()
    end
```

## Кольцевой буфер Reader

Reader и Parser работают через кольцевой буфер — набор чанков по 1MB. Reader заполняет чанки данными из redo log, Parser забирает обработанные. Когда буфер полон — Reader ждёт. Когда пуст — Parser ждёт.

```mermaid
flowchart LR
    subgraph ReaderSide["📖 Reader (Thread)"]
        RProc["Чтение redo log<br/>read1() → redoRead()"]
    end

    subgraph Buffer["🔄 Кольцевой буфер redoBufferList"]
        direction LR
        C0["Chunk 0<br/>1MB<br/>✅ обработан"]
        C1["Chunk 1<br/>1MB<br/>✅ обработан"]
        C2["Chunk 2<br/>1MB<br/>✅ обработан"]
        C3["Chunk 3<br/>1MB<br/>📝 читается"]
        C4["Chunk 4<br/>1MB<br/>⬜ свободен"]
        C5["Chunk 5<br/>1MB<br/>⬜ свободен"]
    end

    subgraph ParserSide["🔍 Parser (в потоке Replicator)"]
        PProc["Парсинг redo записей<br/>analyzeLwn() → OpCode"]
    end

    RProc -->|"записывает<br/>bufferAllocate() + redoRead()"| C4
    C0 -->|"bufferFree() + confirmReadData()"| PProc

    BS["bufferStart"] -.- C0
    BE["bufferEnd"] -.- C3

    classDef done fill:#c8e6c9,stroke:#2e7d32,stroke-width:2px
    classDef reading fill:#ffe082,stroke:#ff8f00,stroke-width:2px
    classDef free fill:#e0e0e0,stroke:#9e9e9e,stroke-width:2px

    class C0,C1,C2 done
    class C3 reading
    class C4,C5 free
    class CondR,CondP,CondF sync
    class Reader_proc,Parser_proc proc
```

**Указатели:**
- `bufferStart` — позиция в redo log, до которой Parser уже обработал все данные. Parser двигает вперёд через `confirmReadData()`
- `bufferEnd` — позиция, до которой Reader уже прочитал данные из файла. Reader двигает вперёд после каждого `read1()`
- `bufferSizeMax` = memoryChunksReadBufferMax × 1MB — общий размер буфера

**Синхронизация (один mutex):**
- `condReaderSleeping` — Replicator/Parser будит Reader, когда нужна новая команда
- `condParserSleeping` — Reader будит Parser, когда записал новые данные
- `condBufferFull` — Parser будит Reader, когда освободил чанк (буфер был полон)

**Жизненный цикл чанка:**
1. ⬜ **Свободен** — чанк в пуле, Reader может запросить через `bufferAllocate()`
2. 📝 **Читается** — Reader вызвал `redoRead()`, заполняет данными, затем `bufferEnd += goodBlocks`
3. ✅ **Готов** — данные записаны, Parser может читать из этого чанка
4. 🗑️ **Освобождён** — Parser вызвал `bufferFree()`, чанк вернулся в пул

## Схема памяти транзакций

```mermaid
flowchart TB
    subgraph TxnBuf["💾 TransactionBuffer"]
        XidMap["unordered_map&lt;XidMap, Transaction*&gt;<br/>XID → Transaction"]
        OrphanLobs["map&lt;LobKey, uint8_t*&gt;<br/>Сиротские LOB данные"]
    end

    subgraph Txn["📋 Transaction"]
        Xid["Xid (USN + SLT + SQN)"]
        Chunks["TransactionChunk*<br/>Цепочка чанков памяти"]
        LobCtx_["LobCtx<br/>Контекст LOB"]
        XmlCtx_["XmlCtx*<br/>Контекст XML"]
        Attrs["AttributeMap<br/>username, program и др."]
        Flags["Флаги: begin/rollback/<br/>system/schema/shutdown"]
    end

    subgraph MM["🔄 MemoryManager (Thread)"]
        Swap["Swap: запись неактивных<br/>транзакций на диск"]
        Unswap["Unswap: загрузка обратно<br/>при COMMIT"]
    end

    XidMap --> Txn
    Chunks -->|" 1MB чанки из пула "| Ctx_["Ctx::getMemoryChunk()"]
    Txn -->|" неактивная "| Swap
    Swap -->|" COMMIT "| Unswap

    classDef txnbuf fill:#bbdefb,stroke:#1565c0,stroke-width:2px
    classDef txn fill:#c8e6c9,stroke:#2e7d32,stroke-width:2px
    classDef mm fill:#ffe082,stroke:#ff8f00,stroke-width:2px
    classDef ctx fill:#e1bee7,stroke:#6a1b9a,stroke-width:2px

    class TxnBuf,TxnBuf txnbuf
    class Txn,Txn txn
    class MM,MM mm
    class Ctx_ ctx
```

## Форматы вывода Builder

```mermaid
flowchart LR
    subgraph Input["📥 Вход: Transaction"]
        Begin["BEGIN"]
        DML["INSERT / UPDATE / DELETE"]
        DDL["DDL"]
        Commit["COMMIT"]
    end

    subgraph BuilderJson_["🏗️ BuilderJson"]
        JMsg["JSON сообщение:<br/>{scn, tm, xid, op,<br/>schema, after, before}"]
    end

    subgraph BuilderProtobuf_["🏗️ BuilderProtobuf"]
        PMsg["Protobuf сообщение:<br/>pb::RedoResponse<br/>(Op, Schema, Payload, Value)"]
    end

    Begin --> JMsg
    DML --> JMsg
    Commit --> JMsg
    Begin --> PMsg
    DML --> PMsg
    Commit --> PMsg
    JMsg -->|BuilderQueue| WJ["✍️ WriterFile / WriterKafka"]
    PMsg -->|BuilderQueue| WS["✍️ WriterStream"]

    classDef input fill:#fff3cd,stroke:#f57f17,stroke-width:2px
    classDef json fill:#c8e6c9,stroke:#2e7d32,stroke-width:2px
    classDef proto fill:#bbdefb,stroke:#1565c0,stroke-width:2px
    classDef writer fill:#e1bee7,stroke:#6a1b9a,stroke-width:2px

    class Input,Input input
    class BuilderJson_,BuilderJson_ json
    class BuilderProtobuf_,BuilderProtobuf_ proto
    class WJ,WS writer
```

## Протокол WriterStream (клиент-сервер)

```mermaid
sequenceDiagram
    participant C as StreamClient
    participant S as WriterStream
    C ->> S: RequestCode::INFO
    S -->> C: ResponseCode::READY
    C ->> S: RequestCode::START (scn, database)
    S -->> C: ResponseCode::STARTING
    S -->> C: ResponseCode::REPLICATE (payload)

    loop Цикл репликации
        S -->> C: ResponseCode::PAYLOAD (данные)
        C ->> C: обработка данных
        C ->> S: RequestCode::CONFIRM (confirmedScn)
        C ->> S: RequestCode::CONTINUE
    end

    Note over C, S: При ошибке
    S -->> C: ResponseCode::FAILED_START
    S -->> C: ResponseCode::INVALID_DATABASE
    S -->> C: ResponseCode::INVALID_COMMAND
```
