// Standalone unit test for the JSON string escaping rules used by BuilderJson.
//
// Run (from repo root, with the rapidjson checkout the build already needs):
//   g++ -std=c++17 -I./rapidjson/include -fsanitize=address,undefined \
//       tests/unit/test_json_escape.cpp -o /tmp/test_json_escape && /tmp/test_json_escape
//
// Expected output: "OK". Any failed assertion prints the file/line of the broken case.

#include <cassert>
#include <cstdio>
#include <string>

#include <rapidjson/document.h>

#include "../../src/builder/JsonEscape.h"

using OpenLogReplicator::JsonEscapeKind;
using OpenLogReplicator::jsonEscape;

namespace {
    // The sink BuilderJson uses writes into its output buffer, this one into a string. Everything
    // else is the very code the builder runs.
    struct StringSink {
        std::string out;

        void character(char value) {
            out += value;
        }

        void text(const char* data, uint64_t size) {
            out.append(data, size);
        }
    };

    std::string escape(const std::string& raw) {
        StringSink sink;
        OpenLogReplicator::jsonEscapeTo(raw.c_str(), raw.size(), sink);
        return sink.out;
    }

    // A value is escaped correctly only if a standard parser accepts the message and hands back
    // the very same bytes.
    void roundTrip(const std::string& raw) {
        const std::string json = R"({"v":")" + escape(raw) + R"("})";

        rapidjson::Document document;
        assert(!document.Parse(json.c_str()).HasParseError());
        assert(document["v"].IsString());
        assert(std::string(document["v"].GetString(), document["v"].GetStringLength()) == raw);
    }
}

int main() {
    for (int c = 0x00; c <= 0x1F; ++c)
        assert(jsonEscape(static_cast<char>(c)).kind != JsonEscapeKind::NONE);

    assert(escape("\b\t\n\f\r") == "\\b\\t\\n\\f\\r");
    assert(escape(std::string("\x0b\x0e\x1e\x1f", 4)) == "\\u000b\\u000e\\u001e\\u001f");
    assert(escape("\"\\/") == "\\\"\\\\\\/");
    assert(escape(" A~") == " A~");

    // Every ASCII byte survives a real parser, control characters included.
    for (int c = 0x00; c <= 0x7F; ++c)
        roundTrip(std::string(1, static_cast<char>(c)));

    // Multi-byte UTF-8 is passed through untouched.
    roundTrip("ольчет");
    roundTrip("日本語");

    // The value from the incident: NUL and 0x1F inside an otherwise printable string.
    roundTrip(std::string("10028,5175740670,11") + '\x00' + '\x1f' + "175");

    printf("OK\n");
    return 0;
}
