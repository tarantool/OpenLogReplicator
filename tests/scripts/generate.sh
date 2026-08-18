#!/usr/bin/env bash
# generate.sh — Generate + validate one OLR regression test fixture.
#
# Usage: ./generate.sh <scenario-name>
# Example: ./generate.sh basic-crud
#
# Runs SQL against a local Oracle Free container (gvenzl/oracle-free),
# captures redo logs, generates schema, runs LogMiner + OLR, and compares.
#
# Prerequisites:
#   - Containers running: make -C tests/1-environments/$ORACLE_TARGET up
#
# Environment variables:
#   ORACLE_TARGET    — Oracle environment name (default: free-23)
#   ORACLE_CONTAINER — Docker container name (default: oracle)
#   ORACLE_PASSWORD  — SYS/SYSTEM password (default: oracle)
#   DB_CONN          — sqlplus connect string for test user
#                      (default: olr_test/olr_test@//localhost:1521/FREEPDB1)
#   SCHEMA_OWNER     — Schema owner for LogMiner filter (default: OLR_TEST)
#   PDB_NAME         — PDB service name (default: FREEPDB1)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TESTS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$TESTS_DIR/.." && pwd)"

# OLR rejects a config whose version differs from the schema version it was built with, so read
# it from the build instead of pinning it here.
SCHEMA_VERSION="$(sed -n 's/^set(SCHEMA_VERSION "\(.*\)")$/\1/p' "$PROJECT_ROOT/CMakeLists.txt")"
if [[ -z "$SCHEMA_VERSION" ]]; then
    echo "generate.sh: cannot read SCHEMA_VERSION from $PROJECT_ROOT/CMakeLists.txt" >&2
    exit 1
fi

# Oracle target environment (default: free-23)
ORACLE_TARGET="${ORACLE_TARGET:-free-23}"
ENV_DIR="$TESTS_DIR/1-environments/$ORACLE_TARGET"
if [[ ! -d "$ENV_DIR" ]]; then
    echo "ERROR: Environment directory not found: $ENV_DIR" >&2
    exit 1
fi

# Per-target defaults. Derived from ORACLE_TARGET unless the caller already
# set PDB_NAME / DB_CONN explicitly. Critical: previous default ('FREEPDB1' for
# all targets) silently sent xe-21 runs into CDB$ROOT, so SYS.USER$ couldn't
# find OLR_TEST and the resulting schema JSON was missing the "users" entry,
# which OLR rejects with "20007 OLR_TEST is missing".
case "$ORACLE_TARGET" in
    xe-21)
        DEFAULT_PDB="XEPDB1"
        ;;
    free-23)
        DEFAULT_PDB="FREEPDB1"
        ;;
    *)
        echo "ERROR: unsupported ORACLE_TARGET=$ORACLE_TARGET" >&2
        exit 1
        ;;
esac
PDB_NAME="${PDB_NAME:-$DEFAULT_PDB}"
DB_CONN="${DB_CONN:-olr_test/olr_test@//localhost:1521/$PDB_NAME}"
ORACLE_CONTAINER="${ORACLE_CONTAINER:-oracle}"
ORACLE_PASSWORD="${ORACLE_PASSWORD:-oracle}"
SCHEMA_OWNER="${SCHEMA_OWNER:-OLR_TEST}"
# This script is designed to run inside the olr-dev image (which has the
# OpenLogReplicator binary, python3, the test scripts and the docker CLI baked
# in). In CI the olr-dev image IS the GitLab job image; locally you'd
# `docker run --rm -it -v /var/run/docker.sock:/var/run/docker.sock olr-dev:latest bash`.
#
# Both Oracle interactions and the OLR run happen from this single shell:
#   - sqlplus + file ops against the oracle container → docker exec / docker cp;
#   - OLR itself → /opt/OpenLogReplicator/OpenLogReplicator directly.
#
# -L on sqlplus: no retry / interactive prompt on auth failure (otherwise
# sqlplus blocks forever waiting for stdin when invoked through `docker exec -T`).
run_sysdba() {
    docker exec "$ORACLE_CONTAINER" sqlplus -L -S / as sysdba @"$1"
}
run_user() {
    docker exec "$ORACLE_CONTAINER" sqlplus -L -S "$DB_CONN" @"$1"
}
copy_in() {
    docker cp "$1" "${ORACLE_CONTAINER}:$2"
}
copy_out() {
    docker cp "${ORACLE_CONTAINER}:$1" "$2"
}

