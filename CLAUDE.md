# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

This is a **fork** of OpenLogReplicator - an open-source Oracle CDC (Change Data Capture) solution written in C++17. It reads Oracle redo log files and streams changes in JSON or Protobuf format to targets (Kafka, file, network, ZeroMQ).

The fork adds:
- **ASM support** (`reader/ReaderASM.cpp`, `reader/ReaderASMBlockDevice.cpp`) - direct reading from Oracle ASM storage
- **Solaris SPARC64 cross-compilation** support
- **OS thread naming** for debugging (`pthread_setname_np`)
- **Hybrid archivelog reading** (ASM or filesystem)

Key files:
- `src/OpenLogReplicator.cpp` - main orchestrator, creates all components
- `src/main.cpp` - entry point, signal handling, config file parsing
- `OpenLogReplicator.json` - runtime configuration (sources, targets, filters, format)
- `scripts/` - SQL helpers (`grants.sql`, `gencfg.sql`, ASM scripts)

## Build System

### Dependencies

Required:
- CMake >= 3.16
- C++17 compiler (GCC 9+ or Clang)
- RapidJSON (submodule/symlink at `./rapidjson`)

Optional (enabled via cmake flags):
- Oracle Instant Client (`WITH_OCI`) - for online replication
- librdkafka (`WITH_RDKAFKA`) - Kafka writer
- Protobuf (`WITH_PROTOBUF`) - Protobuf output + StreamClient
- ZeroMQ (`WITH_ZEROMQ`) - requires Protobuf
- Prometheus C++ client (`WITH_PROMETHEUS`) - metrics endpoint

### Build Commands

**Debug build (with AddressSanitizer):**
```bash
mkdir -p cmake-build-debug && cd cmake-build-debug
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWITH_RAPIDJSON=../rapidjson \
  -DWITH_OCI=/path/to/instantclient \
  -DWITH_RDKAFKA=/path/to/librdkafka \
  -DWITH_PROTOBUF=/path/to/protobuf \
  -DWITH_PROMETHEUS=/path/to/prometheus-cpp \
  -DWITH_ZEROMQ=/path/to/zeromq
make -j$(nproc)
```

**Release build:**
```bash
mkdir -p cmake-build-release && cd cmake-build-release
cmake .. -DCMAKE_BUILD_TYPE=Release <same flags as above>
make -j$(nproc)
```

**Key cmake variables:**
- `WITH_STATIC=ON` - static linking of Kafka, Protobuf
- `CPU_ARCH=native` - target architecture optimization
- `THREAD_INFO=ON` - enable thread info logging

### Docker Build

#### External development

