-- number-precision.sql: Test NUMBER type edge cases and precision limits.
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
    FROM user_tables WHERE table_name = 'TEST_NUMBERS';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_NUMBERS PURGE';
    END IF;
END;
/

CREATE TABLE TEST_NUMBERS (
    id             NUMBER PRIMARY KEY,
    col_number     NUMBER,
    col_precise    NUMBER(38,0),
    col_decimal    NUMBER(20,10),
    col_small      NUMBER(5,2),
    col_integer    INTEGER,
    col_float      BINARY_FLOAT,
    col_double     BINARY_DOUBLE
);

ALTER TABLE TEST_NUMBERS ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- DML: Basic number values
INSERT INTO TEST_NUMBERS VALUES (1, 0, 0, 0, 0, 0, 0, 0);
INSERT INTO TEST_NUMBERS VALUES (2, 1, 1, 1, 1, 1, 1, 1);
INSERT INTO TEST_NUMBERS VALUES (3, -1, -1, -1, -1.00, -1, -1, -1);
COMMIT;

-- DML: Maximum precision NUMBER(38,0)
INSERT INTO TEST_NUMBERS VALUES (4,
    99999999999999999999999999999999999999,
    99999999999999999999999999999999999999,
    9999999999.9999999999,
    999.99,
    2147483647,
    256.0,
    65536.125
);
COMMIT;

-- DML: Negative large numbers
INSERT INTO TEST_NUMBERS VALUES (5,
    -99999999999999999999999999999999999999,
    -99999999999999999999999999999999999999,
    -9999999999.9999999999,
    -999.99,
    -2147483648,
    -256.0,
    -65536.125
);
COMMIT;

-- DML: Very small decimal values
INSERT INTO TEST_NUMBERS VALUES (6,
    0.000000000000000000000000000000000001,
    0,
    0.0000000001,
    0.01,
    0,
    0.5,
    0.0625
);
COMMIT;

-- DML: Fractional values with many decimal places
INSERT INTO TEST_NUMBERS VALUES (7,
    3.14159265358979323846264338327950288,
    3,
    3.1415926536,
    3.14,
    3,
    3.14159265,
    3.14159265358979323846
);
COMMIT;

-- DML: UPDATE to different precision values
UPDATE TEST_NUMBERS SET
    col_number  = 12345678901234567890,
    col_precise = 12345678901234567890123456789012345678,
    col_decimal = 1234567890.1234567890,
    col_small   = 123.45
WHERE id = 1;
COMMIT;

-- DML: UPDATE to zero from large
UPDATE TEST_NUMBERS SET
    col_number  = 0,
    col_precise = 0,
    col_decimal = 0,
    col_small   = 0
WHERE id = 4;
COMMIT;

-- DML: DELETE
DELETE FROM TEST_NUMBERS WHERE id = 3;
DELETE FROM TEST_NUMBERS WHERE id = 5;
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