SCENARIO="${1:?Usage: $0 <scenario-name>}"
SCENARIO_SQL="$TESTS_DIR/0-inputs/${SCENARIO}.sql"

if [[ ! -f "$SCENARIO_SQL" ]]; then
    echo "ERROR: Scenario file not found: $SCENARIO_SQL" >&2
    echo "Available scenarios:" >&2
    ls "$TESTS_DIR/0-inputs/"*.sql 2>/dev/null | sed 's/.*\//  /' | sed 's/\.sql$//' >&2
    exit 1
fi

# Working directory for this run (under tests/.work/ so it's visible inside the olr container)
mkdir -p "$TESTS_DIR/.work"
WORK_DIR=$(mktemp -d "$TESTS_DIR/.work/${ORACLE_TARGET}_${SCENARIO}_XXXXXX")
trap 'rm -rf "$WORK_DIR"' EXIT

# Check for DDL marker — switches to DICT_FROM_REDO_LOGS mode
DDL_MODE=0
if grep -q '^-- @DDL' "$SCENARIO_SQL" 2>/dev/null; then
    DDL_MODE=1
fi

# Check for @MID_SWITCH markers
MID_SWITCH_COUNT=$(grep -c '^-- @MID_SWITCH' "$SCENARIO_SQL" 2>/dev/null || true)

# Generated fixture name encodes scenario + environment
FIXTURE_NAME="${SCENARIO}-${ORACLE_TARGET}"

echo "=== Fixture generation: $SCENARIO (target: $ORACLE_TARGET) ==="
echo "  Container: $ORACLE_CONTAINER"
echo "  Fixture name: $FIXTURE_NAME"
echo "  Work dir: $WORK_DIR"
if [[ "$DDL_MODE" -eq 1 ]]; then
    echo "  Mode: DDL (DICT_FROM_REDO_LOGS)"
fi
echo ""

# ---- Stage 0 (DDL only): Build LogMiner dictionary into redo logs ----
if [[ "$DDL_MODE" -eq 1 ]]; then
    echo "--- Stage 0: Building LogMiner dictionary into redo logs ---"
    cat > "$WORK_DIR/build_dict.sql" <<'DICTSQL'
SET SERVEROUTPUT ON FEEDBACK OFF
BEGIN
    DBMS_LOGMNR_D.BUILD(OPTIONS => DBMS_LOGMNR_D.STORE_IN_REDO_LOGS);
    DBMS_OUTPUT.PUT_LINE('Dictionary built OK');
END;
/
ALTER SYSTEM SWITCH LOGFILE;
BEGIN DBMS_SESSION.SLEEP(2); END;
/
EXIT
DICTSQL
    copy_in "$WORK_DIR/build_dict.sql" /tmp/build_dict.sql
    DICT_OUTPUT=$(run_sysdba /tmp/build_dict.sql)
    echo "  $DICT_OUTPUT"

    # Record the SCN where dictionary starts
    cat > "$WORK_DIR/dict_scn.sql" <<'DICTSCN'
SET HEADING OFF FEEDBACK OFF PAGESIZE 0
SELECT MIN(first_change#) FROM v$archived_log
WHERE dictionary_begin = 'YES' AND deleted = 'NO' AND name IS NOT NULL
  AND first_change# = (SELECT MAX(first_change#) FROM v$archived_log
                        WHERE dictionary_begin = 'YES' AND deleted = 'NO');
