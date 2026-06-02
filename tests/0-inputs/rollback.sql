-- rollback.sql: Test that rolled-back transactions are excluded from CDC output.
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
    FROM user_tables WHERE table_name = 'TEST_ROLLBACK';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_ROLLBACK PURGE';
    END IF;
END;
/

CREATE TABLE TEST_ROLLBACK (
    id   NUMBER PRIMARY KEY,
    name VARCHAR2(100),
    val  NUMBER
);

ALTER TABLE TEST_ROLLBACK ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- Transaction 1: committed (should appear)
INSERT INTO TEST_ROLLBACK VALUES (1, 'committed row 1', 100);
INSERT INTO TEST_ROLLBACK VALUES (2, 'committed row 2', 200);
COMMIT;

-- Transaction 2: rolled back (should NOT appear)
INSERT INTO TEST_ROLLBACK VALUES (3, 'this will be rolled back', 300);
UPDATE TEST_ROLLBACK SET val = 999 WHERE id = 1;
ROLLBACK;

-- Transaction 3: committed (should appear, row 1 still has val=100)
INSERT INTO TEST_ROLLBACK VALUES (4, 'after rollback', 400);
COMMIT;

-- Transaction 4: partial rollback via savepoint
SAVEPOINT sp1;
INSERT INTO TEST_ROLLBACK VALUES (5, 'before savepoint', 500);
SAVEPOINT sp2;
INSERT INTO TEST_ROLLBACK VALUES (6, 'after savepoint - will rollback', 600);
ROLLBACK TO sp2;
INSERT INTO TEST_ROLLBACK VALUES (7, 'after partial rollback', 700);
COMMIT;

-- Transaction 5: committed delete (should appear)
DELETE FROM TEST_ROLLBACK WHERE id = 2;
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
