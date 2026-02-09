# Changelog openlogreplicator-vk

Все изменения в форке относительно upstream (bersler/OpenLogReplicator).

## Формат версий

Форк использует схему: `MAJOR.MINOR.PATCH-{commits}-{hash}`

Например: `1.8.7-45-7d57cc1a`

---

## [Unreleased 1.9.0-x-xxxxxxx] - 2026-xx-xx

### ⚠️ Breaking Changes

- **timestamp-tm-val переименовано в timestamp-metadata**
  - В версиях форка `1.8.7-x` использовался параметр `timestamp-tm-val` для включения времени коммита транзакции в вывод
  - Начиная с `1.9.0-x` используется upstream-название `timestamp-metadata`
  - Функциональность идентична, требуется обновление конфигурации

### Not required in 1.9.0

- separate linking for `nnz` lib — в upstream 1.9.0 линковка `nnz` работает корректно, разделение на `nnz`/`nnz19` не требуется,
  как было сделано в `1.8.7-x`

### Добавлено (перенесено из 1.8.7-x)

- `docs`: описание pipeline of work как форка
- `feat(build)`: conditional compilation для SO_REUSEPORT
- **ASM (Automatic Storage Management) поддержка**
  - `feat(asm)`: ASM Replicator — чтение данных напрямую из ASM
  - `feat(reader)`: network asm reader для ASM (asm)

---

## [1.8.7-45-7d57cc1a] - 2026-03-23

### Добавлено

- **ASM (Automatic Storage Management) поддержка**
  - `feat(asm)`: ASM Replicator — чтение данных напрямую из ASM
  - `feat(reader)`: network asm reader для ASM (asm)
  - `feat(reader)`: direct block device reader для ASM (asm-udev)
  - `doc(asm)`: документация ASM reference manual
  - `doc(asm)`: inline code documentation для ASM классов

- **Archivelogs**
  - `feat(archivelogs)`: hybrid reading of archivelogs — гибридное чтение архивных логов

- **Форматирование вывода**
  - `feat`: параметр `timestamp-tm-val` для вывода времени коммита транзакции
  - `feat(thread)`: установка OS thread name для отладки

- **Скрипты и сборка**
  - `feat(scripts)`: OpenLogReplicator config scripts
  - `feat(scripts)`: conditional compilation для SO_REUSEPORT
  - `refactor(cmake)`: отдельное линкование для `nnz` библиотеки
  - `refactor(gitignore)`: добавлен `config.h` в gitignore

- **Документация**
  - `docs`: описание pipeline of work как форка
  - `docs`: cross-compile для Solaris SPARC64

### Исправлено

- **Solaris SPARC64 поддержка**
  - `fix(build)`: resolve Solaris SPARC64 compatibility issues
  - `fix(build)`: add struct keyword for utsname на Solaris SPARC64
  - `fix(network)`: handle big-endian byte order в Solaris network client
  - `fix(parser)`: correct x86/x86_64 transaction buffer alignment