EXIT
DICTSCN
    copy_in "$WORK_DIR/dict_scn.sql" /tmp/dict_scn.sql
    DICT_START_SCN=$(run_sysdba /tmp/dict_scn.sql | tr -d '[:space:]')
    echo "  Dictionary start SCN: $DICT_START_SCN"
    echo ""
fi

# ---- Stage 1: Run SQL scenario ----
echo "--- Stage 1: Running SQL scenario ---"
copy_in "$SCENARIO_SQL" /tmp/scenario.sql

if [[ "$MID_SWITCH_COUNT" -gt 0 ]]; then
    echo "  Detected $MID_SWITCH_COUNT @MID_SWITCH marker(s) — running DML in background"
    run_user /tmp/scenario.sql > "$WORK_DIR/dml_output.txt" 2>&1 &
    DML_PID=$!
    for i in $(seq 1 "$MID_SWITCH_COUNT"); do
        sleep 8
        echo "  Triggering mid-execution log switch #$i"
        cat > "$WORK_DIR/mid_switch.sql" <<'MIDSQL'
SET FEEDBACK OFF
ALTER SYSTEM SWITCH LOGFILE;
EXIT
MIDSQL
        copy_in "$WORK_DIR/mid_switch.sql" /tmp/mid_switch.sql
        run_sysdba /tmp/mid_switch.sql > /dev/null
    done
    wait "$DML_PID" || true
    SCENARIO_OUTPUT=$(cat "$WORK_DIR/dml_output.txt")
else
    SCENARIO_OUTPUT=$(run_user /tmp/scenario.sql)
fi
echo "$SCENARIO_OUTPUT"

# Parse SCN range from output
START_SCN=$(echo "$SCENARIO_OUTPUT" | grep 'FIXTURE_SCN_START:' | head -1 | sed 's/.*FIXTURE_SCN_START:\s*//' | tr -d '[:space:]')
if [[ -z "$START_SCN" ]]; then
    echo "ERROR: Could not find FIXTURE_SCN_START in scenario output" >&2
    exit 1
fi

# Force log switches
echo "  Forcing log switches..."
cat > "$WORK_DIR/log_switch.sql" <<'LOGSQL'
SET FEEDBACK OFF
ALTER SYSTEM SWITCH LOGFILE;
ALTER SYSTEM SWITCH LOGFILE;
BEGIN DBMS_SESSION.SLEEP(3); END;
/
EXIT
LOGSQL
copy_in "$WORK_DIR/log_switch.sql" /tmp/log_switch.sql
run_sysdba /tmp/log_switch.sql > /dev/null

# Get end SCN
cat > "$WORK_DIR/get_scn.sql" <<'SCNSQL'
SET HEADING OFF FEEDBACK OFF PAGESIZE 0
SELECT current_scn FROM v$database;
EXIT
SCNSQL
copy_in "$WORK_DIR/get_scn.sql" /tmp/get_scn.sql
END_SCN=$(run_sysdba /tmp/get_scn.sql | tr -d '[:space:]')

echo "  SCN range: $START_SCN - $END_SCN"

# ---- Stage 2: Capture archived redo logs ----
echo ""
echo "--- Stage 2: Capturing archived redo logs ---"
REDO_DIR="$TESTS_DIR/3-generated/redo/$FIXTURE_NAME"
rm -rf "$REDO_DIR"
mkdir -p "$REDO_DIR"

# Query log_archive_format from Oracle
cat > "$WORK_DIR/get_archfmt.sql" <<'FMTSQL'
SET HEADING OFF FEEDBACK OFF PAGESIZE 0 LINESIZE 200
SELECT value FROM v$parameter WHERE name='log_archive_format';
EXIT
FMTSQL
copy_in "$WORK_DIR/get_archfmt.sql" /tmp/get_archfmt.sql
LOG_ARCHIVE_FORMAT=$(run_sysdba /tmp/get_archfmt.sql | tr -d '[:space:]')
echo "  Oracle log_archive_format: $LOG_ARCHIVE_FORMAT"

