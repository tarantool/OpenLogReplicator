-- long-spanning-txn.sql: Test transaction spanning multiple archive logs.
-- Verifies OLR correctly reassembles a transaction whose redo records
-- are split across archive log boundaries via forced log switches.
-- Run as PDB user (e.g., olr_test/olr_test@//localhost:1521/FREEPDB1)
--
-- @MID_SWITCH markers tell generate.sh to trigger log switches during
-- the DBMS_SESSION.SLEEP() pauses (the SQL runs in background).
--
-- Outputs: FIXTURE_SCN_START: <scn> and FIXTURE_SCN_END: <scn>

SET SERVEROUTPUT ON
SET FEEDBACK OFF
SET ECHO OFF

-- Setup: drop and recreate table
DECLARE
    v_table_exists NUMBER;
BEGIN
    SELECT COUNT(*) INTO v_table_exists
    FROM user_tables WHERE table_name = 'TEST_SPANNING';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_SPANNING PURGE';
    END IF;
END;
/

CREATE TABLE TEST_SPANNING (
    id      NUMBER PRIMARY KEY,
    payload VARCHAR2(200),
    val     NUMBER
);

ALTER TABLE TEST_SPANNING ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- Transaction 1: spans an archive log boundary.
-- INSERTs go to archive log N, then log switch, then more DML + COMMIT in log N+1.
INSERT INTO TEST_SPANNING VALUES (1, 'before-switch-1', 100);
INSERT INTO TEST_SPANNING VALUES (2, 'before-switch-2', 200);
INSERT INTO TEST_SPANNING VALUES (3, 'before-switch-3', 300);

-- @MID_SWITCH
-- Pause to let generate.sh trigger a log switch while this txn is open
BEGIN DBMS_SESSION.SLEEP(15); END;
/

-- Continue the same (uncommitted) transaction after the log switch
INSERT INTO TEST_SPANNING VALUES (4, 'after-switch-1', 400);
INSERT INTO TEST_SPANNING VALUES (5, 'after-switch-2', 500);
UPDATE TEST_SPANNING SET val = 150 WHERE id = 1;
DELETE FROM TEST_SPANNING WHERE id = 3;
COMMIT;

-- Transaction 2: normal (non-spanning) transaction after the gap
INSERT INTO TEST_SPANNING VALUES (6, 'post-span', 600);
UPDATE TEST_SPANNING SET payload = 'post-updated' WHERE id = 2;
DELETE FROM TEST_SPANNING WHERE id = 5;
COMMIT;

-- Record end SCN
DECLARE
    v_end_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_end_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_END: ' || v_end_scn);
END;
/

EXIT