Docker build files for external contributors are maintained in the public
[`tarantool/openlogreplicator-docker`](https://github.com/tarantool/openlogreplicator-docker)
repository. Check it out next to this repository. The Makefile defaults produce
the corresponding local build command:

```bash
make help
```

The public defaults use `../openlogreplicator-docker`,
`ghcr.io/tarantool/openlogreplicator-base:latest`, and the local image tag
`openlogreplicator-test:local`.

#### Internal Tarantool development

Internal builds use the GitLab project
`tarantool/cdc/v9/openlogreplicator-docker-vk`, normally checked out as the
sibling directory `../openlogreplicator-docker-vk`. Override the Makefile
variables to select the internal base image and local tag:

```bash
make help \
  OLR_DOCKER_REPO=../openlogreplicator-docker \
  OLR_BASE_IMAGE=<internal-registry>/<base-image>:<tag> \
  OLR_TEST_IMAGE=olr-test:local
```

The internal Docker repository provides `build-dev.sh` for debug images and
`build-prod.sh` for release images. Both scripts check out this repository as a
submodule and perform the build inside Docker.

## Architecture

### Component Model

OpenLogReplicator uses a multi-threaded pipeline architecture. The main components are created in `OpenLogReplicator::run()`:

```
Reader (ASM/Filesystem/ASMBlockDevice)  -->  Parser  -->  Builder (JSON/Protobuf)  -->  Writer (Kafka/File/Stream/Discard)
                                      ^
                                      |
Replicator (Online/Batch)  -->  Metadata  -->  State (Disk)
```

**Ctx** (`common/Ctx.h`) - global context, memory manager, logging, configuration flags. Single instance passed to all components.

**Replicator** (`replicator/Replicator.h`) - main thread managing the replication lifecycle. Subclasses:
- `ReplicatorOnline` - connects to live Oracle instance via OCI
- `ReplicatorOnlineASM` - online replication with ASM storage
- `ReplicatorBatch` - offline/archived log processing

**Reader** (`reader/Reader.h`) - reads redo log blocks. Implementations:
- `ReaderFilesystem` - local filesystem redo logs
- `ReaderASM` - reads from ASM via Oracle OCI connection
- `ReaderASMBlockDevice` - direct block device access for ASM disks

**Parser** (`parser/Parser.h`) - parses Oracle redo log records (opcodes), reconstructs transactions. Core of CDC logic. Uses `TransactionBuffer` for managing in-flight transactions.

**Builder** (`builder/Builder.h`) - formats output. Implementations:
- `BuilderJson` - JSON output
- `BuilderProtobuf` - Protobuf output (requires `WITH_PROTOBUF`)

**Writer** (`writer/Writer.h`) - outputs to targets. Implementations:
- `WriterKafka` - Apache Kafka producer
- `WriterFile` - flat file output
- `WriterStream` - TCP network stream (Protobuf)
- `WriterDiscard` - null sink for testing

**Metadata** (`metadata/Metadata.h`) - manages Oracle schema snapshots, table/column mappings, checkpoint state. Uses `Schema` for in-memory catalog and `SerializerJson` for persistence.

**State** (`state/StateDisk.h`) - persists checkpoint state to disk (SCN, sequence, offsets).

### Memory Management

`MemoryManager` (`common/MemoryManager.h`) provides chunked allocation. Memory is tracked by category (BUILDER, PARSER, READER, TRANSACTIONS, WRITER, MISC). The `Ctx` class owns the global memory manager.

### Key Data Types

Custom types in `common/types/`:
- `Scn` - System Change Number (Oracle's logical clock)
- `Seq` - Redo log sequence number
- `Xid` - Transaction ID
- `Time` - Oracle timestamp
- `RowId` - Oracle ROWID
- `LobId` - LOB identifier

### Parser Opcodes

Oracle redo log opcodes are implemented in `src/parser/OpCode*.h` files. Each opcode handles a specific redo record type (e.g., `OpCode0501` = insert, `OpCode0502` = delete). The `Parser` class dispatches to these handlers.

## Configuration

Runtime config is a JSON file passed via `-f` flag. Key sections:
- `source` - Oracle connection, reader type (asm/filesystem), format, filter
- `target` - output destinations with writer type
- `state` - checkpoint persistence (disk path)
- `metrics` - Prometheus bind address

Example configs in `scripts/OpenLogReplicator-example-*.json`.

### Table-name case sensitivity

OLR matches `filter.table` (`owner`/`table`) as a **case-sensitive** regex
against the Oracle data dictionary, which stores unquoted identifiers in
**UPPERCASE** — so filters must be uppercase. OLR emits schema/table uppercase
and the `db` segment verbatim from `source[].name`. When OLR feeds a Debezium
Oracle connector, the `db` segment must use the **same case** in
`table.include.list`, `signal.data.collection`, and signal `data-collections`,
or signals are silently dropped and incremental snapshots fail with
`Schema not found`.

## Testing

Regression tests are available under `tests/` and run inside the Docker test
image. See [`tests/README.md`](tests/README.md) for prerequisites and image build
instructions. To run the complete suite for a supported Oracle environment:

```bash
make test ORACLE_TARGET=xe-21
# or
make test ORACLE_TARGET=free-23
```

## Code Style

- `.clang-tidy` excludes `src/common/OraProtoBuf.pb.*` (generated files)
- Debug builds enable extensive sanitizers (AddressSanitizer, UBSan)
- Release builds use `-O3` with strict warning flags (`-Werror` equivalent set)