# Query archive files with thread#/sequence# to detect filename prefixes
cat > "$WORK_DIR/find_archives.sql" <<SQL
SET HEADING OFF FEEDBACK OFF PAGESIZE 0 LINESIZE 1000
-- status='A' = Available (successfully written). Without this filter Oracle
-- can return rows for destinations where the write failed (e.g. permission
-- problems on /shared/archives), and the subsequent cp will fail with a
-- misleading "No such file or directory".
SELECT name || '|' || thread# || '|' || sequence# || '|' || resetlogs_id
FROM v\$archived_log
WHERE first_change# <= $END_SCN
  AND next_change# >= $START_SCN
  AND deleted = 'NO'
  AND status = 'A'
  AND name IS NOT NULL
ORDER BY thread#, sequence#;
EXIT
SQL

copy_in "$WORK_DIR/find_archives.sql" /tmp/find_archives.sql
ARCHIVE_LIST=$(run_sysdba /tmp/find_archives.sql)

if [[ -z "$ARCHIVE_LIST" ]]; then
    echo "ERROR: No archive logs found for SCN range" >&2
    exit 1
fi

# Detect actual filename prefix by comparing first archive with expected format
# Oracle may add prefixes (e.g., "arch") not reflected in log_archive_format
FIRST_LINE=$(echo "$ARCHIVE_LIST" | head -1 | tr -d '[:space:]')
FIRST_PATH=$(echo "$FIRST_LINE" | cut -d'|' -f1)
FIRST_THREAD=$(echo "$FIRST_LINE" | cut -d'|' -f2)
FIRST_SEQ=$(echo "$FIRST_LINE" | cut -d'|' -f3)
FIRST_RESETLOGS=$(echo "$FIRST_LINE" | cut -d'|' -f4)
FIRST_FNAME=$(basename "$FIRST_PATH")

# Construct what the format would produce for this file
EXPECTED_FNAME=$(echo "$LOG_ARCHIVE_FORMAT" | sed "s/%t/$FIRST_THREAD/;s/%s/$FIRST_SEQ/;s/%r/$FIRST_RESETLOGS/;s/%S/$(printf '%09d' "$FIRST_SEQ")/")
# Derive prefix: strip expected from actual
ARCHIVE_PREFIX="${FIRST_FNAME%%$EXPECTED_FNAME}"
if [[ -n "$ARCHIVE_PREFIX" ]]; then
    echo "  Detected archive filename prefix: '$ARCHIVE_PREFIX'"
    LOG_ARCHIVE_FORMAT="${ARCHIVE_PREFIX}${LOG_ARCHIVE_FORMAT}"
fi
echo "  Effective log-archive-format: $LOG_ARCHIVE_FORMAT"

echo "$ARCHIVE_LIST" | while read -r line; do
    line=$(echo "$line" | tr -d '[:space:]')
    [[ -z "$line" ]] && continue
    arclog=$(echo "$line" | cut -d'|' -f1)
    fname=$(basename "$arclog")
    echo "  Copying: $arclog"
    copy_out "$arclog" "$REDO_DIR/$fname"
done
chmod -R a+r "$REDO_DIR"  # Oracle archives are 640; make readable for OLR container
echo "  Redo logs saved to: $REDO_DIR"

# ---- Stage 3: Generate schema file ----
echo ""
echo "--- Stage 3: Schema generation ---"
SCHEMA_DIR="$TESTS_DIR/3-generated/schema/$FIXTURE_NAME"
rm -rf "$SCHEMA_DIR"
mkdir -p "$SCHEMA_DIR"

# Patch gencfg.sql with test parameters
cp "$PROJECT_ROOT/scripts/gencfg.sql" "$WORK_DIR/gencfg.sql" && rm -f "$WORK_DIR/gencfg.sql.bak"

