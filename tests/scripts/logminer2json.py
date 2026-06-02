#!/usr/bin/env python3
"""Convert LogMiner pipe-delimited output to canonical JSON for comparison.

Input format (one line per DML statement):
  SCN|OPERATION|SEG_OWNER|TABLE_NAME|XID|SQL_REDO|SQL_UNDO

Output: one JSON object per line with normalized fields:
  {"scn": "...", "op": "INSERT|UPDATE|DELETE", "owner": "...", "table": "...",
   "xid": "...", "after": {...}, "before": {...}}

All column values are stored as strings for type-agnostic comparison.
"""

import json
import re
import sys


def parse_insert(sql_redo):
    """Parse: insert into "OWNER"."TABLE"("COL1","COL2",...) values ('v1','v2',...)"""
    m = re.match(
        r'insert into "[^"]*"\."[^"]*"\((.+?)\)\s+values\s+\((.+)\)\s*;?\s*$',
        sql_redo, re.IGNORECASE | re.DOTALL
    )
    if not m:
        return None
    cols = parse_column_list(m.group(1))
    vals = parse_value_list(m.group(2))
    if len(cols) != len(vals):
        return None
    return {"after": dict(zip(cols, vals))}


def parse_update(sql_redo):
    """Parse: update "OWNER"."TABLE" set "COL1" = 'v1', ... where "COL2" = 'v2' and ..."""
    m = re.match(
        r'update "[^"]*"\."[^"]*"\s+set\s+(.+?)\s+where\s+(.+)\s*;?\s*$',
        sql_redo, re.IGNORECASE | re.DOTALL
    )
    if not m:
        return None
    after = parse_assignments(m.group(1))
    before = parse_where_clause(m.group(2))
    return {"before": before, "after": after}


def parse_delete(sql_redo):
    """Parse: delete from "OWNER"."TABLE" where "COL1" = 'v1' and ..."""
    m = re.match(
        r'delete from "[^"]*"\."[^"]*"\s+where\s+(.+)\s*;?\s*$',
        sql_redo, re.IGNORECASE | re.DOTALL
    )
    if not m:
        return None
    before = parse_where_clause(m.group(1))
    return {"before": before}


def parse_column_list(s):
    """Parse quoted column names: "COL1","COL2",... """
    return re.findall(r'"([^"]+)"', s)


def parse_value_list(s):
    """Parse values: 'v1','v2',NULL,TO_DATE(...),... """
    values = []
    i = 0
    while i < len(s):
        c = s[i]
        if c in (' ', ','):
            i += 1
            continue
        if c == "'":
            # Quoted string — handle escaped quotes ('')
            j = i + 1
            val = []
            while j < len(s):
                if s[j] == "'" and j + 1 < len(s) and s[j + 1] == "'":
                    val.append("'")
                    j += 2
                elif s[j] == "'":
                    j += 1
                    break
                else:
                    val.append(s[j])
                    j += 1
            values.append("".join(val))
            i = j
        elif s[i:i+4].upper() == 'NULL':
            values.append(None)
            i += 4
        elif s[i:i+7].upper() == 'TO_DATE':
            # TO_DATE('...','...') — extract the date string
            m = re.match(r"TO_DATE\('([^']*)'", s[i:], re.IGNORECASE)
            if m:
                values.append(m.group(1))
            else:
                values.append(s[i:])
            # Skip to matching closing paren
            depth = 0
            while i < len(s):
                if s[i] == '(':
                    depth += 1
                elif s[i] == ')':
                    depth -= 1
                    if depth == 0:
                        i += 1
                        break
                i += 1
        elif s[i:i+12].upper() == 'TO_TIMESTAMP':
            m = re.match(r"TO_TIMESTAMP\('([^']*)'", s[i:], re.IGNORECASE)
            if m:
                values.append(m.group(1))
            else:
                values.append(s[i:])
            depth = 0
            while i < len(s):
                if s[i] == '(':
                    depth += 1
                elif s[i] == ')':
                    depth -= 1
                    if depth == 0:
                        i += 1
                        break
                i += 1
        elif s[i:i+8].upper() == 'HEXTORAW':
            m = re.match(r"HEXTORAW\('([^']*)'\)", s[i:], re.IGNORECASE)
            if m:
                values.append(m.group(1))
            else:
                values.append(s[i:])
            depth = 0
            while i < len(s):
                if s[i] == '(':
                    depth += 1
                elif s[i] == ')':
                    depth -= 1
                    if depth == 0:
                        i += 1
                        break
                i += 1
        else:
            # Unquoted number or other literal
            j = i
            while j < len(s) and s[j] not in (',', ' '):
                j += 1
            if j == i:
                # Skip unrecognized character to avoid infinite loop
                i += 1
                continue
            values.append(s[i:j])
            i = j
    return values


