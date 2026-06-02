-- special-chars.sql: Test special characters in string data.
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
    FROM user_tables WHERE table_name = 'TEST_SPECIAL';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_SPECIAL PURGE';
    END IF;
END;
/

CREATE TABLE TEST_SPECIAL (
    id    NUMBER PRIMARY KEY,
    label VARCHAR2(100),
    data  VARCHAR2(500)
);

ALTER TABLE TEST_SPECIAL ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- DML: strings with single quotes (escaped as '')
INSERT INTO TEST_SPECIAL VALUES (1, 'single quote', 'it''s a test');
INSERT INTO TEST_SPECIAL VALUES (2, 'double quote', 'she said "hello"');
INSERT INTO TEST_SPECIAL VALUES (3, 'backslash', 'path\to\file');
INSERT INTO TEST_SPECIAL VALUES (4, 'pipe and ampersand', 'a|b&c');
COMMIT;

-- DML: strings with whitespace characters
INSERT INTO TEST_SPECIAL VALUES (5, 'tab char', 'before' || CHR(9) || 'after');
INSERT INTO TEST_SPECIAL VALUES (6, 'newline', 'line1' || CHR(10) || 'line2');
INSERT INTO TEST_SPECIAL VALUES (7, 'cr+lf', 'line1' || CHR(13) || CHR(10) || 'line2');
COMMIT;

-- DML: empty string and spaces
INSERT INTO TEST_SPECIAL VALUES (8, 'spaces only', '   ');
INSERT INTO TEST_SPECIAL VALUES (9, 'leading trailing', '  padded  ');
COMMIT;

-- DML: update with special chars
UPDATE TEST_SPECIAL SET data = 'updated: it''s "new"' WHERE id = 1;
COMMIT;

-- DML: delete
DELETE FROM TEST_SPECIAL WHERE id = 4;
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
