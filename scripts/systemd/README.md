# OpenLogReplicator под systemd

Запуск OLR как systemd-сервиса. **Один и тот же шаблонный юнит
[`openlogreplicator@.service`](openlogreplicator@.service) работает для обоих
режимов чтения redo** — ASM (`asm-udev`) и файловая система (`online`).
Отличаются только JSON-конфиг и права сервисного пользователя, **не сам юнит**.

Проверено на двух CentOS 7-машинах:
- ASM-источник (Oracle 19c non-CDB, reader `asm-udev`);
- filesystem-источник (Oracle 19c CDB/PDB, reader `online`).

## ASM (`asm-udev`) vs Filesystem (`online`) — что различается

| | **ASM** | **Filesystem** |
|---|---|---|
| Где redo-логи | в ASM (`+DATA/.../ONLINELOG/...`) | файлы на ФС (`/opt/oracle/oradata/...`, FRA) |
| `reader.type` в конфиге | `asm-udev` | `online` |
| ASM-креды в конфиге | да (`user-asm`/`password-asm`/`server-asm` → `+ASM`) | нет |
| Как OLR читает redo | открывает **блок-устройства** ASM (`/dev/...`, `O_DIRECT`) | открывает **redo-файлы** напрямую |
| Доступ сервисного юзера | к дискам ASM → группа `asmadmin`/`oinstall`/`disk` | к redo-файлам → группа `oinstall`/`dba` |
| Sandbox-ограничения | **нельзя** `PrivateDevices=true`, `ProtectSystem=strict` (спрячут `/dev/*`) | `/dev` не нужен, можно строже |
| Сам systemd-юнит | **одинаковый** | **одинаковый** |

`reader` в JSON — единственное принципиальное отличие конфига:

```jsonc
// ASM
"reader": {
  "type": "asm-udev",
  "user": "...", "password": "...", "server": "//localhost:1521/ORCL",
  "user-asm": "sys", "password-asm": "...", "server-asm": "//localhost:1521/+ASM"
}
// Filesystem
"reader": {
  "type": "online",
  "user": "C##DBZUSER", "password": "...", "server": "//localhost:1521/ORCLPDB"
}
```
Остальное (формат, фильтр, writer, state) — идентично.

---

## Установка

```bash
# 1. Сервисный пользователь и группа.
getent passwd olr >/dev/null || \
    sudo useradd --system --home-dir /opt/OpenLogReplicator --shell /sbin/nologin olr
# проверить: id olr

# 2. Права на записываемые каталоги.
sudo chown -R olr:olr \
    /opt/OpenLogReplicator/output \
    /opt/OpenLogReplicator/checkpoint \
    /opt/OpenLogReplicator/state \
    /opt/OpenLogReplicator/log

# 3. Установить unit.
sudo cp openlogreplicator@.service /etc/systemd/system/
sudo systemctl daemon-reload

# 4. (опционально) проверка
sudo systemd-analyze verify openlogreplicator@olr.service
```

> Если пользователь `olr` уже есть в системе (заведён заранее администратором) —
> шаг 1 пропусти; `getent passwd olr` это проверит и не пересоздаст его.

Доступ сервисного юзера к redo (зависит от режима):
```bash
# ASM: доступ к блок-устройствам дисков
sudo -u olr head -c1 /dev/<asm-disk> >/dev/null && echo OK || echo "NO access → usermod -aG asmadmin olr"
# Filesystem: доступ к redo-файлам
sudo -u olr head -c1 /opt/oracle/oradata/ORCL/onlinelog/<redo>.log >/dev/null && echo OK || echo "NO access → usermod -aG oinstall olr"
```

## Запуск и управление

```bash
sudo systemctl enable --now openlogreplicator@olr   # старт + автозапуск при загрузке
sudo systemctl status openlogreplicator@olr
journalctl -u openlogreplicator@olr -f

sudo systemctl stop    openlogreplicator@olr        # graceful (SIGINT)
sudo systemctl restart openlogreplicator@olr
```

---

## Проверка, что изменения подхватываются

```bash
tail -f /opt/OpenLogReplicator/output/output-*.json
```
В другом окне терминала DML+COMMIT в отслеживаемой схеме (для CDB не забудь
`ALTER SESSION SET CONTAINER=<PDB>`):
```sql
INSERT INTO USR1.PERSON (id, name, surname, gender, birth_date, age, metadata, description, enabled)
VALUES (5000001,'Ivan','Petrov','M',DATE '1990-05-17',35,HEXTORAW('DEADBEEF'),'cdc test',1);
COMMIT;
UPDATE USR1.PERSON SET surname='Sidorov' WHERE id=5000001;
COMMIT;
DELETE FROM USR1.PERSON WHERE id=5000001;
COMMIT;
```
В `output-*.json` появятся `"op":"c"`, `"op":"u"`, `"op":"d"` с `"table":"PERSON"`.
Записи `"op":"chkpt"` — служебные отметки прогресса (`flags: 4096`).
