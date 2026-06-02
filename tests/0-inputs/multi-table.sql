-- multi-table.sql: Test DML across multiple tables within and across transactions.
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
    FOR t IN (SELECT table_name FROM user_tables
              WHERE table_name IN ('TEST_ORDERS', 'TEST_ITEMS', 'TEST_AUDIT')) LOOP
        EXECUTE IMMEDIATE 'DROP TABLE ' || t.table_name || ' PURGE';
    END LOOP;
END;
/

CREATE TABLE TEST_ORDERS (
    order_id    NUMBER PRIMARY KEY,
    customer    VARCHAR2(100),
    order_date  DATE,
    status      VARCHAR2(20)
);

CREATE TABLE TEST_ITEMS (
    item_id   NUMBER PRIMARY KEY,
    order_id  NUMBER,
    product   VARCHAR2(100),
    qty       NUMBER,
    price     NUMBER(10,2)
);

CREATE TABLE TEST_AUDIT (
    audit_id  NUMBER PRIMARY KEY,
    action    VARCHAR2(50),
    detail    VARCHAR2(200)
);

ALTER TABLE TEST_ORDERS ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;
ALTER TABLE TEST_ITEMS ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;
ALTER TABLE TEST_AUDIT ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;

-- Record start SCN
DECLARE
    v_start_scn NUMBER;
BEGIN
    SELECT current_scn INTO v_start_scn FROM v$database;
    DBMS_OUTPUT.PUT_LINE('FIXTURE_SCN_START: ' || v_start_scn);
END;
/

-- Transaction 1: insert across all three tables in one commit
INSERT INTO TEST_ORDERS VALUES (1, 'Alice', TO_DATE('2025-01-15', 'YYYY-MM-DD'), 'NEW');
INSERT INTO TEST_ITEMS VALUES (101, 1, 'Widget A', 2, 19.99);
INSERT INTO TEST_ITEMS VALUES (102, 1, 'Widget B', 1, 49.99);
INSERT INTO TEST_AUDIT VALUES (1, 'ORDER_CREATED', 'Order 1 for Alice');
COMMIT;

-- Transaction 2: second order, separate commit
INSERT INTO TEST_ORDERS VALUES (2, 'Bob', TO_DATE('2025-01-16', 'YYYY-MM-DD'), 'NEW');
INSERT INTO TEST_ITEMS VALUES (201, 2, 'Gadget X', 5, 9.99);
COMMIT;

-- Transaction 3: update across tables
UPDATE TEST_ORDERS SET status = 'SHIPPED' WHERE order_id = 1;
UPDATE TEST_ITEMS SET qty = 3 WHERE item_id = 101;
INSERT INTO TEST_AUDIT VALUES (2, 'ORDER_SHIPPED', 'Order 1 shipped');
COMMIT;

-- Transaction 4: delete from multiple tables
DELETE FROM TEST_ITEMS WHERE order_id = 2;
DELETE FROM TEST_ORDERS WHERE order_id = 2;
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
