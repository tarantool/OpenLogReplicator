-- interleaved-transactions.sql: Multiple transactions interleaved in redo log.
-- Uses autonomous transactions to create genuine redo interleaving within
-- a single sqlplus session. Tests OLR's transaction correlation and XID tracking.
-- Run as PDB user (e.g., olr_test/olr_test@//localhost:1521/FREEPDB1)
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
    FROM user_tables WHERE table_name = 'TEST_INTERLEAVE';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_INTERLEAVE PURGE';
    END IF;
END;
/

CREATE TABLE TEST_INTERLEAVE (
    id     NUMBER PRIMARY KEY,
    source VARCHAR2(20),
    val    NUMBER
);

ALTER TABLE TEST_INTERLEAVE ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- Interleaved pattern: main transaction and autonomous transactions
-- create genuine redo interleaving
DECLARE
    PROCEDURE auto_insert(p_id NUMBER, p_source VARCHAR2, p_val NUMBER) IS
        PRAGMA AUTONOMOUS_TRANSACTION;
    BEGIN
        INSERT INTO TEST_INTERLEAVE VALUES (p_id, p_source, p_val);
        COMMIT;
    END;

    PROCEDURE auto_update(p_id NUMBER, p_val NUMBER) IS
        PRAGMA AUTONOMOUS_TRANSACTION;
    BEGIN
        UPDATE TEST_INTERLEAVE SET val = p_val WHERE id = p_id;
        COMMIT;
    END;

    PROCEDURE auto_delete(p_id NUMBER) IS
        PRAGMA AUTONOMOUS_TRANSACTION;
    BEGIN
        DELETE FROM TEST_INTERLEAVE WHERE id = p_id;
        COMMIT;
    END;
BEGIN
    -- Main txn: insert id=1
    INSERT INTO TEST_INTERLEAVE VALUES (1, 'main', 100);

    -- Auto txn 1: insert id=2 + commit (interleaves with main)
    auto_insert(2, 'auto1', 200);

    -- Main txn: insert id=3 (still uncommitted)
    INSERT INTO TEST_INTERLEAVE VALUES (3, 'main', 300);

    -- Auto txn 2: insert id=4 + update id=2 + commit
    auto_insert(4, 'auto2', 400);
    auto_update(2, 250);

    -- Main txn: update id=1, then commit
    UPDATE TEST_INTERLEAVE SET val = 150 WHERE id = 1;
    COMMIT;
END;
/

-- Second wave: another interleaved pattern
DECLARE
    PROCEDURE auto_ops IS
        PRAGMA AUTONOMOUS_TRANSACTION;
    BEGIN
        INSERT INTO TEST_INTERLEAVE VALUES (5, 'auto3', 500);
        UPDATE TEST_INTERLEAVE SET source = 'auto3-upd' WHERE id = 5;
        COMMIT;
    END;
BEGIN
    -- Main txn: update
    UPDATE TEST_INTERLEAVE SET val = 350 WHERE id = 3;

    -- Auto txn 3: insert + update in same auto txn
    auto_ops;

    -- Main txn: delete + commit
    DELETE FROM TEST_INTERLEAVE WHERE id = 4;
    COMMIT;
END;
/

-- Record end SCN
DECLARE
    v_end_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_end_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_END: ' || v_end_scn);
END;
/

EXIT