# Patch: name, users, SCN
sed -i.bak "s/v_NAME := 'DB'/v_NAME := 'TEST'/" "$WORK_DIR/gencfg.sql" && rm -f "$WORK_DIR/gencfg.sql.bak"
sed -i.bak "s/v_USERNAME_LIST := VARCHAR2TABLE('USR1', 'USR2')/v_USERNAME_LIST := VARCHAR2TABLE('$SCHEMA_OWNER')/" "$WORK_DIR/gencfg.sql" && rm -f "$WORK_DIR/gencfg.sql.bak"
sed -i.bak "s/SELECT CURRENT_SCN INTO v_SCN FROM SYS.V_\\\$DATABASE/-- SELECT CURRENT_SCN INTO v_SCN FROM SYS.V_\$DATABASE/" "$WORK_DIR/gencfg.sql" && rm -f "$WORK_DIR/gencfg.sql.bak"
sed -i.bak "s/-- v_SCN := 12345678/v_SCN := $START_SCN/" "$WORK_DIR/gencfg.sql" && rm -f "$WORK_DIR/gencfg.sql.bak"

# Add PDB session switch and settings at the beginning
echo "SET FEEDBACK OFF" > "$WORK_DIR/gencfg.tmp"
echo "ALTER SESSION SET CONTAINER=$PDB_NAME;" >> "$WORK_DIR/gencfg.tmp"
cat "$WORK_DIR/gencfg.sql" >> "$WORK_DIR/gencfg.tmp" && mv "$WORK_DIR/gencfg.tmp" "$WORK_DIR/gencfg.sql"

# Add EXIT at end
echo "EXIT;" >> "$WORK_DIR/gencfg.sql" && rm -f "$WORK_DIR/gencfg.sql.bak"

copy_in "$WORK_DIR/gencfg.sql" /tmp/gencfg.sql

echo "  Running gencfg.sql..."
GENCFG_OUTPUT=$(run_sysdba /tmp/gencfg.sql 2>&1)

# Extract JSON content (starts with {"database":)
SCHEMA_FILE="$SCHEMA_DIR/TEST-chkpt-${START_SCN}.json"
echo "$GENCFG_OUTPUT" | sed -n '/^{"database"/,$p' > "$SCHEMA_FILE"

if [[ ! -s "$SCHEMA_FILE" ]]; then
    echo "ERROR: gencfg.sql produced no JSON output" >&2
    echo "Output was:" >&2
    echo "$GENCFG_OUTPUT" >&2
    exit 1
fi

# Fix seq to 0 for batch mode
python3 -c "
import json, sys
with open('$SCHEMA_FILE') as f:
    data = json.load(f)
data['seq'] = 0
with open('$SCHEMA_FILE', 'w') as f:
    json.dump(data, f, separators=(',', ':'))
"
echo "  Schema file: $SCHEMA_FILE ($(wc -c < "$SCHEMA_FILE") bytes)"

# ---- Stage 4: Run LogMiner extraction ----
echo ""
echo "--- Stage 4: Running LogMiner extraction ---"

if [[ "$DDL_MODE" -eq 1 ]]; then
    LM_ARCHIVE_FILTER="first_change# <= $END_SCN AND next_change# >= $DICT_START_SCN"
    LM_OPTIONS="DBMS_LOGMNR.DICT_FROM_REDO_LOGS + DBMS_LOGMNR.DDL_DICT_TRACKING + DBMS_LOGMNR.NO_ROWID_IN_STMT + DBMS_LOGMNR.COMMITTED_DATA_ONLY"
    LM_MODE_DESC="DICT_FROM_REDO_LOGS + DDL_DICT_TRACKING"
else
    LM_ARCHIVE_FILTER="first_change# <= $END_SCN AND next_change# >= $START_SCN"
    LM_OPTIONS="DBMS_LOGMNR.DICT_FROM_ONLINE_CATALOG + DBMS_LOGMNR.NO_ROWID_IN_STMT + DBMS_LOGMNR.COMMITTED_DATA_ONLY"
    LM_MODE_DESC="DICT_FROM_ONLINE_CATALOG"
