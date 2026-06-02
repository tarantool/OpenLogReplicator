-- basic-crud.sql: Simple INSERT/UPDATE/DELETE scenario for fixture generation.
-- Run as PDB user (e.g., olr_test/olr_test@//localhost:1521/FREEPDB1)
--
-- Outputs: FIXTURE_SCN_START: <scn> and FIXTURE_SCN_END: <scn>
-- The orchestrator script parses these and handles log switches separately
-- (ALTER SYSTEM SWITCH LOGFILE must run from CDB root, not PDB).

SET SERVEROUTPUT ON
SET FEEDBACK OFF
SET ECHO OFF

-- Setup: drop and recreate table
DECLARE
    v_table_exists NUMBER;
BEGIN
    SELECT COUNT(*) INTO v_table_exists
    FROM user_tables WHERE table_name = 'TEST_CDC';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_CDC PURGE';
    END IF;
END;
/

CREATE TABLE TEST_CDC (
    id   NUMBER PRIMARY KEY,
    name VARCHAR2(100),
    val  NUMBER
);

ALTER TABLE TEST_CDC ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- DML: INSERTs
INSERT INTO TEST_CDC VALUES (1, 'Alice', 100);
INSERT INTO TEST_CDC VALUES (2, 'Bob', 200);
INSERT INTO TEST_CDC VALUES (3, 'Charlie', 300);
COMMIT;

-- DML: UPDATE
UPDATE TEST_CDC SET val = 150, name = 'Alice Updated' WHERE id = 1;
COMMIT;

-- DML: DELETE
DELETE FROM TEST_CDC WHERE id = 2;
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
