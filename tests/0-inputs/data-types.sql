-- data-types.sql: Test various Oracle column types.
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
    FROM user_tables WHERE table_name = 'TEST_TYPES';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_TYPES PURGE';
    END IF;
END;
/

CREATE TABLE TEST_TYPES (
    id             NUMBER PRIMARY KEY,
    col_varchar2   VARCHAR2(200),
    col_char       CHAR(20),
    col_nvarchar2  NVARCHAR2(200),
    col_number     NUMBER(15,2),
    col_float      BINARY_FLOAT,
    col_double     BINARY_DOUBLE,
    col_date       DATE,
    col_timestamp  TIMESTAMP(6),
    col_raw        RAW(100),
    col_integer    INTEGER
);

ALTER TABLE TEST_TYPES ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- DML: INSERTs with various types
INSERT INTO TEST_TYPES VALUES (
    1,
    'hello world',
    'fixed               ',
    N'unicode text',
    12345.67,
    3.14,
    2.718281828459045,
    TO_DATE('2025-06-15 10:30:00', 'YYYY-MM-DD HH24:MI:SS'),
    TO_TIMESTAMP('2025-06-15 10:30:00.123456', 'YYYY-MM-DD HH24:MI:SS.FF6'),
    HEXTORAW('DEADBEEF'),
    42
);
INSERT INTO TEST_TYPES VALUES (
    2,
    'second row',
    'row2                ',
    N'more text',
    -999.99,
    -1.5,
    1.0e100,
    TO_DATE('2000-01-01 00:00:00', 'YYYY-MM-DD HH24:MI:SS'),
    TO_TIMESTAMP('2000-01-01 00:00:00.000000', 'YYYY-MM-DD HH24:MI:SS.FF6'),
    HEXTORAW('CAFEBABE'),
    0
);
COMMIT;

-- DML: UPDATE various type columns
UPDATE TEST_TYPES SET
    col_varchar2  = 'updated text',
    col_float     = 9.99,
    col_date      = TO_DATE('2026-01-01 12:00:00', 'YYYY-MM-DD HH24:MI:SS'),
    col_raw       = HEXTORAW('00FF00FF')
WHERE id = 1;
COMMIT;

-- DML: DELETE
DELETE FROM TEST_TYPES WHERE id = 2;
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
