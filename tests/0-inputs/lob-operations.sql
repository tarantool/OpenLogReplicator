-- lob-operations.sql: Test CLOB and BLOB column handling.
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
    FROM user_tables WHERE table_name = 'TEST_LOBS';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_LOBS PURGE';
    END IF;
END;
/

CREATE TABLE TEST_LOBS (
    id         NUMBER PRIMARY KEY,
    col_clob   CLOB,
    col_blob   BLOB,
    col_label  VARCHAR2(50)
);

ALTER TABLE TEST_LOBS ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- DML: INSERT small inline LOBs (stored in-row)
INSERT INTO TEST_LOBS VALUES (1, 'Short CLOB text', HEXTORAW('AABBCCDD'), 'small-inline');
INSERT INTO TEST_LOBS VALUES (2, 'Another small CLOB', HEXTORAW('0102030405'), 'small-inline-2');
COMMIT;

-- DML: INSERT with NULL LOBs
INSERT INTO TEST_LOBS VALUES (3, NULL, NULL, 'null-lobs');
COMMIT;

-- DML: INSERT medium CLOB (> 4000 bytes, forces out-of-row storage)
DECLARE
    v_clob CLOB;
BEGIN
    v_clob := RPAD('Medium CLOB content. ', 8000, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ ');
    INSERT INTO TEST_LOBS VALUES (4, v_clob, HEXTORAW(RPAD('FF', 200, 'EE')), 'medium-lob');
    COMMIT;
END;
/

-- DML: UPDATE CLOB value
UPDATE TEST_LOBS SET col_clob = 'Updated CLOB text', col_label = 'updated' WHERE id = 1;
COMMIT;

-- DML: UPDATE set LOB to NULL
UPDATE TEST_LOBS SET col_clob = NULL, col_blob = NULL WHERE id = 2;
COMMIT;

-- DML: UPDATE set NULL LOB to value
UPDATE TEST_LOBS SET col_clob = 'Was null, now has value', col_blob = HEXTORAW('DEADBEEF') WHERE id = 3;
COMMIT;

-- DML: DELETE row with LOBs
DELETE FROM TEST_LOBS WHERE id = 1;
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
