/* Memory buffer for handling output buffer in JSON format
   Copyright (C) 2018-2026 Adam Leszczynski (aleszczynski@bersler.com)

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

#include <ctime>

#include "../common/DbTable.h"
#include "../common/types/RowId.h"
#include "../metadata/Metadata.h"
#include "../metadata/Schema.h"
#include "BuilderJson.h"

namespace OpenLogReplicator {
    BuilderJson::BuilderJson(Ctx* newCtx, Locales* newLocales, Metadata* newMetadata, Format& newFormat, uint64_t newFlushBuffer):
            Builder(newCtx, newLocales, newMetadata, newFormat, newFlushBuffer) {}

    void BuilderJson::columnFloat(const std::string& columnName, double value) {
        comma(hasPreviousColumn);
        append('"');
        appendEscape(columnName);
        append(std::string_view(R"(":)"));

        std::ostringstream ss;
        ss << value;
        append(ss.str());
    }

    void BuilderJson::columnDouble(const std::string& columnName, long double value) {
        comma(hasPreviousColumn);
        append('"');
        appendEscape(columnName);
        append(std::string_view(R"(":)"));

        std::ostringstream ss;
        ss << value;
        append(ss.str());
    }

    void BuilderJson::columnString(const std::string& columnName) {
        comma(hasPreviousColumn);
        append('"');
        appendEscape(columnName);
        append(std::string_view(R"(":")"));
        appendEscape(valueBuffer, valueSize);
        append('"');
    }

    void BuilderJson::columnNumber(const std::string& columnName, int precision __attribute__((unused)),
            int scale __attribute__((unused))) {
        comma(hasPreviousColumn);
        append('"');
        appendEscape(columnName);
        append(std::string_view(R"(":)"));
        appendArr(valueBuffer, valueSize);
    }

    void BuilderJson::columnRowId(const std::string& columnName, RowId rowId) {
        comma(hasPreviousColumn);
        append('"');
        appendEscape(columnName);
        append(std::string_view(R"(":")"));
        char str[RowId::SIZE + 1];
        rowId.toHex(str);
        appendArr(str, 18);
        append('"');
    }

    void BuilderJson::columnRaw(const std::string& columnName, const uint8_t* data, uint64_t size) {
        if (likely(lastBuilderSize + messagePosition + size * 2 + columnName.size() * 3 + 8 < OUTPUT_BUFFER_DATA_SIZE)) {
            if (hasPreviousColumn)
                append<true>(',');
            else
                hasPreviousColumn = true;

            append<true>('"');
            appendEscape<true>(columnName);
            append<true>(std::string_view(R"(":")"));
            for (uint64_t j = 0; j < size; ++j)
                appendHex2<true>(*(data + j));
            append<true>('"');
        } else {
            comma(hasPreviousColumn);
            append('"');
            appendEscape(columnName);
            append(std::string_view(R"(":")"));
            for (uint64_t j = 0; j < size; ++j)
                appendHex2(*(data + j));
            append('"');
        }
    }

    void BuilderJson::columnTimestamp(const std::string& columnName, time_t timestamp, uint64_t fraction) {
        comma(hasPreviousColumn);
        append('"');
        appendEscape(columnName);
        append(std::string_view(R"(":)"));
        char buffer[22];

        switch (format.timestampFormat) {
            case Format::TIMESTAMP_FORMAT::UNIX_NANO:
                // 1712345678123456789
                if (timestamp < 1000000000 && timestamp > -1000000000)
                    appendSDec((timestamp * 1000000000L) + fraction);
                else {
                    // Big number
                    const int64_t firstDigits = timestamp / 1000000000;
                    if (timestamp < 0) {
                        timestamp = -timestamp;
                        fraction = -fraction;
                    }
                    timestamp %= 1000000000;
                    appendSDec(firstDigits);
                    appendDecN<18>((timestamp * 1000000000L) + fraction);
                }
                break;

            case Format::TIMESTAMP_FORMAT::UNIX_MICRO:
                // 1712345678123457
                appendSDec((timestamp * 1000000L) + ((fraction + 500) / 1000));
                break;

            case Format::TIMESTAMP_FORMAT::UNIX_MILLI:
                // 1712345678123
                appendSDec((timestamp * 1000L) + ((fraction + 500000) / 1000000));
                break;

            case Format::TIMESTAMP_FORMAT::UNIX:
                // 1712345678
                appendSDec(timestamp + ((fraction + 500000000) / 1000000000));
                break;

            case Format::TIMESTAMP_FORMAT::UNIX_NANO_STRING:
                // "1712345678123456789"
                append('"');
                if (timestamp < 1000000000 && timestamp > -1000000000)
                    appendSDec((timestamp * 1000000000L) + fraction);
                else {
                    // Big number
                    const int64_t firstDigits = timestamp / 1000000000;
                    if (timestamp < 0) {
                        timestamp = -timestamp;
                        fraction = -fraction;
                    }
                    timestamp %= 1000000000;
                    appendSDec(firstDigits);
                    appendDecN<18>((timestamp * 1000000000L) + fraction);
                }
                append('"');
                break;

            case Format::TIMESTAMP_FORMAT::UNIX_MICRO_STRING:
                // "1712345678123457"
                append('"');
                appendSDec((timestamp * 1000000L) + ((fraction + 500) / 1000));
                append('"');
                break;

            case Format::TIMESTAMP_FORMAT::UNIX_MILLI_STRING:
                // "1712345678123"
                append('"');
                appendSDec((timestamp * 1000L) + ((fraction + 500000) / 1000000));
                append('"');
                break;

            case Format::TIMESTAMP_FORMAT::UNIX_STRING:
                // "1712345678"
                append('"');
                appendSDec(timestamp + ((fraction + 500000000) / 1000000000));
                append('"');
                break;

            case Format::TIMESTAMP_FORMAT::ISO8601_NANO_TZ:
                // "2024-04-05T19:34:38.123456789Z"
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, true, false));
                append('.');
                appendDecN<9>(fraction);
                append(std::string_view(R"(Z")"));
                break;

            case Format::TIMESTAMP_FORMAT::ISO8601_MICRO_TZ:
                // "2024-04-05T19:34:38.123456Z"
                fraction += 500;
                fraction /= 1000;
                if (fraction >= 1000000) {
                    fraction -= 1000000;
                    ++timestamp;
                }
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, true, false));
                append('.');
                appendDecN<6>(fraction);
                append(std::string_view(R"(Z")"));
                break;

            case Format::TIMESTAMP_FORMAT::ISO8601_MILLI_TZ:
                // "2024-04-05T19:34:38.123Z"
                fraction += 500000;
                fraction /= 1000000;
                if (fraction >= 1000) {
                    fraction -= 1000;
                    ++timestamp;
                }
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, true, false));
                append('.');
                appendDecN<3>(fraction);
                append(std::string_view(R"(Z")"));
                break;

            case Format::TIMESTAMP_FORMAT::ISO8601_TZ:
                // "2024-04-05T19:34:38Z"
                if (fraction >= 500000000)
                    ++timestamp;
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, true, false));
                append(std::string_view(R"(Z")"));
                break;
            case Format::TIMESTAMP_FORMAT::ISO8601_NANO:
                // "2024-04-05 19:34:38.123456789"
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, false, false));
                append('.');
                appendDecN<9>(fraction);
                append('"');
                break;

            case Format::TIMESTAMP_FORMAT::ISO8601_MICRO:
                // "2024-04-05 19:34:38.123456"
                fraction += 500;
                fraction /= 1000;
                if (fraction >= 1000000) {
                    fraction -= 1000000;
                    ++timestamp;
                }
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, false, false));
                append('.');
                appendDecN<6>(fraction);
                append('"');
                break;

            case Format::TIMESTAMP_FORMAT::ISO8601_MILLI:
                // "2024-04-05 19:34:38.123"
                fraction += 500000;
                fraction /= 1000000;
                if (fraction >= 1000) {
                    fraction -= 1000;
                    ++timestamp;
                }
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, false, false));
                append('.');
                appendDecN<3>(fraction);
                append('"');
                break;

            case Format::TIMESTAMP_FORMAT::ISO8601:
                // "2024-04-05 19:34:38"
                if (fraction >= 500000000)
                    ++timestamp;
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, false, false));
                append('"');
                break;
        }
    }

    void BuilderJson::columnTimestampTz(const std::string& columnName, time_t timestamp, uint64_t fraction, const std::string_view& tz) {
        comma(hasPreviousColumn);
        append('"');
        appendEscape(columnName);
        append(std::string_view(R"(":)"));
        char buffer[22];

        switch (format.timestampTzFormat) {
            case Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING:
                // "1700000000.123456789,Europe/Warsaw"
                append('"');
                if (timestamp < 1000000000 && timestamp > -1000000000)
                    appendSDec((timestamp * 1000000000L) + fraction);
                else {
                    // Big number
                    const int64_t firstDigits = timestamp / 1000000000;
                    if (timestamp < 0) {
                        timestamp = -timestamp;
                        fraction = -fraction;
                    }
                    timestamp %= 1000000000;
                    appendSDec(firstDigits);
                    appendDecN<18>((timestamp * 1000000000L) + fraction);
                }
                append(',');
                append(tz);
                append('"');
                break;

            case Format::TIMESTAMP_TZ_FORMAT::UNIX_MICRO_STRING:
                // "1700000000.123456,Europe/Warsaw"
                append('"');
                appendSDec((timestamp * 1000000L) + ((fraction + 500) / 1000));
                append(',');
                append(tz);
                append('"');
                break;

            case Format::TIMESTAMP_TZ_FORMAT::UNIX_MILLI_STRING:
                // "1700000000.123,Europe/Warsaw"
                append('"');
                appendSDec((timestamp * 1000L) + ((fraction + 500000) / 1000000));
                append(',');
                append(tz);
                append('"');
                break;

            case Format::TIMESTAMP_TZ_FORMAT::UNIX_STRING:
                // "1700000000,Europe/Warsaw"
                append('"');
                appendSDec(timestamp + ((fraction + 500000000) / 1000000000));
                append(',');
                append(tz);
                append('"');
                break;

            case Format::TIMESTAMP_TZ_FORMAT::ISO8601_NANO_TZ:
                // "2024-04-05T19:34:38.123456789Z Europe/Warsaw"
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, true, false));
                append('.');
                appendDecN<9>(fraction);
                append(std::string_view("Z "));
                append(tz);
                append('"');
                break;

            case Format::TIMESTAMP_TZ_FORMAT::ISO8601_MICRO_TZ:
                // "2024-04-05T19:34:38.123456Z Europe/Warsaw"
                fraction += 500;
                fraction /= 1000;
                if (fraction >= 1000000) {
                    fraction -= 1000000;
                    ++timestamp;
                }
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, true, false));
                append('.');
                appendDecN<6>(fraction);
                append(std::string_view("Z "));
                append(tz);
                append('"');
                break;

            case Format::TIMESTAMP_TZ_FORMAT::ISO8601_MILLI_TZ:
                // "2024-04-05T19:34:38.123Z Europe/Warsaw"
                fraction += 500000;
                fraction /= 1000000;
                if (fraction >= 1000) {
                    fraction -= 1000;
                    ++timestamp;
                }
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, true, false));
                append('.');
                appendDecN<3>(fraction);
                append(std::string_view("Z "));
                append(tz);
                append('"');
                break;

            case Format::TIMESTAMP_TZ_FORMAT::ISO8601_TZ:
                // "2024-04-05T19:34:38Z Europe/Warsaw"
                if (fraction >= 500000000)
                    ++timestamp;
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, true, false));
                append(std::string_view("Z "));
                append(tz);
                append('"');
                break;

            case Format::TIMESTAMP_TZ_FORMAT::ISO8601_NANO:
                // "2024-04-05 19:34:38.123456789,Europe/Warsaw"
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, false, false));
                append('.');
                appendDecN<9>(fraction);
                append(' ');
                append(tz);
                append('"');
                break;

            case Format::TIMESTAMP_TZ_FORMAT::ISO8601_MICRO:
                // "2024-04-05 19:34:38.123456,Europe/Warsaw"
                fraction += 500;
                fraction /= 1000;
                if (fraction >= 1000000) {
                    fraction -= 1000000;
                    ++timestamp;
                }
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, false, false));
                append('.');
                appendDecN<6>(fraction);
                append(' ');
                append(tz);
                append('"');
                break;

            case Format::TIMESTAMP_TZ_FORMAT::ISO8601_MILLI:
                // "2024-04-05 19:34:38.123 Europe/Warsaw"
                fraction += 500000;
                fraction /= 1000000;
                if (fraction >= 1000) {
                    fraction -= 1000;
                    ++timestamp;
                }
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, false, false));
                append('.');
                appendDecN<3>(fraction);
                append(' ');
                append(tz);
                append('"');
                break;

            case Format::TIMESTAMP_TZ_FORMAT::ISO8601:
                // "2024-04-05 19:34:38 Europe/Warsaw"
                if (fraction >= 500000000)
                    ++timestamp;
                append('"');
                appendArr(buffer, Data::epochToIso8601(timestamp, buffer, false, false));
                append(' ');
                append(tz);
                append('"');
                break;
        }
    }

    void BuilderJson::processBeginMessage(Seq sequence, Time timestamp) {
        newTran = false;
        hasPreviousRedo = false;

        if (format.isMessageFormatSkipBegin())
            return;

        builderBegin(sequence, beginScn, 0, BuilderMsg::OUTPUT_BUFFER::NONE);
        append('{');
        hasPreviousValue = false;
        appendHeader(beginScn, timestamp, true, format.isDbFormatAddDml(), true, format.isUserTypeBegin());

        comma(hasPreviousValue);
        if (format.isAttributesFormatBegin())
            appendAttributes();

        if (format.isMessageFormatFull()) {
            append(std::string_view(R"("payload":[)"));
        } else {
            append(std::string_view(R"("payload":[{"op":"begin"}]})"));
            builderCommit();
        }
    }

    void BuilderJson::processCommit() {
        // Skip empty transaction
        if (newTran) {
            newTran = false;
            return;
        }

        if (format.isMessageFormatFull()) {
            append(std::string_view("]}"));
            builderCommit();
        } else if (!format.isMessageFormatSkipCommit()) {
            builderBegin(commitSequence, commitScn, 0, BuilderMsg::OUTPUT_BUFFER::NONE);
            append('{');

            hasPreviousValue = false;
            appendHeader(commitScn, commitTimestamp, false, format.isDbFormatAddDml(), true, format.isUserTypeCommit());

            comma(hasPreviousValue);
            if (format.isAttributesFormatCommit())
                appendAttributes();

            append(std::string_view(R"("payload":[{"op":"commit"}]})"));
            builderCommit();
        }
        num = 0;
    }

    void BuilderJson::processInsert(Seq sequence, Scn scn, Time timestamp, LobCtx* lobCtx, const XmlCtx* xmlCtx, const DbTable* table,
                                    typeObj obj, typeDataObj dataObj, typeDba bdba, typeSlot slot, FileOffset fileOffset) {
        if (newTran)
            processBeginMessage(sequence, timestamp);

        if (format.isMessageFormatFull()) {
            comma(hasPreviousRedo);
        } else {
            builderBegin(sequence, scn, obj, BuilderMsg::OUTPUT_BUFFER::NONE);
            addTagData(lobCtx, xmlCtx, table, Format::VALUE_TYPE::AFTER, fileOffset);

            append('{');
            hasPreviousValue = false;
            appendHeader(scn, timestamp, false, format.isDbFormatAddDml(), true, format.isUserTypeDml());

            comma(hasPreviousValue);
            if (format.isAttributesFormatDml())
                appendAttributes();

            append(std::string_view(R"("payload":[)"));
        }

        append(std::string_view(R"({"op":"c",)"));
        if (format.isMessageFormatAddOffset()) {
            append(std::string_view(R"("offset":)"));
            appendDec(fileOffset.getData());
            append(',');
        }
        appendSchema(table, obj);
        appendRowid(dataObj, bdba, slot);
        appendAfter(lobCtx, xmlCtx, table, fileOffset);
        append('}');

        if (!format.isMessageFormatFull()) {
            append(std::string_view("]}"));
            builderCommit();
        }
        ++num;
    }

    void BuilderJson::processUpdate(Seq sequence, Scn scn, Time timestamp, LobCtx* lobCtx, const XmlCtx* xmlCtx, const DbTable* table,
                                    typeObj obj, typeDataObj dataObj, typeDba bdba, typeSlot slot, FileOffset fileOffset) {
        if (newTran)
            processBeginMessage(sequence, timestamp);

        if (format.isMessageFormatFull()) {
            comma(hasPreviousRedo);
        } else {
            builderBegin(sequence, scn, obj, BuilderMsg::OUTPUT_BUFFER::NONE);
            addTagData(lobCtx, xmlCtx, table, Format::VALUE_TYPE::AFTER, fileOffset);

            append('{');
            hasPreviousValue = false;
            appendHeader(scn, timestamp, false, format.isDbFormatAddDml(), true, format.isUserTypeDml());

            comma(hasPreviousValue);
            if (format.isAttributesFormatDml())
                appendAttributes();

            append(std::string_view(R"("payload":[)"));
        }

        append(std::string_view(R"({"op":"u",)"));
        if (format.isMessageFormatAddOffset()) {
            append(std::string_view(R"("offset":)"));
            appendDec(fileOffset.getData());
            append(',');
        }
        appendSchema(table, obj);
        appendRowid(dataObj, bdba, slot);
        appendBefore(lobCtx, xmlCtx, table, fileOffset);
        appendAfter(lobCtx, xmlCtx, table, fileOffset);
        append('}');

        if (!format.isMessageFormatFull()) {
            append(std::string_view("]}"));
            builderCommit();
        }
        ++num;
    }

    void BuilderJson::processDelete(Seq sequence, Scn scn, Time timestamp, LobCtx* lobCtx, const XmlCtx* xmlCtx, const DbTable* table,
                                    typeObj obj, typeDataObj dataObj, typeDba bdba, typeSlot slot, FileOffset fileOffset) {
        if (newTran)
            processBeginMessage(sequence, timestamp);

        if (format.isMessageFormatFull()) {
            comma(hasPreviousRedo);
        } else {
            builderBegin(sequence, scn, obj, BuilderMsg::OUTPUT_BUFFER::NONE);
            addTagData(lobCtx, xmlCtx, table, Format::VALUE_TYPE::BEFORE, fileOffset);

            append('{');
            hasPreviousValue = false;
            appendHeader(scn, timestamp, false, format.isDbFormatAddDml(), true, format.isUserTypeDml());

            comma(hasPreviousValue);
            if (format.isAttributesFormatDml())
                appendAttributes();

            append(std::string_view(R"("payload":[)"));
        }

        append(std::string_view(R"({"op":"d",)"));
        if (format.isMessageFormatAddOffset()) {
            append(std::string_view(R"("offset":)"));
            appendDec(fileOffset.getData());
            append(',');
        }
        appendSchema(table, obj);
        appendRowid(dataObj, bdba, slot);
        appendBefore(lobCtx, xmlCtx, table, fileOffset);
        append('}');

        if (!format.isMessageFormatFull()) {
            append(std::string_view("]}"));
            builderCommit();
        }
        ++num;
    }

    void BuilderJson::processDdl(Seq sequence, Scn scn, Time timestamp, const DbTable* table, typeObj obj) {
        if (newTran)
            processBeginMessage(sequence, timestamp);

        if (format.isMessageFormatFull()) {
            comma(hasPreviousRedo);
        } else {
            builderBegin(sequence, scn, obj, BuilderMsg::OUTPUT_BUFFER::NONE);
            append('{');
            hasPreviousValue = false;
            appendHeader(scn, timestamp, false, format.isDbFormatAddDdl(), true, format.isUserTypeDdl());

            comma(hasPreviousValue);
            if (format.isAttributesFormatDml())
                appendAttributes();

            append(std::string_view(R"("payload":[)"));
        }

        append(std::string_view(R"({"op":"ddl",)"));
        appendSchema(table, obj);
        append(std::string_view(R"(,"sql":")"));
        if (ddlFirst != nullptr) {
            uint8_t* chunk = ddlFirst;
            while (chunk != nullptr) {
                uint8_t* chunkNext = *reinterpret_cast<uint8_t**>(chunk);
                const typeTransactionSize* chunkSize = reinterpret_cast<typeTransactionSize*>(chunk + sizeof(uint8_t*));
                const uint8_t* chunkData = chunk + sizeof(uint8_t*) + sizeof(uint64_t);
                appendEscape(reinterpret_cast<const char*>(chunkData), *chunkSize);
                chunk = chunkNext;
            }
        }
        append(std::string_view(R"("})"));

        if (!format.isMessageFormatFull()) {
            append(std::string_view("]}"));
            builderCommit();
        }
        ++num;
    }

    void BuilderJson::processCheckpoint(Seq sequence, Scn scn, Time timestamp, FileOffset fileOffset, bool redo) {
        if (lwnScn != scn) {
            lwnScn = scn;
            lwnIdx = 0;
        }

        auto flags = BuilderMsg::OUTPUT_BUFFER::CHECKPOINT;
        if (redo)
            flags = static_cast<BuilderMsg::OUTPUT_BUFFER>(static_cast<uint>(flags) | static_cast<uint>(BuilderMsg::OUTPUT_BUFFER::REDO));
        builderBegin(sequence, scn, 0, flags);
        append('{');
        hasPreviousValue = false;
        appendHeader(scn, timestamp, true, false, false, false);

        comma(hasPreviousValue);
        append(std::string_view(R"("payload":[{"op":"chkpt","seq":)"));
        appendDec(sequence.getData());
        append(std::string_view(R"(,"offset":)"));
        appendDec(fileOffset.getData());
        if (redo)
            append(std::string_view(R"(,"redo":true)"));
        append(std::string_view("}]}"));
        builderCommit();
    }

    void BuilderJson::addTagData(LobCtx* lobCtx, const XmlCtx* xmlCtx, const DbTable* table, Format::VALUE_TYPE valueType, FileOffset fileOffset) {
        if (unlikely(table == nullptr || table->tagCols.empty()))
            return;

        const uint64_t messagePositionOld = messagePosition;
        hasPreviousColumn = false;
        for (uint x = 0; x < table->tagCols.size(); ++x) {
            const typeCol column = table->tagCols[x] - 1;
            if (values[column][static_cast<uint>(valueType)] == nullptr)
                continue;

            if (sizes[column][static_cast<uint>(valueType)] > 0)
                processValue(lobCtx, xmlCtx, table, column, values[column][static_cast<uint>(valueType)], sizes[column][static_cast<uint>(valueType)],
                             fileOffset, true, compressedAfter);
            else
                columnNull(table, column, true);
        }

        if (messagePosition >= messagePositionOld)
            msg->tagSize = messagePosition - messagePositionOld;
        else
            msg->tagSize = messageSize + messagePosition;
    }

    namespace {
        std::string jsonEscape(const std::string& str) {
            std::ostringstream ss;
            Data::writeEscapeValue(ss, str);
            return ss.str();
        }

        int timestampScale(int scale) {
            if (scale < 0 || scale > 9)
                return 6;
            return scale;
        }

        // Maps an Oracle column to its DDL type clause.
        std::string oracleColumnType(const DbColumn* column) {
            const uint length = column->length;
            const int precision = column->precision;
            const int scale = column->scale;
            switch (column->type) {
                case SysCol::COLTYPE::VARCHAR: return "VARCHAR2(" + std::to_string(length > 0 ? length : 4000) + ")";
                case SysCol::COLTYPE::CHAR: return "CHAR(" + std::to_string(length > 0 ? length : 1) + ")";
                case SysCol::COLTYPE::NUMBER:
                    if (precision > 0) {
                        if (scale != 0)
                            return "NUMBER(" + std::to_string(precision) + "," + std::to_string(scale) + ")";
                        return "NUMBER(" + std::to_string(precision) + ")";
                    }
                    return "NUMBER";
                case SysCol::COLTYPE::LONG: return "LONG";
                case SysCol::COLTYPE::DATE: return "DATE";
                case SysCol::COLTYPE::RAW: return "RAW(" + std::to_string(length > 0 ? length : 2000) + ")";
                case SysCol::COLTYPE::LONG_RAW: return "LONG RAW";
                case SysCol::COLTYPE::FLOAT: return "BINARY_FLOAT";
                case SysCol::COLTYPE::DOUBLE: return "BINARY_DOUBLE";
                case SysCol::COLTYPE::CLOB: return "CLOB";
                case SysCol::COLTYPE::BLOB: return "BLOB";
                case SysCol::COLTYPE::TIMESTAMP: return "TIMESTAMP(" + std::to_string(timestampScale(scale)) + ")";
                case SysCol::COLTYPE::TIMESTAMP_WITH_TZ: return "TIMESTAMP(" + std::to_string(timestampScale(scale)) + ") WITH TIME ZONE";
                case SysCol::COLTYPE::TIMESTAMP_WITH_LOCAL_TZ:
                    return "TIMESTAMP(" + std::to_string(timestampScale(scale)) + ") WITH LOCAL TIME ZONE";
                case SysCol::COLTYPE::INTERVAL_YEAR_TO_MONTH: return "INTERVAL YEAR TO MONTH";
                case SysCol::COLTYPE::INTERVAL_DAY_TO_SECOND: return "INTERVAL DAY TO SECOND";
                case SysCol::COLTYPE::UROWID: return "UROWID";
                default: return "VARCHAR2(4000)";
            }
        }
    }

    std::string BuilderJson::buildInitialSchemaMessage(const DbTable* table, Scn scn, const std::string& db) {
        if (table == nullptr)
            return {};

        std::string ddl = "CREATE TABLE \"" + table->owner + "\".\"" + table->name + "\" (";

        bool first = true;
        for (const DbColumn* column : table->columns) {
            if (column == nullptr)
                continue;
            if (column->hidden || column->nested || column->unused || column->guard || column->storedAsLob)
                continue;

            if (!first)
                ddl += ", ";
            first = false;

            ddl += "\"" + column->name + "\" " + oracleColumnType(column);
            if (!column->nullable)
                ddl += " NOT NULL";
        }

        // Without any captured column the DDL would be invalid; skip such tables.
        if (first)
            return {};

        // Primary key (required for an incremental snapshot to run against the table).
        std::string pkCols;
        for (const typeCol idx : table->pk) {
            if (idx < 0 || idx >= static_cast<typeCol>(table->columns.size()))
                continue;
            const DbColumn* column = table->columns[idx];
            if (column == nullptr)
                continue;
            if (!pkCols.empty())
                pkCols += ", ";
            pkCols += "\"" + column->name + "\"";
        }
        if (!pkCols.empty())
            ddl += ", PRIMARY KEY (" + pkCols + ")";

        ddl += ")";

        const int64_t tmMs = static_cast<int64_t>(time(nullptr)) * 1000;
        const std::string scnStr = std::to_string(scn.getData());

        std::string out = R"({"scn":")" + scnStr + R"(","tm":)" + std::to_string(tmMs) +
                          R"(,"c_scn":")" + scnStr + R"(","c_idx":0)";
        if (!db.empty())
            out += R"(,"db":")" + jsonEscape(db) + "\"";
        out += R"(,"payload":[{"op":"ddl","schema":{"owner":")" + jsonEscape(table->owner) +
               R"(","table":")" + jsonEscape(table->name) + R"(","obj":)" + std::to_string(table->obj) + "}";
        out += R"(,"sql":")" + jsonEscape(ddl) + "\"}]}";
        return out;
    }
}
