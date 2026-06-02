-- null-handling.sql: Test NULL value handling in CDC output.
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
    FROM user_tables WHERE table_name = 'TEST_NULLS';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_NULLS PURGE';
    END IF;
END;
/

CREATE TABLE TEST_NULLS (
    id    NUMBER PRIMARY KEY,
    col_a VARCHAR2(100),
    col_b NUMBER,
    col_c DATE
);

ALTER TABLE TEST_NULLS ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- DML: INSERT with NULLs in different columns
INSERT INTO TEST_NULLS VALUES (1, 'has value', 100, TO_DATE('2025-01-01', 'YYYY-MM-DD'));
INSERT INTO TEST_NULLS VALUES (2, NULL, NULL, NULL);
INSERT INTO TEST_NULLS VALUES (3, 'partial', NULL, TO_DATE('2025-06-15', 'YYYY-MM-DD'));
COMMIT;

-- DML: UPDATE value -> NULL
UPDATE TEST_NULLS SET col_a = NULL, col_b = NULL WHERE id = 1;
COMMIT;

-- DML: UPDATE NULL -> value
UPDATE TEST_NULLS SET col_a = 'now has value', col_b = 999 WHERE id = 2;
COMMIT;

-- DML: UPDATE NULL -> NULL (no actual change on nullable cols, but PK triggers log)
UPDATE TEST_NULLS SET col_c = NULL WHERE id = 2;
COMMIT;

-- DML: DELETE row with NULLs
DELETE FROM TEST_NULLS WHERE id = 3;
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
