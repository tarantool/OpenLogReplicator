-- partitioned-table.sql: Test DML on range-partitioned and list-partitioned tables.
-- Run as PDB user (e.g., olr_test/olr_test@//localhost:1521/FREEPDB1)
--
-- Outputs: FIXTURE_SCN_START: <scn> and FIXTURE_SCN_END: <scn>

SET SERVEROUTPUT ON
SET FEEDBACK OFF
SET ECHO OFF

-- Setup: drop and recreate tables
DECLARE
    v_table_exists NUMBER;
BEGIN
    SELECT COUNT(*) INTO v_table_exists
    FROM user_tables WHERE table_name = 'TEST_RANGE_PART';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_RANGE_PART PURGE';
    END IF;

    SELECT COUNT(*) INTO v_table_exists
    FROM user_tables WHERE table_name = 'TEST_LIST_PART';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_LIST_PART PURGE';
    END IF;
END;
/

-- Range-partitioned table by ID
CREATE TABLE TEST_RANGE_PART (
    id     NUMBER PRIMARY KEY,
    name   VARCHAR2(100),
    val    NUMBER
)
PARTITION BY RANGE (id) (
    PARTITION p_low    VALUES LESS THAN (100),
    PARTITION p_mid    VALUES LESS THAN (200),
    PARTITION p_high   VALUES LESS THAN (MAXVALUE)
);

ALTER TABLE TEST_RANGE_PART ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- List-partitioned table by region
CREATE TABLE TEST_LIST_PART (
    id      NUMBER PRIMARY KEY,
    region  VARCHAR2(20),
    amount  NUMBER(10,2)
)
PARTITION BY LIST (region) (
    PARTITION p_east  VALUES ('EAST'),
    PARTITION p_west  VALUES ('WEST'),
    PARTITION p_other VALUES (DEFAULT)
);

ALTER TABLE TEST_LIST_PART ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- DML: INSERT into different range partitions
INSERT INTO TEST_RANGE_PART VALUES (10, 'Low-A', 100);
INSERT INTO TEST_RANGE_PART VALUES (50, 'Low-B', 200);
INSERT INTO TEST_RANGE_PART VALUES (150, 'Mid-A', 300);
INSERT INTO TEST_RANGE_PART VALUES (250, 'High-A', 400);
COMMIT;

-- DML: INSERT into different list partitions
INSERT INTO TEST_LIST_PART VALUES (1, 'EAST', 1000.50);
INSERT INTO TEST_LIST_PART VALUES (2, 'WEST', 2000.75);
INSERT INTO TEST_LIST_PART VALUES (3, 'NORTH', 3000.00);
COMMIT;

-- DML: UPDATE rows in different partitions
UPDATE TEST_RANGE_PART SET val = 150 WHERE id = 10;
UPDATE TEST_RANGE_PART SET val = 350 WHERE id = 150;
COMMIT;

UPDATE TEST_LIST_PART SET amount = 1500.00 WHERE id = 1;
COMMIT;

-- DML: DELETE from different partitions
DELETE FROM TEST_RANGE_PART WHERE id = 50;
DELETE FROM TEST_RANGE_PART WHERE id = 250;
COMMIT;

DELETE FROM TEST_LIST_PART WHERE id = 3;
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
