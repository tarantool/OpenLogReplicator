-- large-transaction.sql: Test large transaction with many rows in a single commit.
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
    FROM user_tables WHERE table_name = 'TEST_BULK';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_BULK PURGE';
    END IF;
END;
/

CREATE TABLE TEST_BULK (
    id      NUMBER PRIMARY KEY,
    payload VARCHAR2(200),
    val     NUMBER
);

ALTER TABLE TEST_BULK ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- Transaction 1: bulk insert 200 rows in a single commit
BEGIN
    FOR i IN 1..200 LOOP
        INSERT INTO TEST_BULK VALUES (i, 'row_' || LPAD(i, 4, '0'), i * 10);
    END LOOP;
    COMMIT;
END;
/

-- Transaction 2: bulk update all rows
UPDATE TEST_BULK SET val = val + 1;
COMMIT;

-- Transaction 3: bulk delete half the rows
DELETE FROM TEST_BULK WHERE MOD(id, 2) = 0;
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
