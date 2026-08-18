/* JSON string escaping rules, extracted from BuilderJson
   Copyright (C) 2026 OpenLogReplicator

This file is part of OpenLogReplicator.

This program is free software: you can redistribute it and/or
modify it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with this program; see the file LICENSE;
If not, see <http://www.gnu.org/licenses/>. */

#ifndef JSON_ESCAPE_H_
#define JSON_ESCAPE_H_

#include <cstdint>

#include "../common/types/Data.h"

namespace OpenLogReplicator {

    enum class JsonEscapeKind : uint8_t {
        NONE,       // written as-is
        SHORT,      // the two-character sequence in JsonEscape::seq
        BACKSLASH,  // a backslash followed by the character
        UNICODE     // "\u00" followed by two hexadecimal digits
    };

    struct JsonEscape {
        JsonEscapeKind kind;
        const char* seq;  // SHORT only
    };

    // RFC 8259: everything except '"', '\' and the control characters U+0000 - U+001F is written
    // as-is, so bytes >= 0x80 of multi-byte UTF-8 sequences pass through.
    inline constexpr JsonEscape jsonEscape(char character) {
        switch (character) {
            case '\b':
                return {JsonEscapeKind::SHORT, "\\b"};
            case '\t':
                return {JsonEscapeKind::SHORT, "\\t"};
            case '\n':
                return {JsonEscapeKind::SHORT, "\\n"};
            case '\f':
                return {JsonEscapeKind::SHORT, "\\f"};
            case '\r':
                return {JsonEscapeKind::SHORT, "\\r"};
            case '"':
            case '\\':
            case '/':
                return {JsonEscapeKind::BACKSLASH, nullptr};
            default:
                if (static_cast<unsigned char>(character) < 0x20)
                    return {JsonEscapeKind::UNICODE, nullptr};
                return {JsonEscapeKind::NONE, nullptr};
        }
    }

    // Writes str escaped, one character at a time, through a sink providing character(char) and
    // text(const char*, uint64_t). Every producer of a JSON string goes through here.
    template<typename Sink>
    void jsonEscapeTo(const char* str, uint64_t size, Sink& sink) {
        while (size > 0) {
            const char character = *str;
            const JsonEscape rule = jsonEscape(character);
            switch (rule.kind) {
                case JsonEscapeKind::SHORT:
                    sink.text(rule.seq, 2);
                    break;
                case JsonEscapeKind::BACKSLASH:
                    sink.character('\\');
                    sink.character(character);
                    break;
                case JsonEscapeKind::UNICODE: {
                    char digits[2];
                    Data::map16x2(static_cast<uint8_t>(character), digits);
                    sink.text("\\u00", 4);
                    sink.character(digits[0]);
                    sink.character(digits[1]);
                    break;
                }
                case JsonEscapeKind::NONE:
                    sink.character(character);
                    break;
            }
            ++str;
            --size;
        }
    }
}

#endif
