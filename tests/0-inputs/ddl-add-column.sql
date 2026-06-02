-- ddl-add-column.sql: DML around an ALTER TABLE ADD COLUMN DDL change.
-- Tests OLR's ability to handle schema changes mid-stream.
-- @DDL
--
-- Scenario:
--   1. Create table with 3 columns, do some INSERTs
--   2. ALTER TABLE ADD COLUMN (DDL)
--   3. Do more DML using the new column
--   4. Verify OLR correctly handles schema change

SET SERVEROUTPUT ON
SET FEEDBACK OFF
SET ECHO OFF

-- Setup: drop and recreate table
DECLARE
    v_table_exists NUMBER;
BEGIN
    SELECT COUNT(*) INTO v_table_exists
    FROM user_tables WHERE table_name = 'TEST_DDL';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_DDL PURGE';
    END IF;
END;
/

CREATE TABLE TEST_DDL (
    id   NUMBER PRIMARY KEY,
    name VARCHAR2(100),
    val  NUMBER
);

ALTER TABLE TEST_DDL ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- Phase 1: DML with original schema (3 columns)
INSERT INTO TEST_DDL VALUES (1, 'Alice', 100);
INSERT INTO TEST_DDL VALUES (2, 'Bob', 200);
INSERT INTO TEST_DDL VALUES (3, 'Charlie', 300);
COMMIT;

-- DDL: Add a new column
ALTER TABLE TEST_DDL ADD (email VARCHAR2(200));

-- Phase 2: DML with new schema (4 columns)
INSERT INTO TEST_DDL VALUES (4, 'Dave', 400, 'dave@test.com');
UPDATE TEST_DDL SET email = 'alice@test.com' WHERE id = 1;
UPDATE TEST_DDL SET val = 250, email = 'bob@test.com' WHERE id = 2;
DELETE FROM TEST_DDL WHERE id = 3;
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