fi
echo "  LogMiner mode: $LM_MODE_DESC"

cat > "$WORK_DIR/logminer_run.sql" <<SQL
SET SERVEROUTPUT ON SIZE UNLIMITED
SET LINESIZE 32767
SET LONG 100000
SET LONGCHUNKSIZE 100000
SET PAGESIZE 0
SET TRIMSPOOL ON
SET TRIMOUT ON
SET FEEDBACK OFF
SET ECHO OFF
SET HEADING OFF
SET VERIFY OFF

DECLARE
    v_count NUMBER := 0;
BEGIN
    FOR rec IN (
        SELECT name FROM v\$archived_log
        WHERE $LM_ARCHIVE_FILTER
          AND deleted = 'NO'
          AND status = 'A'
          AND name IS NOT NULL
        ORDER BY sequence#
    ) LOOP
        DBMS_LOGMNR.ADD_LOGFILE(
            logfilename => rec.name,
            options     => CASE WHEN v_count = 0
                                THEN DBMS_LOGMNR.NEW
                                ELSE DBMS_LOGMNR.ADDFILE
                           END
        );
        v_count := v_count + 1;
        DBMS_OUTPUT.PUT_LINE('Added log: ' || rec.name);
    END LOOP;

    IF v_count = 0 THEN
        DBMS_OUTPUT.PUT_LINE('ERROR: No archive logs found for SCN range');
        RETURN;
    END IF;

    DBMS_OUTPUT.PUT_LINE('Starting LogMiner with ' || v_count || ' log file(s)');

    DBMS_LOGMNR.START_LOGMNR(
        startScn => $START_SCN,
        endScn   => $END_SCN,
        options  => $LM_OPTIONS
    );
END;
/

SPOOL /tmp/logminer_out.lst

SELECT TO_CLOB(scn || '|' || operation || '|' || seg_owner || '|' || table_name || '|' || xid || '|') ||
       REPLACE(REPLACE(sql_redo, CHR(10), ' '), CHR(13), '') || '|' ||
       REPLACE(REPLACE(NVL(sql_undo, ''), CHR(10), ' '), CHR(13), '')
FROM v\$logmnr_contents
WHERE seg_owner = UPPER('$SCHEMA_OWNER')
  AND operation IN ('INSERT', 'UPDATE', 'DELETE')
ORDER BY scn, xid, sequence#;

SPOOL OFF

BEGIN
    DBMS_LOGMNR.END_LOGMNR;
END;
/

EXIT
SQL

copy_in "$WORK_DIR/logminer_run.sql" /tmp/logminer_run.sql

echo "  Running LogMiner..."
LM_OUTPUT=$(run_sysdba /tmp/logminer_run.sql)
echo "$LM_OUTPUT" | head -20 || true

copy_out /tmp/logminer_out.lst "$WORK_DIR/logminer_raw.lst"

python3 "$SCRIPT_DIR/logminer2json.py" "$WORK_DIR/logminer_raw.lst" "$WORK_DIR/logminer.json"
LM_COUNT=$(wc -l < "$WORK_DIR/logminer.json")
echo "  LogMiner records: $LM_COUNT"

# ---- Stage 5: Run OLR in batch mode (via olr dev container) ----
echo ""
echo "--- Stage 5: Running OLR ---"

# Backup schema file before OLR (OLR modifies the schema dir with checkpoints)
cp "$SCHEMA_FILE" "$WORK_DIR/schema_backup.json"

