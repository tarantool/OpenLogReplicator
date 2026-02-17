/* Header for Replicator class
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

#ifndef REPLICATOR_H_
#define REPLICATOR_H_

#include <queue>
#include <set>
#include <vector>

#include "../common/Ctx.h"
#include "../common/Thread.h"
#include "../common/types/Seq.h"

namespace OpenLogReplicator {
    class Parser;
    class Builder;
    class Metadata;
    class Reader;
    class RedoLogRecord;
    class State;
    class Transaction;
    class TransactionBuffer;

    struct parserCompare {
        bool operator()(const Parser* p1, const Parser* p2) const;
    };

    /**
     * @brief Main replicator class responsible for handling Oracle redo logs replication.
     *
     * The Replicator class manages the replication process from Oracle database redo logs.
     * It handles both online and archived redo logs, manages transaction buffering,
     * and coordinates with various components like `parsers`, `builders`, and `metadata handlers`.
     *
     * Key responsibilities include:
     * - Managing redo log file processing (online and archived)
     * - Handling transaction buffering and replay
     * - Maintaining connection to Oracle database
     * - Processing schema changes and updates
     * - Managing file path mappings for log files
     * - Coordinating with different reader implementations
     *
     * The class inherits from Thread and implements the main replication loop.
     */
    class Replicator : public Thread {
    protected:
        /**
         * @brief Function pointer for archive log retrieval callback
         *
         * This function pointer is called to handle archived redo log processing.
         * It's initialized during construction and allows flexible implementation
         * of archive log handling strategies.
         */
        void (*archGetLog)(Replicator* replicator);

        /**
         * @brief Builder instance for constructing output data
         *
         * The builder is responsible for formatting and building the output data
         * that represents the replicated transactions. This includes converting
         * Oracle redo log records into appropriate output formats.
         */
        Builder* builder;

        /**
         * @brief Metadata handler for database schema information
         *
         * Manages metadata about the database schema including table definitions,
         * column information, and other schema-related data that's needed for
         * proper replication and output generation.
         */
        Metadata* metadata;

        /**
         * @brief Transaction buffer for managing transaction data
         *
         * Handles buffering of transaction data before it's processed or written out.
         * This helps in managing the flow of transaction data and ensures proper
         * ordering and consistency during replication.
         */
        TransactionBuffer* transactionBuffer;

        /**
         * @brief Database name identifier
         *
         * Stores the name of the Oracle database being replicated. Used for
         * identification and logging purposes.
         */
        std::string database;

        /**
         * @brief Path for redo copy operations
         *
         * Specifies the location where redo log copies are stored during replication
         * operations. This may be used for backup or temporary storage of redo logs.
         */
        std::string redoCopyPath;

        /**
         * @brief Reader for archived redo logs
         *
         * Pointer to the reader instance that handles archived redo log files.
         * May be null if no archived logs are being processed.
         */
        // Redo log files
        Reader* archReader{nullptr};

        /**
         * @brief Last checked day for archive log processing
         *
         * Tracks the last day that was checked for archive log availability.
         * Used to avoid redundant checks and optimize archive log processing.
         */
        std::string lastCheckedDay;

        /**
         * @brief Priority queue for archived redo log processing
         *
         * A priority queue containing Parser instances for archived redo logs.
         * Logs are prioritized based on their sequence numbers to ensure correct
         * processing order.
         */
        std::priority_queue<Parser*, std::vector<Parser*>, parserCompare> archiveRedoQueue;

        /**
         * @brief Set of online redo log parsers
         *
         * Contains Parser instances for currently active online redo logs.
         * These logs are continuously written to by Oracle and need constant monitoring.
         */
        std::set<Parser*> onlineRedoSet;

        /**
         * @brief Set of active readers
         *
         * Maintains a collection of Reader instances that are actively processing
         * redo log files. This allows management of multiple concurrent readers.
         */
        std::set<Reader*> readers;

        /**
         * @brief Path mapping configuration
         *
         * Vector of path mappings that allow remapping of source paths to target paths
         * for redo log files. This is useful when log files are stored in different
         * locations than expected.
         */
        std::vector<std::string> pathMapping;

        /**
         * @brief Batch of redo log paths
         *
         * Vector storing paths to redo log files that are processed in batches.
         * This helps in organizing and processing multiple redo logs efficiently.
         */
        std::vector<std::string> redoLogsBatch;

        /**
         * @brief Cleans up the archive redo log list
         *
         * Removes outdated or unnecessary entries from the archive redo log list.
         * This helps in maintaining efficient memory usage and avoiding processing
         * of obsolete log files.
         */
        void cleanArchList();

        /**
         * @brief Updates online redo log information
         *
         * Refreshes the status and information about currently active online redo logs.
         * This ensures that the replicator has up-to-date information about the
         * online redo log structure and content.
         */
        void updateOnlineLogs() const;

        /**
         * @brief Drops all active readers
         *
         * Closes and releases all active reader instances. This is typically called
         * during cleanup or shutdown operations to properly dispose of resources.
         */
        void readerDropAll();

        /**
         * @brief Extracts sequence number from file name
         *
         * Parses a redo log file name to extract the sequence number component.
         * This is essential for proper ordering and processing of redo log files.
         *
         * @param replicator Pointer to the replicator instance
         * @param file The file name to parse
         * @return Sequence number extracted from the file name
         */
        static Seq getSequenceFromFileName(const Replicator* replicator, const std::string& file);

        /**
         * @brief Gets the name of the replication mode
         *
         * Returns a string representation of the current replication mode.
         * This is used for logging and identification purposes.
         *
         * @return String representing the replication mode
         */
        virtual std::string getModeName() const;

        /**
         * @brief Checks database connection status
         *
         * Verifies that the connection to the Oracle database is still valid.
         * This is important for ensuring continuous replication without interruption.
         *
         * @return True if connection is valid, false otherwise
         */
        virtual bool checkConnection();

        /**
         * @brief Determines whether to continue with online log processing
         *
         * Evaluates whether the replication process should continue with online log
         * processing based on current conditions and configuration.
         *
         * @return True to continue with online logs, false otherwise
         */
        virtual bool continueWithOnline();

        /**
         * @brief Verifies database schema compatibility
         *
         * Checks that the current schema matches expectations and performs necessary
         * validation for schema changes.
         *
         * @param currentScn Current system change number to validate against
         */
        virtual void verifySchema(Scn currentScn);

        /**
         * @brief Creates initial schema definition
         *
         * Initializes the schema definition for the database. This is typically
         * called when starting replication for the first time or when schema
         * information needs to be established.
         */
        virtual void createSchema();

        /**
         * @brief Updates online redo log data
         *
         * Refreshes information about online redo log files and their current status.
         * This helps maintain accurate knowledge of the active redo log structure.
         */
        virtual void updateOnlineRedoLogData();

    public:
        Replicator(Ctx* newCtx, void (*newArchGetLog)(Replicator* replicator), Builder* newBuilder, Metadata* newMetadata,
                   TransactionBuffer* newTransactionBuffer, std::string newAlias, std::string newDatabase);
        ~Replicator() override;

        /**
         * @brief Initializes the replicator instance
         *
         * Performs initialization tasks required before the replicator can begin
         * processing redo logs. This includes setting up internal structures,
         * initializing components such as builders and metadata handlers,
         * and preparing for log file processing.
         *
         * This method is intended to be called once during the setup phase of
         * the replicator lifecycle and should not be invoked more than once.
         *
         * @throws Exception if initialization fails due to missing dependencies
         *                   or invalid configuration settings.
         */
        virtual void initialize();

        /**
         * @brief Positions the reader to the appropriate starting point in the redo log stream.
         *
         * This method determines the correct position within the redo log files to start
         * reading from. It considers the current state of the replicator, including any
         * previously processed SCN (System Change Number) and ensures that the reader
         * begins at the right location to resume or initiate replication.
         *
         * The positioning logic may involve checking the last known SCN, verifying
         * the availability of redo log files, and adjusting the reader's position accordingly.
         * This is crucial for maintaining consistency and avoiding duplicate or missed records.
         *
         * @note This method is typically called during initialization or after a restart
         *       to ensure that the replication process resumes from the correct point.
         */
        virtual void positionReader();

        /**
         * @brief Loads database metadata into the replicator.
         *
         * This method retrieves and loads the complete metadata of the Oracle database
         * into the replicator's internal structures. It ensures that all schema information,
         * including tables, columns, indexes, constraints, and other relevant metadata,
         * is available for processing and replication.
         *
         * The metadata loading process involves querying the database catalog views
         * and populating internal data structures that are used during the replication
         * process. This step is critical for accurately interpreting redo log records
         * and generating correct output data.
         *
         * @note This method should be called after the initial connection to the database
         *       is established and before the replication process begins. It is typically
         *       invoked during the initialization phase of the replicator.
         *
         * @throws Exception if metadata loading fails due to database access issues,
         *                   invalid schema definitions, or unexpected errors during parsing.
         */
        virtual void loadDatabaseMetadata();

        /**
         * @brief Main execution loop of the replicator thread.
         *
         * This method implements the core replication loop, which continuously monitors
         * and processes redo log files. It orchestrates the interaction between
         * online and archived redo log processing, manages transaction buffering,
         * and ensures that data is correctly replicated from the source Oracle database
         * to the configured output destination.
         *
         * The loop typically follows these steps:
         * 1. Processes online redo logs for real-time changes.
         * 2. Handles archived redo logs if available.
         * 3. Updates internal state and metadata.
         * 4. Ensures proper synchronization and error handling.
         *
         * @note This method is automatically invoked when the thread starts and runs
         *       until the replicator is explicitly stopped or an unrecoverable error occurs.
         */
        void run() override;

        /**
         * @brief Creates a new reader instance for a specific redo log group.
         *
         * This virtual method is responsible for creating and returning a new Reader
         * instance that will be used to read redo log files belonging to a particular
         * group. The group parameter identifies the type or category of redo logs
         * (e.g., online vs archived) that this reader will handle.
         *
         * Implementations of this method should return a properly initialized Reader
         * object capable of reading the specified redo log group. This allows for
         * flexible and extensible reader creation based on the type of redo logs
         * being processed.
         *
         * @param group Identifier for the redo log group to create a reader for.
         *              This value is typically used to determine the appropriate
         *              reader type or configuration.
         * @return A pointer to the newly created Reader instance, or nullptr if
         *         creation fails or the group is unsupported.
         */
        virtual Reader* readerCreate(int group);

        /**
         * @brief Checks the status and validity of online redo logs.
         *
         * This method verifies the current state of online redo logs, ensuring they
         * are accessible and valid for processing. It performs checks such as:
         * - Validity of log file paths
         * - Presence of required log files
         * - Consistency of log sequence numbers
         * - Proper alignment with the current SCN
         *
         * This check is performed periodically to maintain the integrity of the
         * replication process and prevent issues caused by corrupted or missing
         * online redo logs.
         *
         * @note This method is typically invoked during the main replication loop
         *       to ensure ongoing health of the online redo log processing pipeline.
         */
        void checkOnlineRedoLogs();

        /**
         * @brief Switches the replicator to standby mode.
         *
         * This method transitions the replicator into standby mode, which typically
         * involves pausing active processing of redo logs and preparing for a potential
         * failover or maintenance operation. In standby mode, the replicator may
         * continue to monitor log files but does not actively process or replicate
         * changes until switched back to active mode.
         *
         * Standby mode is often used in high availability setups where the replicator
         * might temporarily stop processing to allow for maintenance or to prepare
         * for taking over replication duties from another node.
         *
         * @note This method should be called when transitioning the replicator to
         *       a passive state. It ensures that all active processes are paused
         *       and resources are appropriately managed.
         */
        virtual void goStandby();

        /**
         * @brief Adds a path mapping for redo log files.
         *
         * This method adds a new path mapping to the internal vector of path mappings.
         * It allows users to specify a mapping from a source path to a target path for
         * redo log files. This is particularly useful when the actual location of redo
         * log files differs from what the replicator expects, enabling flexible handling
         * of log file locations.
         *
         * @param source The original path of the redo log file.
         * @param target The desired path where the redo log file should be accessed.
         */
        void addPathMapping(std::string source, std::string target);

        /**
         * @brief Adds a redo log file path to the batch processing list.
         *
         * This method appends a given redo log file path to the internal batch vector
         * of redo log paths. The batched paths are used for processing multiple redo
         * logs together, which can improve efficiency and reduce overhead during
         * initialization or log file discovery phases.
         *
         * @param path The full path to the redo log file to be added to the batch.
         */
        void addRedoLogsBatch(std::string path);

        /**
         * @brief Callback function to retrieve archived redo log files by path.
         *
         * This static method serves as a callback for retrieving archived redo log files
         * based on a specified path. It is typically used in conjunction with the
         * `archGetLog` function pointer to provide a mechanism for fetching archived
         * logs from a defined location. The implementation may involve scanning directories,
         * identifying eligible log files, and adding them to the processing queue.
         *
         * @param replicator Pointer to the Replicator instance that invokes this callback.
         *                   This allows access to internal state and methods for managing
         *                   the archive log processing workflow.
         */
        static void archGetLogPath(Replicator* replicator);

        /**
         * @brief Callback function to retrieve archived redo log files from a list.
         *
         * This static method serves as a callback for retrieving archived redo log files
         * based on a pre-defined list of paths. It is used in conjunction with the
         * `archGetLog` function pointer to fetch archived logs from a provided list
         * of file paths. This approach is suitable when the paths to archived logs
         * are already known and managed externally.
         *
         * The method iterates through the list of paths, creates Parser instances for
         * each valid log file, and adds them to the archive redo log processing queue.
         * It ensures that the logs are processed in order of their sequence numbers.
         *
         * @param replicator Pointer to the Replicator instance that invokes this callback.
         *                   This allows access to internal state and methods for managing
         *                   the archive log processing workflow.
         */
        static void archGetLogList(Replicator* replicator);

        /**
         * @brief Applies path mapping to a given path.
         *
         * This method takes a path string and applies any configured path mappings
         * to transform it according to the user-defined rules. It searches through
         * the internal `pathMapping` vector to find a matching source path and
         * replaces it with the corresponding target path.
         *
         * This functionality is essential for handling scenarios where redo log
         * files are located in different directories than expected by the replicator,
         * allowing flexible configuration without requiring changes to the underlying
         * file system structure.
         *
         * @param path Reference to the path string to be mapped. The string is modified
         *             in-place to reflect the result of the mapping operation.
         */
        void applyMapping(std::string& path) const;

        /**
         * @brief Updates resetlogs information for the replicator.
         *
         * This method is responsible for updating internal state related to resetlogs
         * events in the Oracle database. Resetlogs occur when a database is opened
         * with the RESETLOGS option, which resets the online redo log sequence numbers
         * and can affect how redo logs are interpreted and processed.
         *
         * The method ensures that the replicator's internal tracking of log sequences,
         * SCNs, and other resetlogs-specific metadata is synchronized with the current
         * state of the database. This is crucial for maintaining accurate replication
         * continuity after a resetlogs event.
         *
         * @note This method is typically invoked during initialization or when a resetlogs
         *       event is detected during log processing to ensure proper handling of
         *       subsequent redo log entries.
         */
        void updateResetlogs();

        /**
         * @brief Wakes up the replicator thread to process pending tasks.
         *
         * This method is used to signal the replicator thread to wake up from its
         * sleeping state and re-evaluate its processing tasks. It is typically invoked
         * when there are new redo log files to process, or when external events require
         * immediate attention from the replicator.
         *
         * The wake-up mechanism ensures that the replicator responds promptly to
         * changes in the redo log environment, such as new archived logs becoming
         * available or online log switches occurring.
         *
         * @note This method is part of the Thread interface and is overridden to
         *       provide custom behavior for waking up the replicator thread.
         */
        void wakeUp() override;

        /**
         * @brief Prints a startup message to the console or log.
         *
         * This method outputs a formatted message indicating that the replicator
         * has started successfully. It typically includes information such as the
         * database name, alias, and replication mode to aid in debugging and monitoring.
         *
         * The message is logged using the context's logging facilities and provides
         * a clear indication that the replicator is operational and ready to process
         * redo log files.
         *
         * @note This method is usually called during the initialization phase of
         *       the replicator to confirm successful startup.
         */
        void printStartMsg() const;

        /**
         * @brief Processes archived redo logs from the queue.
         *
         * This method processes archived redo logs that have been placed in the
         * archive redo log processing queue (`archiveRedoQueue`). It iteratively
         * retrieves and processes each log entry in order of their sequence numbers,
         * ensuring that the logs are applied in the correct chronological order.
         *
         * The method continues processing until the queue is empty or an error occurs.
         * During processing, it may update internal state, handle transaction boundaries,
         * and notify the builder or metadata handler of changes.
         *
         * @return true if all archived logs were processed successfully, false otherwise.
         *
         * @note This method is typically called from the main replication loop (`run`)
         *       to handle archived redo logs that are available for processing.
         */
        bool processArchivedRedoLogs();

        /**
         * @brief Processes online redo logs for real-time transaction replication.
         *
         * This method handles the continuous processing of online redo logs, which
         * contain real-time changes made to the Oracle database. It iterates through
         * the set of active online redo log parsers (`onlineRedoSet`) and processes
         * each one to extract and interpret redo log records.
         *
         * The method ensures that changes are captured in the correct order and
         * buffered appropriately for further processing or output generation.
         * It also manages the lifecycle of online redo log parsers, including
         * detecting log switches and updating internal state accordingly.
         *
         * This is a core part of the replication loop and is typically invoked
         * repeatedly during normal operation to keep up with live database changes.
         *
         * @return true if online redo logs were processed successfully, false otherwise.
         *
         * @note This method is called internally by the main replication loop (`run`)
         *       and should not be invoked directly from external code.
         */
        bool processOnlineRedoLogs();

        friend class OpenLogReplicator;
        friend class ReplicatorOnline;

        std::string getName() const override {
            return {"Replicator: " + alias};
        }
    };
}

#endif
