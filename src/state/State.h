/* Header for State class
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

#ifndef STATE_H_
#define STATE_H_

#include <set>

#include "../common/types/Types.h"

/**
 * @file State.h
 * @brief Header file for the State class hierarchy
 *
 * This file contains the declaration of the State base class and its derived
 * StateDisk class, which provide interfaces for managing persistent state data
 * in different storage backends.
 */

/**
 * @namespace OpenLogReplicator
 * @brief Main namespace for OpenLogReplicator components
 */
namespace OpenLogReplicator {
    class Ctx;

    /**
     * @class State
     * @brief Abstract base class for state management
     *
     * The State class provides an abstract interface for managing persistent state
     * data. It defines methods for listing, reading, writing, and dropping state
     * entries. The actual implementation of these operations is provided by
     * derived classes such as StateDisk.
     *
     * This class serves as the foundation for different state storage mechanisms
     * in OpenLogReplicator, allowing for flexible backend implementations while
     * maintaining a consistent interface.
     *
     * @note This is an abstract base class and cannot be instantiated directly.
     *
     * @see StateDisk
     */
    class State {
    protected:
        /**
         * @brief Context pointer for accessing global application context
         *
         * Pointer to the global context object that provides access to logging,
         * configuration, and other global services required by state operations.
         */
        Ctx* ctx;

    public:
        /**
         * @brief Constant representing disk-based state storage type
         *
         * This constant identifies the disk-based state storage type. It is used
         * in contexts where different state storage backends need to be distinguished.
         * Currently only TYPE_DISK is defined, but the design allows for extension
         * to other storage types.
         */
        static constexpr uint64_t TYPE_DISK{0};


        /**
         * @brief Constructor for State class
         *
         * Initializes the State object with a reference to the global context.
         *
         * @param newCtx Pointer to the global context object to be used by this state manager
         */
        explicit State(Ctx* newCtx);

        /**
         * @brief Virtual destructor for State class
         *
         * Provides proper cleanup for derived classes through virtual inheritance.
         * This ensures that destructors of derived classes are called correctly
         * when deleting objects through base class pointers.
         */
        virtual ~State() = default;

        /**
         * @brief List all available state entries
         *
         * Populates the provided set with names of all available state entries.
         * This method is used to enumerate all stored state data for management
         * and monitoring purposes.
         *
         * @param namesList Reference to a set that will be populated with state entry names
         *
         * @note The implementation should ensure thread safety if concurrent access
         *       to the state store is possible.
         */
        virtual void list(std::set<std::string>& namesList) const = 0;


        /**
         * @brief Read a state entry from storage
         *
         * Reads the content of a state entry identified by name into the provided
         * string buffer. The operation respects the maximum size limit specified.
         *
         * @param name Name of the state entry to read
         * @param maxSize Maximum allowed size of the state entry in bytes
         * @param in Reference to string where the read content will be stored
         * @return true if the read operation was successful, false otherwise
         *
         * @throws RuntimeException if the file size exceeds the maximum allowed size
         *         or if there are issues with file access or reading.
         *
         * @note The implementation should handle cases where the requested file
         *       doesn't exist or has invalid size constraints.
         */
        [[nodiscard]] virtual bool read(const std::string& name, uint64_t maxSize, std::string& in) = 0;

        /**
         * @brief Write a state entry to storage
         *
         * Writes the content of a state entry identified by name to persistent storage.
         * The SCN (System Change Number) parameter is included for potential use in
         * transactional consistency, though it may be unused in some implementations.
         *
         * @param name Name of the state entry to write
         * @param scn System Change Number associated with this state change
         * @param out String stream containing the content to write
         *
         * @throws RuntimeException if there are issues with file creation, opening,
         *         or writing operations.
         *
         * @note The implementation typically creates a new file or overwrites an
         *       existing one with the same name, appending a .json extension.
         */
        virtual void write(const std::string& name, Scn scn, const std::ostringstream& out) = 0;

        /**
         * @brief Drop/delete a state entry from storage
         *
         * Removes a state entry identified by name from persistent storage.
         * This operation permanently deletes the state data.
         *
         * @param name Name of the state entry to delete
         *
         * @throws RuntimeException if the deletion fails due to file system errors
         *         or permission issues.
         *
         * @note The implementation should handle cases where the target file
         *       doesn't exist gracefully.
         */
        virtual void drop(const std::string& name) = 0;
    };
}

#endif
