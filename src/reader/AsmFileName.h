/* ASM file-name parsing helpers
   Copyright (C) 2025 OpenLogReplicator

This file is part of OpenLogReplicator.

OpenLogReplicator is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 3, or (at your option)
any later version.

OpenLogReplicator is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenLogReplicator; see the file LICENSE;  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef ASM_FILE_NAME_H_
#define ASM_FILE_NAME_H_

#include <cstdint>
#include <string>

namespace OpenLogReplicator {

    // Extract file# from a system name "tag.file#.incarnation#" (e.g. "group_4.267.1224328757").
    // Returns false for user aliases (e.g. "redo01.log"), which carry no embedded file number.
    inline bool parseAsmFileNumber(const std::string& fileNameOnly, uint32_t& fileNumber) {
        const size_t lastDot = fileNameOnly.rfind('.');
        if (lastDot == std::string::npos || lastDot == 0)
            return false;
        const size_t prevDot = fileNameOnly.rfind('.', lastDot - 1);
        if (prevDot == std::string::npos || prevDot == 0)
            return false;
        const std::string fileNumStr = fileNameOnly.substr(prevDot + 1, lastDot - prevDot - 1);
        const std::string incStr = fileNameOnly.substr(lastDot + 1);
        if (fileNumStr.empty() || incStr.empty())
            return false;
        if (fileNumStr.find_first_not_of("0123456789") != std::string::npos ||
            incStr.find_first_not_of("0123456789") != std::string::npos)
            return false;
        try {
            fileNumber = static_cast<uint32_t>(std::stoul(fileNumStr));
        } catch (...) {
            return false;
        }
        return true;
    }
}

#endif
