# Changelog openlogreplicator-vk

Все изменения в форке относительно upstream (bersler/OpenLogReplicator).

## Формат версий

Форк использует схему: `MAJOR.MINOR.PATCH-{commits}-{hash}`

Например: `1.8.7-45-7d57cc1a`

---

## [Unreleased 1.9.0-x-xxxxxxx] - 2026-xx-xx

### Адаптация из 1.8.7-x

#### ⚠️ Breaking Changes

- **Форматирование вывода**
  - **timestamp-tm-val переименовано в timestamp-metadata**
    - В версиях форка `1.8.7-x` использовался параметр `timestamp-tm-val` для включения времени коммита транзакции в вывод
    - Начиная с `1.9.0-x` используется upstream-название `timestamp-metadata`
    - Функциональность идентична, требуется обновление конфигурации

#### Не требуется в 1.9.0 (не перенесено из 1.8.7-x)

- **Скрипты и сборка**
  - `feat(scripts)`: OpenLogReplicator config scripts — в scripts папке и так лежит достаточно примеров запуска
  - `refactor(cmake)`: отдельное линкование для `nnz` библиотеки — в upstream 1.9.0 линковка `nnz` работает корректно, разделение на `nnz`/`nnz19` не требуется,
    как было сделано в `1.8.7-x`

#### Перенесено/адаптировано из 1.8.7-x

- **Документация**
  - `docs`: описание pipeline of work как форка
  - `docs`: cross-compile для Solaris SPARC64

- **ASM (Automatic Storage Management) поддержка**
  - `feat(asm)`: ASM Replicator — чтение данных напрямую из ASM
  - `feat(reader)`: network asm reader для ASM (asm)
  - `feat(reader)`: direct block device reader для ASM (asm-udev)
  - `doc(asm)`: документация по параметрам ASM
  - `doc(asm)`: документация ASM классов
  - `feat(archivelogs)`: настройка гибридного чтения архивных логов, возможность чтения архивных логов на ASM или из ФС

- **Форматирование вывода**
  - `feat(thread)`: установка OS thread name для отладки

- **Скрипты и сборка**
  - `feat(scripts)`: conditional compilation для SO_REUSEPORT
  - `refactor(gitignore)`: создан .gitignore

- **Solaris SPARC64 поддержка**
  - `fix(build)`: resolve Solaris SPARC64 compatibility issues
  - `fix(network)`: handle big-endian byte order in Solaris network client
  - `fix(parser)`: correct transaction buffer alignment

---

## [1.8.7-45-7d57cc1a] - 2026-03-23

### Добавлено

- **Документация**
  - `docs`: описание pipeline of work как форка
  - `docs`: cross-compile для Solaris SPARC64

- **ASM (Automatic Storage Management) поддержка**
  - `feat(asm)`: ASM Replicator — чтение данных напрямую из ASM
  - `feat(reader)`: network asm reader для ASM (asm)
  - `feat(reader)`: direct block device reader для ASM (asm-udev)
  - `doc(asm)`: документация по параметрам ASM
  - `doc(asm)`: документация ASM классов
  - `feat(archivelogs)`: настройка гибридного чтения архивных логов, возможность чтения архивных логов на ASM или из ФС

- **Форматирование вывода**
  - `feat`: параметр `timestamp-tm-val` для вывода времени коммита транзакции
  - `feat(thread)`: установка OS thread name для отладки

- **Скрипты и сборка**
  - `feat(scripts)`: OpenLogReplicator config scripts
  - `feat(scripts)`: conditional compilation для SO_REUSEPORT
  - `refactor(cmake)`: отдельное линкование для `nnz` библиотеки
  - `refactor(gitignore)`: создан .gitignore

### Исправлено

- **Solaris SPARC64 поддержка**
  - `fix(build)`: resolve Solaris SPARC64 compatibility issues
  - `fix(network)`: handle big-endian byte order in Solaris network client
  - `fix(parser)`: correct transaction buffer alignment
