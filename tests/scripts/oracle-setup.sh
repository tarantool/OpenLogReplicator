#!/usr/bin/env bash
# oracle-setup.sh — apply OLR test setup to a running oracle container.
#
# Idempotent: re-running on an already-configured DB is safe (ALTER DATABASE
# ARCHIVELOG / OPEN are no-ops on second invocation; grants overwrite cleanly).
#
# Runs from the CI job (or local dev shell) via `docker exec` against the
# already-up oracle container. We do NOT use docker-compose bind-mount init
# scripts because they don't propagate reliably across the DinD daemon/job
# filesystem boundary in our k8s runner setup.
#
# Usage: oracle-setup.sh <xe-21|free-23>
set -euo pipefail

TARGET="${1:?Usage: $0 <xe-21|free-23>}"
CONTAINER="${ORACLE_CONTAINER:-oracle}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GRANTS_TEMPLATE="$(cd "$SCRIPT_DIR/../.." && pwd)/scripts/grants.sql"
APP_USER="${APP_USER:-olr_test}"

case "$TARGET" in
    xe-21)   PDB="XEPDB1" ;;
    free-23) PDB="FREEPDB1" ;;
    *)       echo "ERROR: unsupported ORACLE_TARGET=$TARGET" >&2; exit 1 ;;
esac

if [ ! -f "$GRANTS_TEMPLATE" ]; then
    echo "ERROR: grants.sql not found at $GRANTS_TEMPLATE" >&2
    exit 1
fi

echo "[OLR setup] Applying OLR setup to $CONTAINER (PDB=$PDB, user=$APP_USER)..."

# Stream the full setup (archivelog enable + open PDBs + grants) into sqlplus
# via stdin. Single sqlplus session keeps state across SHUTDOWN/STARTUP.
GRANTS_SQL=$(sed "s/<USER>/${APP_USER}/g" "$GRANTS_TEMPLATE")

docker exec -i "$CONTAINER" sqlplus -L -S / as sysdba <<SQL
WHENEVER SQLERROR EXIT SQL.SQLCODE;
SET ECHO ON
SET FEEDBACK ON

-- 1. Archivelog mode (CDB-level, needs MOUNT state).
SHUTDOWN IMMEDIATE;
STARTUP MOUNT;
ALTER DATABASE ARCHIVELOG;
ALTER DATABASE OPEN;

-- 2. PDBs come up MOUNTED after a CDB restart — explicitly open them and
--    SAVE STATE so they stay OPEN across future restarts.
ALTER PLUGGABLE DATABASE ALL OPEN;
ALTER PLUGGABLE DATABASE ALL SAVE STATE;

ALTER DATABASE ADD SUPPLEMENTAL LOG DATA;
ALTER SYSTEM SET db_recovery_file_dest_size=10G;

-- 3. Apply OLR's canonical grant set inside the PDB.
ALTER SESSION SET CONTAINER=$PDB;
$GRANTS_SQL

EXIT;
SQL

echo "[OLR setup] Waiting for oracle healthcheck to come back up..."
# After SHUTDOWN/STARTUP the container is still running but sqlplus connections
# may briefly fail. Poll until a trivial query succeeds.
for i in $(seq 1 60); do
    if docker exec "$CONTAINER" bash -c "echo 'SELECT 1 FROM dual;' | sqlplus -L -S ${APP_USER}/${APP_USER}@//localhost:1521/${PDB}" >/dev/null 2>&1; then
        echo "[OLR setup] Oracle ready ($i tries)"
        exit 0
    fi
    sleep 2
done

echo "ERROR: oracle didn't recover within 120s after setup" >&2
exit 1