def parse_assignments(s):
    """Parse SET clause: "COL1" = 'v1', "COL2" = 'v2', ..."""
    result = {}
    # Match "COL" = value patterns
    pattern = r'"([^"]+)"\s*=\s*'
    parts = re.split(r',\s*(?=")', s)
    for part in parts:
        m = re.match(r'\s*"([^"]+)"\s*=\s*(.+)$', part.strip(), re.DOTALL)
        if m:
            col = m.group(1)
            val_str = m.group(2).strip()
            result[col] = extract_value(val_str)
    return result


def parse_where_clause(s):
    """Parse WHERE clause: "COL1" = 'v1' and "COL2" = 'v2' and ..."""
    result = {}
    # Split on ' and ' (case-insensitive) but not within quotes
    parts = re.split(r'\s+and\s+', s, flags=re.IGNORECASE)
    for part in parts:
        m = re.match(r'\s*"([^"]+)"\s*=\s*(.+)$', part.strip(), re.DOTALL)
        if m:
            col = m.group(1)
            val_str = m.group(2).strip()
            result[col] = extract_value(val_str)
        # Handle IS NULL
        m2 = re.match(r'\s*"([^"]+)"\s+IS\s+NULL', part.strip(), re.IGNORECASE)
        if m2:
            result[m2.group(1)] = None
    return result


def extract_value(val_str):
    """Extract a single value from SQL expression."""
    val_str = val_str.rstrip(';').strip()
    if val_str.upper() == 'NULL':
        return None
    if val_str.startswith("'") and val_str.endswith("'"):
        # Unescape ''
        return val_str[1:-1].replace("''", "'")
    m = re.match(r"TO_DATE\('([^']*)'", val_str, re.IGNORECASE)
    if m:
        return m.group(1)
    m = re.match(r"TO_TIMESTAMP\('([^']*)'", val_str, re.IGNORECASE)
    if m:
        return m.group(1)
    m = re.match(r"HEXTORAW\('([^']*)'\)", val_str, re.IGNORECASE)
    if m:
        return m.group(1)
    return val_str


def convert_line(line):
    """Convert one pipe-delimited LogMiner line to a dict."""
    parts = line.split('|', 6)
    if len(parts) < 7:
        return None

    scn, operation, seg_owner, table_name, xid, sql_redo, sql_undo = parts
    operation = operation.strip()
    sql_redo = sql_redo.strip()

    record = {
        "scn": scn.strip(),
        "op": operation,
        "owner": seg_owner.strip(),
        "table": table_name.strip(),
        "xid": xid.strip(),
    }

    if operation == 'INSERT':
        parsed = parse_insert(sql_redo)
    elif operation == 'UPDATE':
        parsed = parse_update(sql_redo)
    elif operation == 'DELETE':
        parsed = parse_delete(sql_redo)
    else:
        return None

    if parsed is None:
        print(f"WARNING: Failed to parse SQL_REDO: {sql_redo}", file=sys.stderr)
        record["after"] = {}
        record["before"] = {}
        return record

    record.update(parsed)
    return record


SQL_START_RE = re.compile(r'^(insert into|update|delete from)\s+"', re.IGNORECASE)


def merge_continuation_lines(lines):
    """Merge LogMiner continuation rows for long SQL_REDO/SQL_UNDO.

    When SQL_REDO exceeds ~4000 chars, LogMiner splits it across multiple rows
    with the same scn|op|owner|table|xid prefix. Continuation rows have sql_redo
    that doesn't start with an SQL keyword (insert/update/delete).
    """
    merged = []
    accum = None  # (header_parts[0:5], sql_redo, sql_undo)
    for line in lines:
        parts = line.split('|', 6)
        if len(parts) < 6:
            continue
        sql_redo = parts[5] if len(parts) > 5 else ''
        sql_undo = parts[6] if len(parts) > 6 else ''
        if accum and not SQL_START_RE.match(sql_redo.strip()):
            accum = (accum[0], accum[1] + sql_redo, accum[2] + sql_undo)
        else:
            if accum:
                merged.append('|'.join(accum[0]) + '|' + accum[1] + '|' + accum[2])
            accum = (parts[:5], sql_redo, sql_undo)
    if accum:
        merged.append('|'.join(accum[0]) + '|' + accum[1] + '|' + accum[2])
    return merged


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <logminer-output-file> [output-file]", file=sys.stderr)
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None

    raw_lines = []
    with open(input_file) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('--') or line.startswith('SQL>'):
                continue
            raw_lines.append(line)

    merged_lines = merge_continuation_lines(raw_lines)

    records = []
    for line in merged_lines:
        rec = convert_line(line)
        if rec:
            records.append(rec)

    output = '\n'.join(json.dumps(r, sort_keys=True) for r in records) + '\n'

    if output_file:
        with open(output_file, 'w') as f:
            f.write(output)
    else:
        sys.stdout.write(output)


if __name__ == '__main__':
    main()
