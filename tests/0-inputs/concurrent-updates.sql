-- concurrent-updates.sql: Same row updated across rapid commits.
-- Tests before/after image ordering and transaction boundaries.
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
    FROM user_tables WHERE table_name = 'TEST_CONCURRENT';
    IF v_table_exists > 0 THEN
        EXECUTE IMMEDIATE 'DROP TABLE TEST_CONCURRENT PURGE';
    END IF;
END;
/

CREATE TABLE TEST_CONCURRENT (
    id      NUMBER PRIMARY KEY,
    status  VARCHAR2(20),
    counter NUMBER,
    note    VARCHAR2(100)
);

ALTER TABLE TEST_CONCURRENT ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- Seed rows
INSERT INTO TEST_CONCURRENT VALUES (1, 'new', 0, 'row one');
INSERT INTO TEST_CONCURRENT VALUES (2, 'new', 0, 'row two');
INSERT INTO TEST_CONCURRENT VALUES (3, 'new', 0, 'row three');
COMMIT;

-- Rapid updates to same row (id=1) across separate commits
UPDATE TEST_CONCURRENT SET status = 'pending', counter = 1 WHERE id = 1;
COMMIT;

UPDATE TEST_CONCURRENT SET status = 'active', counter = 2 WHERE id = 1;
COMMIT;

UPDATE TEST_CONCURRENT SET status = 'complete', counter = 3, note = 'done' WHERE id = 1;
COMMIT;

-- Update different rows in same transaction then commit
UPDATE TEST_CONCURRENT SET counter = 10 WHERE id = 2;
UPDATE TEST_CONCURRENT SET counter = 20 WHERE id = 3;
COMMIT;

-- Update same row twice in one transaction
UPDATE TEST_CONCURRENT SET status = 'reopened', counter = 4 WHERE id = 1;
UPDATE TEST_CONCURRENT SET status = 'closed', counter = 5, note = 'final' WHERE id = 1;
COMMIT;

-- Delete and re-insert same PK in separate transactions
DELETE FROM TEST_CONCURRENT WHERE id = 2;
COMMIT;

INSERT INTO TEST_CONCURRENT VALUES (2, 'resurrected', 1, 'back again');
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