# OLR runs in the same filesystem as this script (we ARE the olr image),
# so absolute local paths go directly into the config — no container/host mapping.
REDO_FILES_JSON=""
for f in "$REDO_DIR"/*; do
    [[ -f "$f" ]] || continue
    fname=$(basename "$f")
    if [[ -n "$REDO_FILES_JSON" ]]; then
        REDO_FILES_JSON="$REDO_FILES_JSON, "
    fi
    REDO_FILES_JSON="$REDO_FILES_JSON\"$REDO_DIR/$fname\""
done

OLR_OUTPUT="$WORK_DIR/olr_output.json"

cat > "$WORK_DIR/olr_config.json" <<EOF
{
  "version": "$SCHEMA_VERSION",
  "log-level": 3,
  "memory": {
    "min-mb": 32,
    "max-mb": 256
  },
  "state": {
    "type": "disk",
    "path": "$SCHEMA_DIR"
  },
  "source": [
    {
      "alias": "S1",
      "name": "TEST",
      "reader": {
        "type": "batch",
        "redo-log": [$REDO_FILES_JSON],
        "log-archive-format": "$LOG_ARCHIVE_FORMAT",
        "start-scn": $START_SCN
      },
      "format": {
        "type": "json",
        "scn": 1,
        "timestamp": 7,
        "timestamp-metadata": 7,
        "xid": 1
      },
      "filter": {
        "table": [
          {"owner": "$SCHEMA_OWNER", "table": ".*"}
        ]
      }
    }
  ],
  "target": [
    {
      "alias": "T1",
      "source": "S1",
      "writer": {
        "type": "file",
        "output": "$OLR_OUTPUT",
        "new-line": 1,
        "append": 1
      }
    }
  ]
}
EOF

echo "  Running OLR..."
if ! /opt/OpenLogReplicator/OpenLogReplicator -r -f "$WORK_DIR/olr_config.json" \
    > "$WORK_DIR/olr_stdout.log" 2>&1; then
    echo "ERROR: OLR exited with non-zero status" >&2
    cat "$WORK_DIR/olr_stdout.log" >&2
    exit 1
fi

if [[ ! -f "$OLR_OUTPUT" ]]; then
    echo "ERROR: OLR did not produce output file" >&2
    cat "$WORK_DIR/olr_stdout.log" >&2
    exit 1
fi

OLR_LINES=$(wc -l < "$OLR_OUTPUT")
echo "  OLR output lines: $OLR_LINES"

# Clean up runtime checkpoint files and restore original schema
rm -f "$SCHEMA_DIR"/TEST-chkpt.json "$SCHEMA_DIR"/TEST-chkpt-*.json
cp "$WORK_DIR/schema_backup.json" "$SCHEMA_FILE"

# ---- Stage 6: Compare ----
echo ""
echo "--- Stage 6: Comparing LogMiner vs OLR ---"
if python3 "$SCRIPT_DIR/compare.py" "$WORK_DIR/logminer.json" "$OLR_OUTPUT"; then
    COMPARE_RESULT=0
else
    COMPARE_RESULT=1
fi

# ---- Stage 7: Save golden file ----
echo ""
if [[ $COMPARE_RESULT -eq 0 ]]; then
    echo "--- Stage 7: Saving golden file ---"
    EXPECTED_DIR="$TESTS_DIR/3-generated/expected/$FIXTURE_NAME"
    mkdir -p "$EXPECTED_DIR"
    cp "$OLR_OUTPUT" "$EXPECTED_DIR/output.json"
    echo "  Golden file saved: $EXPECTED_DIR/output.json"

    cp "$WORK_DIR/logminer.json" "$EXPECTED_DIR/logminer-reference.json"
    echo "  LogMiner reference saved: $EXPECTED_DIR/logminer-reference.json"
    echo ""
    echo "=== PASS: Fixture '$SCENARIO' generated successfully ==="
else
    echo "--- Stage 7: SKIPPED (comparison failed) ---"
    echo ""
    echo "=== FAIL: Fixture '$SCENARIO' comparison failed ==="
    echo "  LogMiner JSON: $WORK_DIR/logminer.json"
    echo "  OLR output:    $OLR_OUTPUT"
    echo "  OLR log:       $WORK_DIR/olr_stdout.log"
    echo ""
    echo "Debug: inspect the files above, then re-run after fixing."
    trap - EXIT  # preserve work dir for debugging
    exit 1
fi
