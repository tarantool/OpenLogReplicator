-- many-columns.sql: Test table with 50+ columns.
-- Stress tests OLR's column parsing, supplemental log handling,
-- and redo record layout with wide schema definitions.
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
    FROM user_tables WHERE table_name = 'TEST_MANY_COLS';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_MANY_COLS PURGE';
    END IF;
END;
/

CREATE TABLE TEST_MANY_COLS (
    id       NUMBER PRIMARY KEY,
    col_01   VARCHAR2(50),
    col_02   VARCHAR2(50),
    col_03   VARCHAR2(50),
    col_04   VARCHAR2(50),
    col_05   VARCHAR2(50),
    col_06   VARCHAR2(50),
    col_07   VARCHAR2(50),
    col_08   VARCHAR2(50),
    col_09   VARCHAR2(50),
    col_10   VARCHAR2(50),
    col_11   NUMBER,
    col_12   NUMBER,
    col_13   NUMBER,
    col_14   NUMBER,
    col_15   NUMBER,
    col_16   NUMBER,
    col_17   NUMBER,
    col_18   NUMBER,
    col_19   NUMBER,
    col_20   NUMBER,
    col_21   DATE,
    col_22   DATE,
    col_23   DATE,
    col_24   TIMESTAMP,
    col_25   TIMESTAMP,
    col_26   VARCHAR2(100),
    col_27   VARCHAR2(100),
    col_28   VARCHAR2(100),
    col_29   VARCHAR2(100),
    col_30   VARCHAR2(100),
    col_31   NUMBER(10,2),
    col_32   NUMBER(10,2),
    col_33   NUMBER(10,2),
    col_34   NUMBER(10,2),
    col_35   NUMBER(10,2),
    col_36   VARCHAR2(200),
    col_37   VARCHAR2(200),
    col_38   VARCHAR2(200),
    col_39   VARCHAR2(200),
    col_40   VARCHAR2(200),
    col_41   NUMBER,
    col_42   NUMBER,
    col_43   NUMBER,
    col_44   NUMBER,
    col_45   NUMBER,
    col_46   VARCHAR2(50),
    col_47   VARCHAR2(50),
    col_48   VARCHAR2(50),
    col_49   VARCHAR2(50),
    col_50   VARCHAR2(50),
    col_51   NUMBER,
    col_52   NUMBER,
    col_53   NUMBER,
    col_54   NUMBER,
    col_55   NUMBER,
    col_56   VARCHAR2(100),
    col_57   VARCHAR2(100),
    col_58   VARCHAR2(100),
    col_59   VARCHAR2(100),
    col_60   VARCHAR2(100)
);

ALTER TABLE TEST_MANY_COLS ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- Insert 1: All columns populated
INSERT INTO TEST_MANY_COLS VALUES (
    1,
    'str01', 'str02', 'str03', 'str04', 'str05',
    'str06', 'str07', 'str08', 'str09', 'str10',
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    TO_DATE('2025-01-15', 'YYYY-MM-DD'),
    TO_DATE('2025-06-30', 'YYYY-MM-DD'),
    TO_DATE('2025-12-31', 'YYYY-MM-DD'),
    TO_TIMESTAMP('2025-01-15 10:30:00.123456', 'YYYY-MM-DD HH24:MI:SS.FF6'),
    TO_TIMESTAMP('2025-06-30 23:59:59.999999', 'YYYY-MM-DD HH24:MI:SS.FF6'),
    'medium26', 'medium27', 'medium28', 'medium29', 'medium30',
    31.01, 32.02, 33.03, 34.04, 35.05,
    'long36-value', 'long37-value', 'long38-value', 'long39-value', 'long40-value',
    41, 42, 43, 44, 45,
    'str46', 'str47', 'str48', 'str49', 'str50',
    51, 52, 53, 54, 55,
    'medium56', 'medium57', 'medium58', 'medium59', 'medium60'
);
COMMIT;

-- Insert 2: Many NULLs (sparse row)
INSERT INTO TEST_MANY_COLS (id, col_01, col_11, col_21, col_31, col_41, col_51) VALUES (
    2, 'sparse', 99, TO_DATE('2025-03-15', 'YYYY-MM-DD'), 99.99, 99, 99
);
COMMIT;

-- Update 1: Change many columns at once on the full row
UPDATE TEST_MANY_COLS SET
    col_01 = 'updated01', col_05 = 'updated05', col_10 = 'updated10',
    col_11 = 111, col_15 = 115, col_20 = 120,
    col_26 = 'updated26', col_30 = 'updated30',
    col_36 = 'updated36-long-value', col_40 = 'updated40-long-value',
    col_46 = 'updated46', col_50 = 'updated50',
    col_56 = 'updated56', col_60 = 'updated60'
WHERE id = 1;
COMMIT;

-- Update 2: Change a single column on the sparse row
UPDATE TEST_MANY_COLS SET col_01 = 'sparse-updated' WHERE id = 2;
COMMIT;

-- Delete the sparse row
DELETE FROM TEST_MANY_COLS WHERE id = 2;
COMMIT;

-- Delete the full row
DELETE FROM TEST_MANY_COLS WHERE id = 1;
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
