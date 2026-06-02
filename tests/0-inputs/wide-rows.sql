-- wide-rows.sql: VARCHAR2(4000) max-length values and wide rows.
-- Tests block-spanning redo records with large column values.
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
    FROM user_tables WHERE table_name = 'TEST_WIDE';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_WIDE PURGE';
    END IF;
END;
/

CREATE TABLE TEST_WIDE (
    id         NUMBER PRIMARY KEY,
    col_short  VARCHAR2(10),
    col_medium VARCHAR2(200),
    col_long1  VARCHAR2(4000),
    col_long2  VARCHAR2(4000)
);

ALTER TABLE TEST_WIDE ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- INSERT: max-length columns (8000+ bytes per row across two VARCHAR2(4000))
INSERT INTO TEST_WIDE VALUES (1, 'short', RPAD('M', 200, 'M'), RPAD('A', 4000, 'A'), RPAD('B', 4000, 'B'));
COMMIT;

-- INSERT: one long column, other NULL
INSERT INTO TEST_WIDE VALUES (2, 'half', NULL, RPAD('X', 4000, 'X'), NULL);
COMMIT;

-- UPDATE: change both long columns
UPDATE TEST_WIDE SET col_long1 = RPAD('C', 4000, 'C'), col_long2 = RPAD('D', 4000, 'D') WHERE id = 1;
COMMIT;

-- UPDATE: set long column to short value
UPDATE TEST_WIDE SET col_long1 = 'now short' WHERE id = 2;
COMMIT;

-- UPDATE: set short value to max-length
UPDATE TEST_WIDE SET col_long2 = RPAD('Z', 4000, 'Z') WHERE id = 2;
COMMIT;

-- DELETE: row with max-length columns
DELETE FROM TEST_WIDE WHERE id = 1;
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
