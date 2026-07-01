// Standalone unit test for ReaderUdev's ASM file-number parsing.
//
// Run (from repo root):
//   g++ -std=c++17 -fsanitize=address,undefined \
//       tests/unit/test_parse_asm_filenum.cpp -o /tmp/test_parse && /tmp/test_parse
//
// Expected output: "OK". Any failed assertion prints the file/line of the broken case.

#include <cassert>
#include <cstdio>

#include "../../src/reader/AsmFileName.h"

using OpenLogReplicator::parseAsmFileNumber;

int main() {
    uint32_t n = 0;

    // Fully-qualified system names -> file number extracted.
    assert(parseAsmFileNumber("group_4.267.1224328757", n) && n == 267);
    assert(parseAsmFileNumber("thread_1_seq_42.300.1158000000", n) && n == 300);

    // User aliases / malformed names -> rejected (fall back to name matching).
    assert(!parseAsmFileNumber("redo01.log", n));      // "log" is not numeric
    assert(!parseAsmFileNumber("redo01log", n));       // no dots
    assert(!parseAsmFileNumber("foo.267", n));         // only one dot
    assert(!parseAsmFileNumber("group_4.267.", n));    // empty incarnation
    assert(!parseAsmFileNumber(".267.123", n));        // empty tag

    printf("OK\n");
    return 0;
}
