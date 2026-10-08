/* Header for ReplicatorOnline class
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

#ifndef REPLICATOR_ONLINE_H_
#define REPLICATOR_ONLINE_H_

#include "Replicator.h"
#include "../common/DbTable.h"
#include "../metadata/SchemaElement.h"

namespace OpenLogReplicator {
    class DatabaseConnection;
    class DatabaseEnvironment;
    class Schema;

    class ReplicatorOnline : public Replicator {
    protected:
        /**
         * @brief SQL query to retrieve the list of archived logs.
         *
         * This constant defines an SQL statement used to fetch information about archived redo logs
         * from the Oracle database. It selects the log name, sequence number, first change number,
         * and next change number. The query filters logs based on a minimum sequence number,
         * resetlogs identifier, and ensures that the log name is not null. Remote standby destinations
         * (STANDBY_DEST = 'YES') are skipped, as their NAME holds a TNS descriptor, not a file path.
         * Results are ordered by sequence number, destination ID, and recovery destination status.
         *
         * @note This query is intended for use in online replication scenarios where archived logs
         *       are required to reconstruct the redo log stream.
         */
        static constexpr std::string_view SQL_GET_ARCHIVE_LOG_LIST
        {
            "SELECT"
            "   NAME"
            ",  SEQUENCE#"
            ",  FIRST_CHANGE#"
            ",  NEXT_CHANGE#"
            " FROM"
            "   SYS.V_$ARCHIVED_LOG"
            " WHERE"
            "   SEQUENCE# >= :i"
            "   AND RESETLOGS_ID = :j"
            "   AND NAME IS NOT NULL"
            "   AND STANDBY_DEST = 'NO'"
            " ORDER BY"
            "   SEQUENCE#"
            ",  DEST_ID"
            ",  IS_RECOVERY_DEST_FILE DESC"
        };

        /**
         * @brief SQL query to retrieve essential database information.
         *
         * This constant defines an SQL statement used to fetch critical database properties
         * required for online replication. It retrieves information such as:
         * - Whether the database is running in ARCHIVELOG mode.
         * - Whether minimal supplemental logging is enabled.
         * - Whether primary key supplemental logging is enabled.
         * - Whether all column supplemental logging is enabled.
         * - The byte order (endianness) of the platform.
         * - The Oracle database banner string.
         * - The database name obtained via SYS_CONTEXT.
         * - The current system change number (SCN).
         * - The database timezone.
         *
         * The query joins the V_$DATABASE view with V_$TRANSPORTABLE_PLATFORM and V_$VERSION
         * to gather platform-specific and version-related data.
         *
         * @note This query is essential for validating database compatibility and configuration
         *       during the initialization phase of online replication.
         */
        static constexpr std::string_view SQL_GET_DATABASE_INFORMATION
        {
            "SELECT"
            "   DECODE(D.LOG_MODE, 'ARCHIVELOG', 1, 0)"
            ",  DECODE(D.SUPPLEMENTAL_LOG_DATA_MIN, 'NO', 0, 1)"
            ",  DECODE(D.SUPPLEMENTAL_LOG_DATA_PK, 'YES', 1, 0)"
            ",  DECODE(D.SUPPLEMENTAL_LOG_DATA_ALL, 'YES', 1, 0)"
            ",  DECODE(TP.ENDIAN_FORMAT, 'Big', 1, 0)"
            ",  VER.BANNER"
            ",  SYS_CONTEXT('USERENV','DB_NAME')"
            ",  CURRENT_SCN"
            ",  DBTIMEZONE"
            " FROM"
            "   SYS.V_$DATABASE D"
            " JOIN"
            "   SYS.V_$TRANSPORTABLE_PLATFORM TP ON"
            "     TP.PLATFORM_NAME = D.PLATFORM_NAME"
            " JOIN"
            "   SYS.V_$VERSION VER ON"
            "     VER.BANNER LIKE '%Oracle%Database%'"
        };

        /**
         * @brief SQL query to retrieve database incarnation information.
         *
         * This constant defines an SQL statement used to fetch information about the current
         * database incarnation from the Oracle database. It retrieves key attributes related to
         * the database incarnation such as:
         * - Incarnation number.
         * - Resetlogs change number.
         * - Prior resetlogs change number.
         * - Status of the incarnation.
         * - Resetlogs identifier.
         * - Prior incarnation number.
         *
         * This query is executed against the SYS.V_$DATABASE_INCARNATION view which provides
         * metadata about the logical structure of the database incarnation history.
         *
         * @note This query is used during the initialization and validation steps of online
         *       replication to ensure that the replicator is working with the correct database
         *       incarnation and to maintain consistency across log replay operations.
         */
        static constexpr std::string_view SQL_GET_DATABASE_INCARNATION
        {
            "SELECT"
            "   INCARNATION#"
            ",  RESETLOGS_CHANGE#"
            ",  PRIOR_RESETLOGS_CHANGE#"
            ",  STATUS"
            ",  RESETLOGS_ID"
            ",  PRIOR_INCARNATION#"
            " FROM"
            "   SYS.V_$DATABASE_INCARNATION"
        };

        /**
         * @brief SQL query to retrieve the database role.
         *
         * This constant defines an SQL statement used to determine the role of the Oracle database
         * instance. It queries the V_$DATABASE view to fetch the value of the DATABASE_ROLE column,
         * which indicates whether the database is a PRIMARY, PHYSICAL STANDBY, LOGICAL STANDBY,
         * or SNAPSHOT standby.
         *
         * This information is crucial for determining the appropriate replication strategy and
         * ensuring that the replicator operates correctly depending on the database role.
         *
         * @note This query is used during the initialization phase of online replication to
         *       validate the database role and prevent replication from starting in an incompatible
         *       configuration.
         */
        static constexpr std::string_view SQL_GET_DATABASE_ROLE
        {
            "SELECT"
            "   DATABASE_ROLE"
            " FROM"
            "   SYS.V_$DATABASE"
        };

        /**
         * @brief SQL query to retrieve the current system change number (SCN) of the database.
         *
         * This constant defines an SQL statement used to fetch the current system change number (SCN)
         * from the Oracle database. The SCN represents a unique, monotonically increasing number
         * that identifies a specific point in time within the database's transaction history.
         *
         * Retrieving this value is important for synchronizing replication activities and ensuring
         * that the replicator has the most up-to-date knowledge of the database state.
         *
         * @note This query is typically used during initialization or checkpointing phases to
         *       establish the baseline SCN for subsequent log analysis and replay.
         */
        static constexpr std::string_view SQL_GET_DATABASE_SCN
        {
            "SELECT"
            "   D.CURRENT_SCN"
            " FROM"
            "   SYS.V_$DATABASE D"
        };

        /**
         * @brief SQL query to retrieve container information in CDB environments.
         *
         * This constant defines an SQL statement used to fetch information about the current
         * container context in Oracle Container Database (CDB) environments. It retrieves:
         * - The current container ID (CON_ID).
         * - The current container name (CON_NAME).
         * - The CDB name (if available), otherwise falling back to the database name (DB_NAME).
         *
         * This query uses the DUAL table and SYS_CONTEXT functions to obtain metadata
         * about the session's container context, which is essential for distinguishing
         * between root and pluggable databases when operating in a multi-tenant environment.
         *
         * @note This query is particularly relevant when OpenLogReplicator is configured
         *       to operate in a CDB environment, allowing proper identification of the
         *       active container and enabling accurate replication of data across PDBs.
         */
        static constexpr std::string_view SQL_GET_CON_INFO
        {
            "SELECT"
            "   SYS_CONTEXT('USERENV','CON_ID')"
            ",  SYS_CONTEXT('USERENV','CON_NAME')"
            ",  NVL(SYS_CONTEXT('USERENV','CDB_NAME'), SYS_CONTEXT('USERENV','DB_NAME'))"
            ",  (SELECT P.DBID FROM SYS.V_$PDBS P WHERE P.CON_ID = SYS_CONTEXT('USERENV','CON_ID'))"
            " FROM"
            "   DUAL"
        };

        /**
         * @brief SQL query to convert a timestamp into a system change number (SCN).
         *
         * This constant defines an SQL statement used to convert a given timestamp into a
         * corresponding system change number (SCN) using Oracle's built-in function `TIMESTAMP_TO_SCN`.
         * The input timestamp is expected to be provided in the format 'YYYY-MM-DD HH24:MI:SS',
         * and it is passed as a bind variable (:i). The result of the conversion is returned
         * directly from the DUAL table.
         *
         * This functionality is primarily used in scenarios where historical SCN values need
         * to be determined based on a known timestamp. For example, it may be employed to
         * find the SCN at which a specific event occurred, aiding in point-in-time recovery
         * or replication targeting.
         *
         * @note This query assumes that the provided timestamp is valid and compatible with
         *       Oracle's internal date/time representation. It is intended to be used in
         *       contexts where the user supplies a precise timestamp to derive the associated
         *       SCN.
         */
        static constexpr std::string_view SQL_GET_SCN_FROM_TIME
                {"SELECT TIMESTAMP_TO_SCN(TO_DATE('YYYY-MM-DD HH24:MI:SS', :i) FROM DUAL"};

        /**
         * @brief SQL query to convert a relative time offset into a system change number (SCN).
         *
         * This constant defines an SQL statement used to convert a relative time offset (in seconds)
         * into a corresponding system change number (SCN) using Oracle's built-in function `TIMESTAMP_TO_SCN`.
         * The input time offset is specified in seconds and is subtracted from the current system time (SYSDATE),
         * effectively calculating an SCN at a past point in time. The value is passed as a bind variable (:i).
         * The result of the conversion is returned directly from the DUAL table.
         *
         * This functionality supports time-based replication scenarios where a specific point in time
         * needs to be translated into an SCN. It is especially useful for setting up replication
         * targets at a certain time in the past, enabling point-in-time recovery or historical data
         * replication.
         *
         * @note This query assumes that the provided time offset is a positive integer representing
         *       seconds in the past. It relies on Oracle's ability to interpret SYSDATE minus the
         *       time offset as a valid timestamp for SCN conversion.
         */
        static constexpr std::string_view SQL_GET_SCN_FROM_TIME_RELATIVE
                {"SELECT TIMESTAMP_TO_SCN(SYSDATE - (:i/24/3600)) FROM DUAL"};

        /**
         * @brief SQL query to determine the maximum sequence number for a given system change number (SCN).
         *
         * This constant defines an SQL statement used to find the highest sequence number (SEQ#) of either
         * online redo logs or archived logs that corresponds to a given system change number (SCN). It is
         * designed to assist in identifying the last log entry that contains changes up to and including
         * the specified SCN.
         *
         * The query performs a union of two subqueries:
         * 1. Retrieves sequence numbers from SYS.V_$LOG where the log's first change number is greater than
         *    or equal to the given SCN plus one (i.e., the log covers the SCN).
         * 2. Retrieves sequence numbers from SYS.V_$ARCHIVED_LOG where the log's first change number is
         *    greater than or equal to the given SCN plus one, and matches the resetlogs identifier.
         *
         * This is used during the initialization and log selection process to determine how far back
         * in the redo log chain we need to go to start reading from a particular SCN.
         *
         * @param i The system change number (SCN) to search for.
         * @param j The resetlogs identifier used to filter archived logs.
         *
         * @note This query is essential for log position tracking and ensuring that all necessary
         *       redo logs are included in the replication stream for a given SCN.
         */
        static constexpr std::string_view SQL_GET_SEQUENCE_FROM_SCN
        {
            "SELECT MAX(SEQUENCE#) FROM ("
            "  SELECT"
            "     SEQUENCE#"
            "   FROM"
            "     SYS.V_$LOG"
            "   WHERE"
            "     FIRST_CHANGE# - 1 <= :i"
            " UNION"
            "  SELECT"
            "     SEQUENCE#"
            "   FROM"
            "     SYS.V_$ARCHIVED_LOG"
            "   WHERE"
            "     FIRST_CHANGE# - 1 <= :i"
            "     AND RESETLOGS_ID = :j)"
        };

        /**
         * @brief SQL query to determine the maximum sequence number for a given system change number (SCN) in a standby environment.
         *
         * This constant defines an SQL statement used to find the highest sequence number (SEQ#) of either
         * standby redo logs or archived logs that corresponds to a given system change number (SCN). It is
         * specifically tailored for use in a physical standby database environment where standby redo logs
         * are present and potentially contain changes up to and including the specified SCN.
         *
         * The query performs a union of two subqueries:
         * 1. Retrieves sequence numbers from SYS.V_$STANDBY_LOG where the log's first change number is greater than
         *    or equal to the given SCN plus one (i.e., the log covers the SCN).
         * 2. Retrieves sequence numbers from SYS.V_$ARCHIVED_LOG where the log's first change number is
         *    greater than or equal to the given SCN plus one, and matches the resetlogs identifier.
         *
         * This query is used during the initialization and log selection process in standby configurations
         * to ensure that all relevant logs—both standby redo logs and archived logs—are considered when
         * determining the correct starting point for log replay.
         *
         * @param i The system change number (SCN) to search for.
         * @param j The resetlogs identifier used to filter archived logs.
         *
         * @note This query is essential for log position tracking in physical standby databases to ensure
         *       that the replicator correctly identifies the latest available logs for consistent replay.
         */
        static constexpr std::string_view SQL_GET_SEQUENCE_FROM_SCN_STANDBY
        {
            "SELECT MAX(SEQUENCE#) FROM ("
            "  SELECT"
            "     SEQUENCE#"
            "   FROM"
            "     SYS.V_$STANDBY_LOG"
            "   WHERE"
            "     FIRST_CHANGE# - 1 <= :i"
            " UNION"
            "  SELECT"
            "     SEQUENCE#"
            "   FROM"
            "     SYS.V_$ARCHIVED_LOG"
            "   WHERE"
            "     FIRST_CHANGE# - 1 <= :i"
            "     AND RESETLOGS_ID = :j)"
        };

        /**
         * @brief SQL query to retrieve the list of redo log files for a specified type.
         *
         * This constant defines an SQL statement used to fetch the list of redo log files
         * belonging to a specific group type from the Oracle database. It selects the group number
         * and member path of each log file, filtering by the specified log file type. The results
         * are ordered by group number, recovery destination status, and member path to provide a
         * consistent and predictable output.
         *
         * This query is used during the initialization and log management phases of online
         * replication to identify and track redo log files that are required for processing
         * redo log streams.
         *
         * @param i The type of the log file to retrieve (e.g., ONLINE or ARCHIVED).
         *
         * @note This query helps in identifying redo log files that are currently active or
         *       archived, which is essential for maintaining accurate log positions and
         *       facilitating log-based replication.
         */
        static constexpr std::string_view SQL_GET_LOGFILE_LIST
        {
            "SELECT"
            "   LF.GROUP#"
            ",  LF.MEMBER"
            " FROM"
            "   SYS.V_$LOGFILE LF"
            " WHERE"
            "   TYPE = :i"
            " ORDER BY"
            "   LF.GROUP# ASC"
            ",  LF.IS_RECOVERY_DEST_FILE DESC"
            ",  LF.MEMBER ASC"
        };

        /**
         * @brief SQL query to retrieve a parameter value from the database.
         *
         * This constant defines an SQL statement used to fetch the value of a specific database parameter
         * from the SYS.V_$PARAMETER view. The parameter name is passed as a bind variable (:i), and the
         * query returns only the VALUE column.
         *
         * This query is used during the initialization or runtime configuration checks to verify or
         * obtain settings like buffer sizes, parallelism options, or other Oracle parameters that
         * might affect replication behavior or performance.
         *
         * @param i The name of the parameter to retrieve.
         *
         * @note This query allows dynamic access to parameter values, which can be useful for adapting
         *       replication strategies based on current database settings.
         */
        static constexpr std::string_view SQL_GET_PARAMETER
        {
            "SELECT"
            "   VALUE"
            " FROM"
            "   SYS.V_$PARAMETER"
            " WHERE"
            "   NAME = :i"
        };

        /**
         * @brief SQL query to retrieve a property value from the database.
         *
         * This constant defines an SQL statement used to fetch the value of a specific database property
         * from the DATABASE_PROPERTIES view. The property name is passed as a bind variable (:i), and the
         * query returns only the PROPERTY_VALUE column.
         *
         * This query is used during the initialization or runtime configuration checks to verify or
         * obtain settings that are stored in the database properties table, such as NLS settings,
         * compatibility levels, or other custom properties relevant to replication behavior.
         *
         * @param i The name of the property to retrieve.
         *
         * @note This query enables access to database-wide properties that may influence
         *       replication logic or data handling, providing flexibility in configuring
         *       replication based on database-level settings.
         */
        static constexpr std::string_view SQL_GET_PROPERTY
        {
            "SELECT"
            "   PROPERTY_VALUE"
            " FROM"
            "   DATABASE_PROPERTIES"
            " WHERE"
            "   PROPERTY_NAME = :i"
        };

        /**
         * @brief SQL query to retrieve column information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed column information from the
         * system tables CCOL$ and OBJ$ for objects owned by a specific user. It selects ROWID, CON#, INTCOL#, OBJ#,
         * and decomposed parts of SPARE1 (SPARE11 and SPARE12) from the CCOL$ table joined with OBJ$.
         * The query filters records by the owner ID of the object and utilizes SCN-based flashback queries
         * to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * column definitions and metadata that are critical for accurate data replication.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on CCOL$.
         * @param k The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication, where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema.
         */
        static constexpr std::string_view SQL_GET_SYS_CCOL_USER
        {
            "SELECT"
            "   L.ROWID, L.CON#, L.INTCOL#, L.OBJ#, MOD(L.SPARE1, 18446744073709551616) AS SPARE11,"
            "   MOD(TRUNC(L.SPARE1 / 18446744073709551616), 18446744073709551616) AS SPARE12"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.CCOL$ AS OF SCN :j L ON"
            "     O.OBJ# = L.OBJ#"
            " WHERE"
            "   O.OWNER# = :k"
        };

        /**
         * @brief SQL query to retrieve column information for a specific object from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed column information from the
         * system table CCOL$ for a specific object identified by its object number (OBJ#). It selects
         * ROWID, CON#, INTCOL#, OBJ#, and decomposed parts of SPARE1 (SPARE11 and SPARE12) from the
         * CCOL$ table. The query filters records by the object number and utilizes an SCN-based flashback
         * query to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * column definitions and metadata that are critical for accurate data replication, particularly
         * when focusing on a specific object rather than an entire user's objects.
         *
         * @param j The system change number (SCN) to use for flashback query on CCOL$.
         * @param k The object number (OBJ#) to filter columns by.
         *
         * @note This query is particularly useful in scenarios involving flashback or historical
         *       data replication where column metadata for a specific object at a given SCN is required.
         *       It allows for precise retrieval of column information without needing to consider
         *       ownership details.
         */
        static constexpr std::string_view SQL_GET_SYS_CCOL_OBJ
        {
            "SELECT"
            "   L.ROWID, L.CON#, L.INTCOL#, L.OBJ#, MOD(L.SPARE1, 18446744073709551616) AS SPARE11,"
            "   MOD(TRUNC(L.SPARE1 / 18446744073709551616), 18446744073709551616) AS SPARE12"
            " FROM"
            "   SYS.CCOL$ AS OF SCN :j L"
            " WHERE"
            "   L.OBJ# = :k"
        };

        /**
         * @brief SQL query to retrieve constraint definition information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch constraint definition information from the
         * system tables CDEF$ and OBJ$ for objects owned by a specific user. It selects ROWID, CON#, OBJ#, and TYPE#
         * from the CDEF$ table joined with OBJ$. The query filters records by the owner ID of the object and utilizes
         * SCN-based flashback queries to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * constraint definitions that are critical for accurate data replication and schema reconstruction.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on CDEF$.
         * @param k The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication, where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including constraints.
         */
        static constexpr std::string_view SQL_GET_SYS_CDEF_USER
        {
            "SELECT"
            "   D.ROWID, D.CON#, D.OBJ#, D.TYPE#"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.CDEF$ AS OF SCN :j D ON"
            "     O.OBJ# = D.OBJ#"
            " WHERE"
            "   O.OWNER# = :k"
        };

        /**
         * @brief SQL query to retrieve constraint definition information for a specific object from the system tables.
         *
         * This constant defines an SQL statement used to fetch constraint definition information from the
         * system table CDEF$ for a specific object identified by its object number (OBJ#). It selects
         * ROWID, CON#, OBJ#, and TYPE# from the CDEF$ table. The query filters records by the object number
         * and utilizes an SCN-based flashback query to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * constraint definitions that are critical for accurate data replication and schema reconstruction,
         * particularly when focusing on a single object rather than an entire user's objects.
         *
         * @param j The system change number (SCN) to use for flashback query on CDEF$.
         * @param k The object number (OBJ#) to filter constraints by.
         *
         * @note This query is especially useful in scenarios involving flashback or historical
         *       data replication where constraint metadata for a specific object at a given SCN is required.
         *       It allows for precise retrieval of constraint information without considering ownership details.
         */
        static constexpr std::string_view SQL_GET_SYS_CDEF_OBJ
        {
            "SELECT"
            "   D.ROWID, D.CON#, D.OBJ#, D.TYPE#"
            " FROM"
            "   SYS.CDEF$ AS OF SCN :j D"
            " WHERE"
            "   D.OBJ# = :k"
        };

        /**
         * @brief SQL query to retrieve column information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed column information from the
         * system tables COL$ and OBJ$ for objects owned by a specific user. It selects various metadata
         * fields including ROWID, OBJ#, COL#, SEGCOL#, INTCOL#, NAME, TYPE#, LENGTH, PRECISION#, SCALE,
         * CHARSETFORM, CHARSETID, NULL$, and decomposed parts of PROPERTY (PROPERTY1 and PROPERTY2)
         * from the COL$ table joined with OBJ$. The query filters records by the owner ID of the object
         * and utilizes SCN-based flashback queries to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * column definitions and metadata that are critical for accurate data replication.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on COL$.
         * @param k The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication, where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema. It allows for detailed inspection of column
         *       characteristics and their properties at a given point in time.
         */
        static constexpr std::string_view SQL_GET_SYS_COL_USER
        {
            "SELECT"
            "   C.ROWID, C.OBJ#, C.COL#, C.SEGCOL#, C.INTCOL#, C.NAME, C.TYPE#, C.LENGTH, C.PRECISION#, C.SCALE, C.CHARSETFORM, C.CHARSETID, C.NULL$,"
            "   MOD(C.PROPERTY, 18446744073709551616) AS PROPERTY1, MOD(TRUNC(C.PROPERTY / 18446744073709551616), 18446744073709551616) AS PROPERTY2"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.COL$ AS OF SCN :j C ON"
            "     O.OBJ# = C.OBJ#"
            " WHERE"
            "   O.OWNER# = :k"
        };

        /**
         * @brief SQL query to retrieve column information for a specific object from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed column information from the
         * system table COL$ for a specific object identified by its object number (OBJ#). It selects
         * various metadata fields including ROWID, OBJ#, COL#, SEGCOL#, INTCOL#, NAME, TYPE#, LENGTH,
         * PRECISION#, SCALE, CHARSETFORM, CHARSETID, NULL$, and decomposed parts of PROPERTY (PROPERTY1 and PROPERTY2)
         * from the COL$ table. The query filters records by the object number and utilizes an SCN-based flashback
         * query to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * column definitions and metadata that are critical for accurate data replication, particularly
         * when focusing on a specific object rather than an entire user's objects.
         *
         * @param j The system change number (SCN) to use for flashback query on COL$.
         * @param k The object number (OBJ#) to filter columns by.
         *
         * @note This query is particularly useful in scenarios involving flashback or historical
         *       data replication where column metadata for a specific object at a given SCN is required.
         *       It allows for precise retrieval of column information without needing to consider
         *       ownership details.
         */
        static constexpr std::string_view SQL_GET_SYS_COL_OBJ
        {
            "SELECT"
            "   C.ROWID, C.OBJ#, C.COL#, C.SEGCOL#, C.INTCOL#, C.NAME, C.TYPE#, C.LENGTH, C.PRECISION#, C.SCALE, C.CHARSETFORM, C.CHARSETID, C.NULL$,"
            "   MOD(C.PROPERTY, 18446744073709551616) AS PROPERTY1, MOD(TRUNC(C.PROPERTY / 18446744073709551616), 18446744073709551616) AS PROPERTY2"
            " FROM"
            "   SYS.COL$ AS OF SCN :j C"
            " WHERE"
            "   C.OBJ# = :k"
        };

        /**
         * @brief SQL query to retrieve deferred segment creation storage information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch deferred segment creation storage information from the
         * system table DEFERRED_STG$ for objects owned by a specific user. It selects ROWID, OBJ#, and decomposed parts
         * of FLAGS_STG (FLAGS_STG1 and FLAGS_STG2) from the DEFERRED_STG$ table joined with OBJ$. The query filters
         * records by the owner ID of the object and utilizes SCN-based flashback queries to ensure data consistency
         * at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * deferred segment creation storage flags that are critical for accurate data replication.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on DEFERRED_STG$.
         * @param k The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication, where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including deferred segment creation information.
         */
        static constexpr std::string_view SQL_GET_SYS_DEFERRED_STG_USER
        {
            "SELECT"
            "   DS.ROWID, DS.OBJ#, MOD(DS.FLAGS_STG, 18446744073709551616) AS FLAGS_STG1,"
            "   MOD(TRUNC(DS.FLAGS_STG / 18446744073709551616), 18446744073709551616) AS FLAGS_STG2"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.DEFERRED_STG$ AS OF SCN :j DS ON"
            "     O.OBJ# = DS.OBJ#"
            " WHERE"
            "   O.OWNER# = :k"
        };

        /**
         * @brief SQL query to retrieve deferred segment creation storage information for a specific object from the system tables.
         *
         * This constant defines an SQL statement used to fetch deferred segment creation storage information from the
         * system table DEFERRED_STG$ for a specific object identified by its object number (OBJ#). It selects ROWID,
         * OBJ#, and decomposed parts of FLAGS_STG (FLAGS_STG1 and FLAGS_STG2) from the DEFERRED_STG$ table. The query
         * filters records by the object number and utilizes an SCN-based flashback query to ensure data consistency
         * at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * deferred segment creation storage flags that are critical for accurate data replication,
         * particularly when focusing on a specific object rather than an entire user's objects.
         *
         * @param j The system change number (SCN) to use for flashback query on DEFERRED_STG$.
         * @param k The object number (OBJ#) to filter deferred segment creation information by.
         *
         * @note This query is particularly useful in scenarios involving flashback or historical
         *       data replication where deferred segment creation flags for a specific object at a given
         *       SCN are required. It allows for precise retrieval of storage flags without considering
         *       ownership details.
         */
        static constexpr std::string_view SQL_GET_SYS_DEFERRED_STG_OBJ
        {
            "SELECT"
            "   DS.ROWID, DS.OBJ#, MOD(DS.FLAGS_STG, 18446744073709551616) AS FLAGS_STG1,"
            "   MOD(TRUNC(DS.FLAGS_STG / 18446744073709551616), 18446744073709551616) AS FLAGS_STG2"
            " FROM"
            "   SYS.DEFERRED_STG$ AS OF SCN :j DS"
            " WHERE"
            "   DS.OBJ# = :k"
        };

        /**
         * @brief SQL query to retrieve extended column information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed extended column information from the
         * system tables ECOL$ and OBJ$ for objects owned by a specific user. It selects ROWID, TABOBJ#, COLNUM,
         * and GUARD_ID from the ECOL$ table joined with OBJ$. The query filters records by the owner ID of the object
         * and utilizes SCN-based flashback queries to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather extended column
         * definitions and metadata that are critical for accurate data replication, especially in cases where
         * additional column attributes beyond standard ones are needed.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on ECOL$.
         * @param k The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including extended column attributes.
         */
        static constexpr std::string_view SQL_GET_SYS_ECOL_USER
        {
            "SELECT"
            "   E.ROWID, E.TABOBJ#, E.COLNUM, E.GUARD_ID"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.ECOL$ AS OF SCN :j E ON"
            "     O.OBJ# = E.TABOBJ#"
            " WHERE"
            "   O.OWNER# = :k"
        };

        /**
         * @brief SQL query to retrieve extended column information for a specific object from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed extended column information from the
         * system table ECOL$ for a specific object identified by its table object number (TABOBJ#). It selects
         * ROWID, TABOBJ#, COLNUM, and GUARD_ID from the ECOL$ table. The query filters records by the table object
         * number and utilizes an SCN-based flashback query to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather extended column
         * definitions and metadata that are critical for accurate data replication, particularly when focusing
         * on a specific table rather than an entire user's objects.
         *
         * @param j The system change number (SCN) to use for flashback query on ECOL$.
         * @param k The table object number (TABOBJ#) to filter extended column information by.
         *
         * @note This query is particularly useful in scenarios involving flashback or historical
         *       data replication where extended column attributes for a specific table at a given SCN are required.
         *       It allows for precise retrieval of extended column information without considering ownership details.
         */
        static constexpr std::string_view SQL_GET_SYS_ECOL_OBJ
        {
            "SELECT"
            "   E.ROWID, E.TABOBJ#, E.COLNUM, E.GUARD_ID"
            " FROM"
            "   SYS.ECOL$ AS OF SCN :j E"
            " WHERE"
            "   E.TABOBJ# = :k"
        };

        /**
         * @brief SQL query to retrieve extended column information for a specific user from the system tables,
         *        handling cases where the GUARD_ID might not be populated in ECOL$.
         *
         * This constant defines an SQL statement used to fetch detailed extended column information from the
         * system tables ECOL$ and OBJ$ for objects owned by a specific user. It selects ROWID, TABOBJ#, COLNUM,
         * and a default value of -1 for GUARD_ID from the ECOL$ table joined with OBJ$. The query filters records
         * by the owner ID of the object and utilizes SCN-based flashback queries to ensure data consistency
         * at a specific point in time.
         *
         * This variant is used to handle legacy or special cases where the GUARD_ID column in ECOL$ may be NULL
         * or undefined, providing a fallback mechanism to ensure metadata completeness.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on ECOL$.
         * @param k The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication where extended column metadata might lack GUARD_ID values.
         *       It ensures that the replicator can still process such entries without failure.
         */
        static constexpr std::string_view SQL_GET_SYS_ECOL11_USER
        {
            "SELECT"
            "   E.ROWID, E.TABOBJ#, E.COLNUM, -1 AS GUARD_ID"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.ECOL$ AS OF SCN :j E ON"
            "     O.OBJ# = E.TABOBJ#"
            " WHERE"
            "   O.OWNER# = :k"
        };

        /**
         * @brief SQL query to retrieve extended column information for a specific object from the system tables,
         *        handling cases where the GUARD_ID might not be populated in ECOL$.
         *
         * This constant defines an SQL statement used to fetch detailed extended column information from the
         * system table ECOL$ for a specific object identified by its table object number (TABOBJ#). It selects
         * ROWID, TABOBJ#, COLNUM, and a default value of -1 for GUARD_ID from the ECOL$ table. The query filters
         * records by the table object number and utilizes an SCN-based flashback query to ensure data consistency
         * at a specific point in time.
         *
         * This variant is used to handle legacy or special cases where the GUARD_ID column in ECOL$ may be NULL
         * or undefined, providing a fallback mechanism to ensure metadata completeness.
         *
         * @param j The system change number (SCN) to use for flashback query on ECOL$.
         * @param k The table object number (TABOBJ#) to filter extended column information by.
         *
         * @note This query is particularly useful in scenarios involving flashback or historical
         *       data replication where extended column metadata for a specific table at a given SCN might
         *       lack GUARD_ID values. It ensures that the replicator can still process such entries without failure.
         */
        static constexpr std::string_view SQL_GET_SYS_ECOL11_OBJ
        {
            "SELECT"
            "   E.ROWID, E.TABOBJ#, E.COLNUM, -1 AS GUARD_ID"
            " FROM"
            "   SYS.ECOL$ AS OF SCN :j E"
            " WHERE"
            "   E.TABOBJ# = :k"
        };

        /**
         * @brief SQL query to retrieve LOB column information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed LOB (Large Object) column information
         * from the system tables LOB$ and OBJ$ for objects owned by a specific user. It selects ROWID, OBJ#,
         * COL#, INTCOL#, LOBJ#, and TS# from the LOB$ table joined with OBJ$. The query filters records by the
         * owner ID of the object and utilizes SCN-based flashback queries to ensure data consistency at a
         * specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather LOB column
         * definitions and metadata that are critical for accurate data replication, especially when dealing
         * with large data types such as BLOBs, CLOBs, or NCLOBs.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on LOB$.
         * @param k The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including LOB column attributes.
         */
        static constexpr std::string_view SQL_GET_SYS_LOB_USER
        {
            "SELECT"
            "   L.ROWID, L.OBJ#, L.COL#, L.INTCOL#, L.LOBJ#, L.TS#"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.LOB$ AS OF SCN :j L ON"
            "     O.OBJ# = L.OBJ#"
            " WHERE"
            "   O.OWNER# = :k"
        };

        /**
         * @brief SQL query to retrieve LOB column information for a specific object from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed LOB (Large Object) column information
         * from the system table LOB$ for a specific object identified by its object number (OBJ#). It selects
         * ROWID, OBJ#, COL#, INTCOL#, LOBJ#, and TS# from the LOB$ table. The query filters records by the
         * object number and utilizes an SCN-based flashback query to ensure data consistency at a specific
         * point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather LOB column
         * definitions and metadata that are critical for accurate data replication, particularly when focusing
         * on a specific object rather than an entire user's objects.
         *
         * @param i The system change number (SCN) to use for flashback query on LOB$.
         * @param j The object number (OBJ#) to filter LOB columns by.
         *
         * @note This query is particularly useful in scenarios involving flashback or historical
         *       data replication where LOB column metadata for a specific object at a given SCN is required.
         *       It allows for precise retrieval of LOB column information without needing to consider
         *       ownership details.
         */
        static constexpr std::string_view SQL_GET_SYS_LOB_OBJ
        {
            "SELECT"
            "   L.ROWID, L.OBJ#, L.COL#, L.INTCOL#, L.LOBJ#, L.TS#"
            " FROM"
            "   SYS.LOB$ AS OF SCN :i L"
            " WHERE"
            "   L.OBJ# = :j"
        };

        /**
         * @brief SQL query to retrieve LOB compression partition information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed LOB compression partition information
         * from the system tables LOBCOMPPART$, LOB$, and OBJ$ for objects owned by a specific user. It selects
         * ROWID, PARTOBJ#, and LOBJ# from the LOBCOMPPART$ table joined with LOB$ and OBJ$. The query filters
         * records by the owner ID of the object and utilizes SCN-based flashback queries to ensure data consistency
         * at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather LOB compression
         * partition definitions and metadata that are critical for accurate data replication, especially when
         * dealing with compressed LOB partitions.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on LOB$.
         * @param k The system change number (SCN) to use for flashback query on LOBCOMPPART$.
         * @param l The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including LOB compression partition attributes.
         */
        static constexpr std::string_view SQL_GET_SYS_LOB_COMP_PART_USER
        {
            "SELECT"
            "   LCP.ROWID, LCP.PARTOBJ#, LCP.LOBJ#"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.LOB$ AS OF SCN :j L ON"
            "     O.OBJ# = L.OBJ#"
            " JOIN"
            "   SYS.LOBCOMPPART$ AS OF SCN :k LCP ON"
            "     LCP.LOBJ# = L.LOBJ#"
            " WHERE"
            "   O.OWNER# = :l"
        };

        /**
         * @brief SQL query to retrieve LOB compression partition information for a specific object from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed LOB compression partition information
         * from the system tables LOBCOMPPART$ and LOB$ for a specific object identified by its object number (OBJ#).
         * It selects ROWID, PARTOBJ#, and LOBJ# from the LOBCOMPPART$ table joined with LOB$. The query filters
         * records by the object number and utilizes an SCN-based flashback query to ensure data consistency at a
         * specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather LOB compression
         * partition definitions and metadata that are critical for accurate data replication, particularly when
         * focusing on a specific object rather than an entire user's objects.
         *
         * @param i The system change number (SCN) to use for flashback query on LOB$.
         * @param j The system change number (SCN) to use for flashback query on LOBCOMPPART$.
         * @param k The object number (OBJ#) to filter LOB compression partitions by.
         *
         * @note This query is particularly useful in scenarios involving flashback or historical
         *       data replication where LOB compression partition metadata for a specific object at a given SCN is required.
         *       It allows for precise retrieval of LOB compression partition information without needing to consider
         *       ownership details.
         */
        static constexpr std::string_view SQL_GET_SYS_LOB_COMP_PART_OBJ
        {
            "SELECT"
            "   LCP.ROWID, LCP.PARTOBJ#, LCP.LOBJ#"
            " FROM"
            "   SYS.LOB$ AS OF SCN :i L"
            " JOIN"
            "   SYS.LOBCOMPPART$ AS OF SCN :j LCP ON"
            "     LCP.LOBJ# = L.LOBJ#"
            " WHERE"
            "   L.OBJ# = :k"
        };

        /**
         * @brief SQL query to retrieve LOB fragment information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed LOB fragment information from the
         * system tables LOBFRAG$, LOBCOMPPART$, LOB$, and OBJ$ for objects owned by a specific user.
         * It selects ROWID, FRAGOBJ#, PARENTOBJ#, and TS# from the LOBFRAG$ table joined with LOBCOMPPART$
         * and LOB$, and further joined with OBJ$. The query filters records by the owner ID of the object
         * and utilizes SCN-based flashback queries to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather LOB fragment
         * definitions and metadata that are critical for accurate data replication, especially when dealing
         * with fragmented LOB structures.
         *
         * The first part of the UNION ALL retrieves fragments associated with compressed partitions.
         * The second part retrieves fragments directly associated with LOBs.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on LOB$.
         * @param k The system change number (SCN) to use for flashback query on LOBCOMPPART$.
         * @param l The system change number (SCN) to use for flashback query on LOBFRAG$.
         * @param m The owner ID (OWNER#) to filter objects by.
         * @param n The system change number (SCN) to use for flashback query on OBJ$ (second part).
         * @param o The system change number (SCN) to use for flashback query on LOB$ (second part).
         * @param p The system change number (SCN) to use for flashback query on LOBFRAG$ (second part).
         * @param q The owner ID (OWNER#) to filter objects by (second part).
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including LOB fragment attributes.
         */
        static constexpr std::string_view SQL_GET_SYS_LOB_FRAG_USER
        {
            "SELECT"
            "   LF.ROWID, LF.FRAGOBJ#, LF.PARENTOBJ#, LF.TS#"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.LOB$ AS OF SCN :j L ON"
            "     O.OBJ# = L.OBJ#"
            " JOIN"
            "   SYS.LOBCOMPPART$ AS OF SCN :k LCP ON"
            "     LCP.LOBJ# = L.LOBJ#"
            " JOIN"
            "   SYS.LOBFRAG$ AS OF SCN :l LF ON"
            "     LCP.PARTOBJ# = LF.PARENTOBJ#"
            " WHERE"
            "   O.OWNER# = :m"
            " UNION ALL"
            " SELECT"
            "   LF.ROWID, LF.FRAGOBJ#, LF.PARENTOBJ#, LF.TS#"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :n O"
            " JOIN"
            "   SYS.LOB$ AS OF SCN :o L ON"
            "     O.OBJ# = L.OBJ#"
            " JOIN"
            "   SYS.LOBFRAG$ AS OF SCN :p LF ON"
            "     L.LOBJ# = LF.PARENTOBJ#"
            " WHERE"
            "   O.OWNER# = :q"
        };

        /**
         * @brief SQL query to retrieve LOB fragment information for a specific object from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed LOB fragment information from the
         * system tables LOBFRAG$, LOBCOMPPART$, and LOB$ for a specific object identified by its object number (OBJ#).
         * It selects ROWID, FRAGOBJ#, PARENTOBJ#, and TS# from the LOBFRAG$ table joined with LOBCOMPPART$
         * and LOB$. The query filters records by the object number and utilizes SCN-based flashback queries
         * to ensure data consistency at a specific point in time.
         *
         * Additionally, it includes a second part of the query using a UNION ALL to also fetch fragments
         * directly linked to LOB objects without going through LOBCOMPPART$. This ensures comprehensive
         * coverage of LOB fragments regardless of their structural relationship.
         *
         * This query is used during schema parsing and metadata extraction processes to gather LOB fragment
         * definitions and metadata that are critical for accurate data replication, particularly when dealing
         * with fragmented LOB structures for a specific object.
         *
         * @param i The system change number (SCN) to use for flashback query on LOB$ (first part).
         * @param j The system change number (SCN) to use for flashback query on LOBCOMPPART$ (first part).
         * @param k The system change number (SCN) to use for flashback query on LOBFRAG$ (first part).
         * @param l The object number (OBJ#) to filter LOB fragments by (first part).
         * @param m The system change number (SCN) to use for flashback query on LOB$ (second part).
         * @param n The system change number (SCN) to use for flashback query on LOBFRAG$ (second part).
         * @param o The object number (OBJ#) to filter LOB fragments by (second part).
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including LOB fragment attributes for a specific object.
         *       It handles both direct and indirect relationships between LOB fragments and their parent objects.
         */
        static constexpr std::string_view SQL_GET_SYS_LOB_FRAG_OBJ
        {
            "SELECT"
            "   LF.ROWID, LF.FRAGOBJ#, LF.PARENTOBJ#, LF.TS#"
            " FROM"
            "   SYS.LOB$ AS OF SCN :i L"
            " JOIN"
            "   SYS.LOBCOMPPART$ AS OF SCN :j LCP ON"
            "     LCP.LOBJ# = L.LOBJ#"
            " JOIN"
            "   SYS.LOBFRAG$ AS OF SCN :k LF ON"
            "     LCP.PARTOBJ# = LF.PARENTOBJ#"
            " WHERE"
            "   L.OBJ# = :l"
            " UNION ALL"
            " SELECT"
            "   LF.ROWID, LF.FRAGOBJ#, LF.PARENTOBJ#, LF.TS#"
            " FROM"
            "   SYS.LOB$ AS OF SCN :m L"
            " JOIN"
            "   SYS.LOBFRAG$ AS OF SCN :n LF ON"
            "     L.LOBJ# = LF.PARENTOBJ#"
            " WHERE"
            "   L.OBJ# = :o"
        };

        /**
         * @brief SQL query to retrieve object information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed object information from the
         * system table OBJ$ for objects owned by a specific user. It selects ROWID, OWNER#, OBJ#, DATAOBJ#,
         * NAME, TYPE#, and decomposed parts of FLAGS (FLAGS1 and FLAGS2) from the OBJ$ table. The query
         * filters records by the owner ID of the object and utilizes an SCN-based flashback query to
         * ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * object definitions and metadata that are critical for accurate data replication.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication, where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including object attributes.
         */
        static constexpr std::string_view SQL_GET_SYS_OBJ_USER
        {
            "SELECT"
            "   O.ROWID, O.OWNER#, O.OBJ#, O.DATAOBJ#, O.NAME, O.TYPE#,"
            "   MOD(O.FLAGS, 18446744073709551616) AS FLAGS1, MOD(TRUNC(O.FLAGS / 18446744073709551616), 18446744073709551616) AS FLAGS2"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " WHERE"
            "   O.OWNER# = :j"
        };

        /**
         * @brief SQL query to retrieve object information for a specific user and name pattern from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed object information from the
         * system table OBJ$ for objects owned by a specific user and matching a given name pattern.
         * It selects ROWID, OWNER#, OBJ#, DATAOBJ#, NAME, TYPE#, and decomposed parts of FLAGS (FLAGS1 and FLAGS2)
         * from the OBJ$ table. The query filters records by the owner ID of the object and a regular expression
         * pattern applied to the object name. It utilizes an SCN-based flashback query to ensure data consistency
         * at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * object definitions and metadata that match a specific naming convention, which is critical
         * for accurate data replication when filtering objects by name.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The owner ID (OWNER#) to filter objects by.
         * @param k The regular expression pattern to match against object names.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including object attributes, while applying
         *       a name-based filter. It allows for flexible object selection based on naming conventions.
         */
        static constexpr std::string_view SQL_GET_SYS_OBJ_NAME
        {
            "SELECT"
            "   O.ROWID, O.OWNER#, O.OBJ#, O.DATAOBJ#, O.NAME, O.TYPE#,"
            "   MOD(O.FLAGS, 18446744073709551616) AS FLAGS1, MOD(TRUNC(O.FLAGS / 18446744073709551616), 18446744073709551616) AS FLAGS2"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " WHERE"
            "   O.OWNER# = :j AND REGEXP_LIKE(O.NAME, :k)"
        };

        /**
         * @brief SQL query to retrieve table information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed table information from the
         * system tables TAB$ and OBJ$ for objects owned by a specific user. It selects ROWID, OBJ#,
         * DATAOBJ#, TS#, CLUCOLS, and decomposed parts of FLAGS (FLAGS1 and FLAGS2) and PROPERTY (PROPERTY1 and PROPERTY2)
         * from the TAB$ table joined with OBJ$. The query filters records by the owner ID of the object
         * and utilizes SCN-based flashback queries to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * table definitions and metadata that are critical for accurate data replication, particularly
         * for understanding table storage characteristics and properties.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on TAB$.
         * @param k The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication, where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including table attributes such as tablespace,
         *       clustering columns, and storage properties.
         */
        static constexpr std::string_view SQL_GET_SYS_TAB_USER
        {
            "SELECT"
            "   T.ROWID, T.OBJ#, T.DATAOBJ#, T.TS#, T.CLUCOLS,"
            "   MOD(T.FLAGS, 18446744073709551616) AS FLAGS1, MOD(TRUNC(T.FLAGS / 18446744073709551616), 18446744073709551616) AS FLAGS2,"
            "   MOD(T.PROPERTY, 18446744073709551616) AS PROPERTY1, MOD(TRUNC(T.PROPERTY / 18446744073709551616), 18446744073709551616) AS PROPERTY2"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.TAB$ AS OF SCN :j T ON"
            "     O.OBJ# = T.OBJ#"
            " WHERE"
            "   O.OWNER# = :k"
        };

        /**
         * @brief SQL query to retrieve table information for a specific object from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed table information from the
         * system table TAB$ for a specific object identified by its object number (OBJ#). It selects
         * ROWID, OBJ#, DATAOBJ#, TS#, CLUCOLS, and decomposed parts of FLAGS (FLAGS1 and FLAGS2) and PROPERTY (PROPERTY1 and PROPERTY2)
         * from the TAB$ table. The query filters records by the object number and utilizes an SCN-based flashback
         * query to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * table definitions and metadata that are critical for accurate data replication, particularly
         * when focusing on a specific object rather than an entire user's objects.
         *
         * @param j The system change number (SCN) to use for flashback query on TAB$.
         * @param k The object number (OBJ#) to filter tables by.
         *
         * @note This query is particularly useful in scenarios involving flashback or historical
         *       data replication where table metadata for a specific object at a given SCN is required.
         *       It allows for precise retrieval of table information without needing to consider
         *       ownership details.
         */
        static constexpr std::string_view SQL_GET_SYS_TAB_OBJ
        {
            "SELECT"
            "   T.ROWID, T.OBJ#, T.DATAOBJ#, T.TS#, T.CLUCOLS,"
            "   MOD(T.FLAGS, 18446744073709551616) AS FLAGS1, MOD(TRUNC(T.FLAGS / 18446744073709551616), 18446744073709551616) AS FLAGS2,"
            "   MOD(T.PROPERTY, 18446744073709551616) AS PROPERTY1, MOD(TRUNC(T.PROPERTY / 18446744073709551616), 18446744073709551616) AS PROPERTY2"
            " FROM"
            "   SYS.TAB$ AS OF SCN :j T"
            " WHERE"
            "   T.OBJ# = :k"
        };

        /**
         * @brief SQL query to retrieve table partition information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed table partition information from the
         * system tables TABCOMPART$ and OBJ$ for objects owned by a specific user. It selects ROWID, OBJ#,
         * DATAOBJ#, and BO# from the TABCOMPART$ table joined with OBJ$. The query filters records by the
         * owner ID of the object and utilizes SCN-based flashback queries to ensure data consistency at a
         * specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * table partition definitions and metadata that are critical for accurate data replication,
         * particularly for understanding partitioned table structures and their associated metadata.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on TABCOMPART$.
         * @param k The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including partitioned table attributes.
         */
        static constexpr std::string_view SQL_GET_SYS_TABCOMPART_USER
        {
            "SELECT"
            "   TCP.ROWID, TCP.OBJ#, TCP.DATAOBJ#, TCP.BO#"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.TABCOMPART$ AS OF SCN :j TCP ON"
            "     O.OBJ# = TCP.OBJ#"
            " WHERE"
            "   O.OWNER# = :k"
        };

        /**
         * @brief SQL query to retrieve table partition information for a specific object from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed table partition information from the
         * system table TABCOMPART$ for a specific object identified by its object number (OBJ#). It selects
         * ROWID, OBJ#, DATAOBJ#, and BO# from the TABCOMPART$ table. The query filters records by the object
         * number and utilizes an SCN-based flashback query to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * table partition definitions and metadata that are critical for accurate data replication,
         * particularly when focusing on a specific object rather than an entire user's objects.
         *
         * @param j The system change number (SCN) to use for flashback query on TABCOMPART$.
         * @param k The object number (OBJ#) to filter table partitions by.
         *
         * @note This query is particularly useful in scenarios involving flashback or historical
         *       data replication where table partition metadata for a specific object at a given SCN is required.
         *       It allows for precise retrieval of partition information without needing to consider
         *       ownership details.
         */
        static constexpr std::string_view SQL_GET_SYS_TABCOMPART_OBJ
        {
            "SELECT"
            "   TCP.ROWID, TCP.OBJ#, TCP.DATAOBJ#, TCP.BO#"
            " FROM"
            "   SYS.TABCOMPART$ AS OF SCN :j TCP"
            " WHERE"
            "   TCP.OBJ# = :k"
        };

        /**
         * @brief SQL query to retrieve table partition information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed table partition information from the
         * system tables TABPART$ and OBJ$ for objects owned by a specific user. It selects ROWID, OBJ#,
         * DATAOBJ#, and BO# from the TABPART$ table joined with OBJ$. The query filters records by the
         * owner ID of the object and utilizes SCN-based flashback queries to ensure data consistency at a
         * specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * table partition definitions and metadata that are critical for accurate data replication,
         * particularly for understanding partitioned table structures and their associated metadata.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on TABPART$.
         * @param k The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including partitioned table attributes.
         */
        static constexpr std::string_view SQL_GET_SYS_TABPART_USER
        {
            "SELECT"
            "   TP.ROWID, TP.OBJ#, TP.DATAOBJ#, TP.BO#"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.TABPART$ AS OF SCN :j TP ON"
            "     O.OBJ# = TP.OBJ#"
            " WHERE"
            "   O.OWNER# = :k"
        };

        /**
         * @brief SQL query to retrieve table partition information for a specific object from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed table partition information from the
         * system table TABPART$ for a specific object identified by its object number (OBJ#). It selects
         * ROWID, OBJ#, DATAOBJ#, and BO# from the TABPART$ table. The query filters records by the object
         * number and utilizes an SCN-based flashback query to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * table partition definitions and metadata that are critical for accurate data replication,
         * particularly when focusing on a specific object rather than an entire user's objects.
         *
         * @param j The system change number (SCN) to use for flashback query on TABPART$.
         * @param k The object number (OBJ#) to filter table partitions by.
         *
         * @note This query is particularly useful in scenarios involving flashback or historical
         *       data replication where table partition metadata for a specific object at a given SCN is required.
         *       It allows for precise retrieval of partition information without needing to consider
         *       ownership details.
         */
        static constexpr std::string_view SQL_GET_SYS_TABPART_OBJ
        {
            "SELECT"
            "   TP.ROWID, TP.OBJ#, TP.DATAOBJ#, TP.BO#"
            " FROM"
            "   SYS.TABPART$ AS OF SCN :j TP"
            " WHERE"
            "   TP.OBJ# = :k"
        };

        /**
         * @brief SQL query to retrieve table subpartition information for a specific user from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed table subpartition information from the
         * system tables TABSUBPART$ and OBJ$ for objects owned by a specific user. It selects ROWID, OBJ#,
         * DATAOBJ#, and POBJ# from the TABSUBPART$ table joined with OBJ$. The query filters records by the
         * owner ID of the object and utilizes SCN-based flashback queries to ensure data consistency at a
         * specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * table subpartition definitions and metadata that are critical for accurate data replication,
         * particularly for understanding complex partitioned table structures and their associated metadata.
         *
         * @param i The system change number (SCN) to use for flashback query on OBJ$.
         * @param j The system change number (SCN) to use for flashback query on TABSUBPART$.
         * @param k The owner ID (OWNER#) to filter objects by.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including subpartitioned table attributes.
         */
        static constexpr std::string_view SQL_GET_SYS_TABSUBPART_USER
        {
            "SELECT"
            "   TSP.ROWID, TSP.OBJ#, TSP.DATAOBJ#, TSP.POBJ#"
            " FROM"
            "   SYS.OBJ$ AS OF SCN :i O"
            " JOIN"
            "   SYS.TABSUBPART$ AS OF SCN :j TSP ON"
            "     O.OBJ# = TSP.OBJ#"
            " WHERE"
            "   O.OWNER# = :k"
        };

        /**
         * @brief SQL query to retrieve table subpartition information for a specific object from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed table subpartition information from the
         * system table TABSUBPART$ for a specific object identified by its object number (OBJ#). It selects
         * ROWID, OBJ#, DATAOBJ#, and POBJ# from the TABSUBPART$ table. The query filters records by the object
         * number and utilizes an SCN-based flashback query to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * table subpartition definitions and metadata that are critical for accurate data replication,
         * particularly when focusing on a specific object rather than an entire user's objects.
         *
         * @param j The system change number (SCN) to use for flashback query on TABSUBPART$.
         * @param k The object number (OBJ#) to filter table subpartitions by.
         *
         * @note This query is particularly useful in scenarios involving flashback or historical
         *       data replication where table subpartition metadata for a specific object at a given SCN is required.
         *       It allows for precise retrieval of subpartition information without needing to consider
         *       ownership details.
         */
        static constexpr std::string_view SQL_GET_SYS_TABSUBPART_OBJ
        {
            "SELECT"
            "   TSP.ROWID, TSP.OBJ#, TSP.DATAOBJ#, TSP.POBJ#"
            " FROM"
            "   SYS.TABSUBPART$ AS OF SCN :j TSP"
            " WHERE"
            "   TSP.POBJ# = :k"
        };

        /**
         * @brief SQL query to retrieve tablespace information for a specific system change number (SCN).
         *
         * This constant defines an SQL statement used to fetch detailed tablespace information from the
         * system table TS$ for a specific point in time defined by the system change number (SCN).
         * It selects ROWID, TS#, NAME, and BLOCKSIZE from the TS$ table. The query utilizes an SCN-based
         * flashback query to ensure data consistency at the specified SCN.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * tablespace definitions and metadata that are critical for accurate data replication,
         * particularly for understanding storage characteristics and allocation units.
         *
         * @param i The system change number (SCN) to use for flashback query on TS$.
         *
         * @note This query is essential for scenarios involving flashback or historical data
         *       replication where the state of the TS$ table at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including tablespace attributes.
         */
        static constexpr std::string_view SQL_GET_SYS_TS
        {
            "SELECT"
            "   T.ROWID, T.TS#, T.NAME, T.BLOCKSIZE"
            " FROM"
            "   SYS.TS$ AS OF SCN :i T"
        };

        /**
         * @brief SQL query to retrieve user information for a specific user name pattern from the system tables.
         *
         * This constant defines an SQL statement used to fetch detailed user information from the
         * system table USER$ for users whose names match a given regular expression pattern.
         * It selects ROWID, USER#, NAME, and decomposed parts of SPARE1 (SPARE11 and SPARE12)
         * from the USER$ table. The query applies a regular expression filter on the user name
         * and utilizes an SCN-based flashback query to ensure data consistency at a specific point in time.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * user definitions and metadata that are critical for accurate data replication, particularly
         * when filtering users by name patterns.
         *
         * @param i The system change number (SCN) to use for flashback query on USER$.
         * @param j The regular expression pattern to match against user names.
         *
         * @note This query is particularly important in scenarios involving flashback or historical
         *       data replication where the state of system tables at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including user attributes, while applying
         *       a name-based filter. It allows for flexible user selection based on naming conventions.
         */
        static constexpr std::string_view SQL_GET_SYS_USER
        {
            "SELECT"
            "   U.ROWID, U.USER#, U.NAME, MOD(U.SPARE1, 18446744073709551616) AS SPARE11,"
            "   MOD(TRUNC(U.SPARE1 / 18446744073709551616), 18446744073709551616) AS SPARE12"
            " FROM"
            "   SYS.USER$ AS OF SCN :i U"
            " WHERE"
            "   REGEXP_LIKE(U.NAME, :j)"
        };

        /**
         * @brief SQL query to retrieve XML DB type table set information for a specific system change number (SCN).
         *
         * This constant defines an SQL statement used to fetch detailed XML DB type table set information from the
         * system table XDB$TTSET for a specific point in time defined by the system change number (SCN).
         * It selects ROWID, GUID, TOKSUF, FLAGS, and OBJ# from the XDB$TTSET table. The query utilizes an SCN-based
         * flashback query to ensure data consistency at the specified SCN.
         *
         * This query is used during schema parsing and metadata extraction processes to gather
         * XML DB type table set definitions and metadata that are critical for accurate data replication,
         * particularly for understanding XML-related schema elements and their attributes.
         *
         * @param i The system change number (SCN) to use for flashback query on XDB$TTSET.
         *
         * @note This query is essential for scenarios involving flashback or historical data
         *       replication where the state of the XDB$TTSET table at a specific SCN must be retrieved
         *       to accurately reconstruct the schema including XML DB type table set attributes.
         */
        static constexpr std::string_view SQL_GET_XDB_TTSET
        {
            "SELECT"
            "   T.ROWID, T.GUID, T.TOKSUF, T.FLAGS, T.OBJ#"
            " FROM"
            "   XDB.XDB$TTSET AS OF SCN :i T"
        };

        /**
         * @brief SQL query to check the connection to the database.
         *
         * This constant defines a simple SQL statement used to verify connectivity to the Oracle database.
         * It executes a basic SELECT statement that returns a constant value (1) from the DUAL table.
         * This query is commonly used in database applications to test whether a connection is active
         * and functional.
         *
         * @note This query is typically executed during initialization or health checks to confirm
         *       that the database connection is operational and responsive.
         */
        static constexpr std::string_view SQL_CHECK_CONNECTION
                {"SELECT 1 FROM DUAL"};

        /**
         * @brief Indicates whether the database is operating in a standby mode.
         *
         * This boolean flag determines if the database is functioning as a physical or logical
         * standby. It is initialized during the setup phase of the replicator and influences
         * various aspects of log processing and metadata retrieval. Standby databases often
         * require different handling for log files and SCN calculations compared to primary databases.
         *
         * @note This flag is set based on the database role obtained via SQL_GET_DATABASE_ROLE
         *       and affects the execution paths for queries like SQL_GET_SEQUENCE_FROM_SCN_STANDBY.
         */
        bool standby{false};

        /**
         * @brief Positions the reader to the initial log file and SCN.
         *
         * This method initializes the reader's position within the redo log stream. It determines
         * the appropriate starting point for log reading based on the configured initial SCN or
         * the current database SCN. It also sets up the necessary log file readers and prepares
         * the system for log processing.
         *
         * This method is called during the initialization phase of the replicator to ensure that
         * the reader begins processing from the correct location in the redo log stream.
         *
         * @throws Exception if there is an error in positioning the reader or initializing log readers.
         */
        void positionReader() override;

        /**
         * @brief Loads database metadata required for online replication.
         *
         * This method is responsible for gathering and loading all necessary metadata from the
         * Oracle database system tables. It performs several key tasks:
         * - Determines the database role (primary or standby) to adjust behavior accordingly.
         * - Fetches database information such as archive log mode, supplemental logging settings,
         *   and the current system change number (SCN).
         * - Retrieves information about database incarnations to ensure correct replication context.
         * - Collects container information in CDB environments.
         * - Obtains the list of archived logs to prepare for log replay.
         * - Loads system tables related to objects, columns, constraints, tables, tablespaces, and users.
         * - Processes XML DB type table set information if applicable.
         * - Initializes the schema with the loaded metadata.
         *
         * This method is invoked during the initialization phase of the replicator to ensure
         * that all required metadata is available before beginning log processing.
         *
         * @throws Exception if any error occurs during metadata loading or schema initialization.
         */
        void loadDatabaseMetadata() override;

        /**
         * @brief Checks the database connection.
         *
         * This method verifies that the connection to the Oracle database is active and functional.
         * It executes a simple query (SQL_CHECK_CONNECTION) to ensure that the database is reachable
         * and responsive. This check is typically performed during initialization or periodic health
         * monitoring to confirm that the replicator can communicate with the database.
         *
         * @return true if the connection is successful and the database is accessible, false otherwise.
         * @throws Exception if an error occurs during the connection check.
         */
        bool checkConnection() override;

        /**
         * @brief Retrieves the value of a specified database parameter.
         *
         * This method executes a query against the database to fetch the value of a given parameter.
         * It uses the SQL_GET_PARAMETER query to retrieve the value from the SYS.V_$PARAMETER view.
         * The parameter name is passed as an argument, and the method returns the corresponding value
         * as a string. If the parameter is not found or an error occurs, an empty string is returned.
         *
         * This functionality is useful for dynamically checking database configuration settings
         * during runtime or initialization, allowing the replicator to adapt its behavior based
         * on current parameter values.
         *
         * @param parameter The name of the database parameter to retrieve.
         * @return The value of the specified parameter, or an empty string if not found or an error occurs.
         * @throws Exception if an error occurs during the database query execution.
         */
        std::string getParameterValue(std::string parameter) const;

        /**
         * @brief Retrieves the value of a specified database property.
         *
         * This method executes a query against the database to fetch the value of a given property.
         * It uses the SQL_GET_PROPERTY query to retrieve the value from the DATABASE_PROPERTIES view.
         * The property name is passed as an argument, and the method returns the corresponding value
         * as a string. If the property is not found or an error occurs, an empty string is returned.
         *
         * This functionality is useful for dynamically checking database-wide properties
         * during runtime or initialization, allowing the replicator to adapt its behavior based
         * on current property values.
         *
         * @param property The name of the database property to retrieve.
         * @return The value of the specified property, or an empty string if not found or an error occurs.
         * @throws Exception if an error occurs during the database query execution.
         */
        std::string getPropertyValue(std::string property) const;

        /**
         * @brief Checks if a table has grants assigned to it.
         *
         * This method verifies whether a specified table has any grants associated with it in the database.
         * It performs a lookup in the system tables to determine if there are any privileges granted on the table.
         * This check is important for ensuring that the replicator has the necessary permissions to read and replicate
         * data from the table, especially in environments where access control is strict.
         *
         * @param tableName The name of the table to check for grants.
         * @throws Exception if an error occurs during the grant check or if the table does not exist.
         */
        void checkTableForGrants(const std::string& tableName);

        /**
         * @brief Checks if a table has grants assigned to it at a specific system change number (SCN).
         *
         * This method verifies whether a specified table has any grants associated with it in the database
         * at a specific point in time defined by the system change number (SCN). It performs a flashback
         * query against the system tables to determine if there were any privileges granted on the table
         * at that particular SCN. This is particularly useful in scenarios involving historical data
         * replication or flashback operations where the state of grants at a specific point in time needs
         * to be validated.
         *
         * @param tableName The name of the table to check for grants.
         * @param scn The system change number (SCN) to perform the flashback query at.
         * @throws Exception if an error occurs during the grant check or if the table does not exist.
         */
        void checkTableForGrantsFlashback(const std::string& tableName, Scn scn);

        /**
         * @brief Returns the name of the replication mode.
         *
         * This method returns a string indicating the current replication mode, which is "online".
         * It overrides the base class implementation to provide a specific description for online
         * replication mode.
         *
         * @return A string literal "online", indicating that the replicator is operating in online mode.
         */
        std::string getModeName() const override;

        /**
         * @brief Verifies the schema against the current database state.
         *
         * This method performs a verification step to ensure that the schema loaded into the replicator
         * is consistent with the current state of the database. It compares the schema elements against
         * the actual database metadata at the specified system change number (currentScn). This is crucial
         * for maintaining data integrity during online replication, especially after potential schema changes
         * or when resuming replication from a checkpoint.
         *
         * The verification process involves checking for discrepancies in object definitions, column mappings,
         * constraint definitions, and other schema components. If inconsistencies are detected, appropriate
         * actions are taken to resolve them or report errors to the user.
         *
         * @param currentScn The system change number (SCN) to use for comparing the schema against the database state.
         *                   This SCN represents the point in time at which the schema verification is performed.
         * @throws Exception if schema verification fails due to inconsistencies or other issues.
         */
        void verifySchema(Scn currentScn) override;

        /**
         * @brief Creates the schema in the target database.
         *
         * This method is responsible for creating the schema structure in the target database
         * based on the metadata collected during the initialization phase. It ensures that all
         * necessary database objects (tables, indexes, constraints, etc.) are created according
         * to the schema definition.
         *
         * This method is typically invoked during the initialization or setup phase of the replicator
         * to prepare the target environment for data replication.
         *
         * @throws Exception if an error occurs during schema creation or if the schema already exists.
         */
        void createSchema() override;

        /**
         * @brief Reads system dictionaries metadata for a given schema and system change number (SCN).
         *
         * This method reads metadata from system dictionaries related to various database objects
         * such as columns, constraints, tables, and tablespaces. It populates the provided schema
         * object with this metadata, ensuring that the schema reflects the state of the database
         * at the specified system change number (targetScn).
         *
         * The method iterates through different system dictionary tables and loads relevant information
         * into the schema. It handles various object types and their associated metadata, including
         * column definitions, constraint information, table properties, and tablespace details.
         *
         * This method is typically called during the initialization or schema update phases to
         * ensure that the schema is fully populated with metadata from the system tables.
         *
         * @param schema Pointer to the Schema object to populate with system dictionary metadata.
         * @param targetScn The system change number (SCN) to use for flashback queries on system tables.
         *                  This SCN defines the point in time for which the metadata should be retrieved.
         * @throws Exception if an error occurs during metadata loading or schema population.
         */
        void readSystemDictionariesMetadata(Schema* schema, Scn targetScn);

        /**
         * @brief Reads system dictionaries details for a given schema, system change number (SCN), user, and object.
         *
         * This method reads detailed metadata from system dictionaries related to specific database objects
         * such as columns, constraints, tables, and tablespaces for a given user and object. It populates the
         * provided schema object with this detailed metadata, ensuring that the schema reflects the state of
         * the database at the specified system change number (targetScn) for the specified user and object.
         *
         * The method iterates through different system dictionary tables and loads relevant information
         * into the schema. It handles various object types and their associated metadata, including
         * column definitions, constraint information, table properties, and tablespace details.
         *
         * This method is typically called during the initialization or schema update phases to
         * ensure that the schema is fully populated with detailed metadata from the system tables
         * for a specific user and object.
         *
         * @param schema Pointer to the Schema object to populate with system dictionary metadata.
         * @param targetScn The system change number (SCN) to use for flashback queries on system tables.
         *                  This SCN defines the point in time for which the metadata should be retrieved.
         * @param user The user ID for which to retrieve metadata.
         * @param obj The object ID for which to retrieve metadata.
         * @throws Exception if an error occurs during metadata loading or schema population.
         */
        void readSystemDictionariesDetails(Schema* schema, Scn targetScn, typeUser user, typeObj obj);

        /**
         * @brief Reads system dictionaries for a given schema, system change number (SCN), owner, table name, and options.
         *
         * This method reads system dictionaries related to a specific table for a given owner and system change number (SCN).
         * It populates the provided schema object with metadata for the specified table, ensuring that the schema reflects
         * the state of the database at the specified SCN for the given owner and table.
         *
         * The method fetches metadata from system tables such as OBJ$, COL$, TAB$, and others relevant to the table.
         * It applies filters based on the owner and table name and utilizes flashback queries to ensure data consistency
         * at the specified SCN.
         *
         * This method is typically called during the initialization or schema update phases to populate
         * schema information for a specific table.
         *
         * @param schema Pointer to the Schema object to populate with system dictionary metadata.
         * @param targetScn The system change number (SCN) to use for flashback queries on system tables.
         *                  This SCN defines the point in time for which the metadata should be retrieved.
         * @param owner The owner (schema name) of the table for which to retrieve metadata.
         * @param table The name of the table for which to retrieve metadata.
         * @param options The options to apply when reading the schema, such as including or excluding certain metadata elements.
         * @throws Exception if an error occurs during metadata loading or schema population.
         */
        void readSystemDictionaries(Schema* schema, Scn targetScn, const std::string& owner, const std::string& table, DbTable::OPTIONS options);

        /**
         * @brief Creates a schema element for a specific table.
         *
         * This method creates a schema element for a specific table based on the provided parameters.
         * It constructs a schema element with the specified owner, table name, key list, key, tag type,
         * tag list, tag, condition, options, and updates a map of tables that have been updated.
         *
         * This method is used during the schema creation process to define the structure and properties
         * of individual tables within the schema. It allows for fine-grained control over how each table
         * is represented in the schema, including specifying keys, tags, conditions, and options.
         *
         * @param targetScn The system change number (SCN) to use for flashback queries on system tables.
         *                  This SCN defines the point in time for which the metadata should be retrieved.
         * @param owner The owner (schema name) of the table for which to create the schema element.
         * @param table The name of the table for which to create the schema element.
         * @param keyList The list of keys to include in the schema element.
         * @param key The key to use for the schema element.
         * @param tagType The type of tag to associate with the schema element.
         * @param tagList The list of tags to include in the schema element.
         * @param tag The tag to use for the schema element.
         * @param condition The condition to apply when creating the schema element.
         * @param options The options to apply when creating the schema element.
         * @param tablesUpdated A map of tables that have been updated, used to track which tables have been processed.
         * @throws Exception if an error occurs during schema element creation or if the table does not exist.
         */
        void createSchemaForTable(Scn targetScn, const std::string& owner, const std::string& table, const std::vector<std::string>& keyList,
                                  const std::string& key, SchemaElement::TAG_TYPE tagType, const std::vector<std::string>& tagList, const std::string& tag,
                                  const std::string& condition, DbTable::OPTIONS options, std::unordered_map<typeObj, std::string>& tablesUpdated);

        /**
          * @brief Updates the online redo log data.
          *
          * This method is responsible for updating the internal state of the replicator related to
          * online redo logs. It typically involves refreshing or reinitializing the redo log data
          * structures to reflect the current state of the redo log files in the database. This is
          * crucial for ensuring that the replicator maintains an accurate view of the redo log stream
          * and can correctly process new log entries.
          *
          * The method may involve re-fetching information about redo log files, their statuses,
          * sequence numbers, and other metadata. It ensures that the replicator's internal log
          * tracking mechanisms are synchronized with the actual database state.
          *
          * This method is typically called during the initialization or refresh phases of the replicator
          * to keep the redo log information up-to-date.
          *
          * @throws Exception if an error occurs during the update process or if there are issues
          *         with accessing or processing the redo log data.
          */
        void updateOnlineRedoLogData() override;

    public:
        DatabaseEnvironment* env;
        DatabaseConnection* conn;

        /**
         * @brief Controls whether the database connection should be kept open.
         *
         * This boolean flag determines whether the replicator maintains an active database connection
         * throughout its operation or closes and reopens it as needed. When set to true, the connection
         * remains persistent, which can improve performance by avoiding repeated connection establishment
         * overhead. When set to false, the connection is closed after each operation, which may be
         * preferred in environments with strict connection limits or when connection pooling is used.
         *
         * @note This flag is typically set during initialization based on configuration parameters
         *       and affects how the replicator manages its database connections.
         */
        bool keepConnection;

        ReplicatorOnline(Ctx* newCtx, void (*newArchGetLog)(Replicator* replicator), Builder* newBuilder, Metadata* newMetadata,
                         TransactionBuffer* newTransactionBuffer, std::string newAlias, std::string newDatabase, std::string newUser,
                         std::string newPassword, std::string newConnectString, bool newKeepConnection);
        ~ReplicatorOnline() override;

        /**
         * @brief Switches the replicator to standby mode.
         *
         * This method transitions the replicator from its current operational state to standby mode.
         * In standby mode, the replicator adjusts its behavior to accommodate the characteristics
         * of a standby database, such as handling standby redo logs instead of online redo logs,
         * and potentially modifying how it processes and interprets log data.
         *
         * This method is typically invoked when the replicator detects that the underlying database
         * has transitioned to a standby role, or when explicitly commanded to switch to standby mode.
         * It ensures that all internal state and processing logic aligns with the standby environment.
         *
         * @throws Exception if an error occurs during the transition to standby mode or if the
         *         transition cannot be completed successfully.
         */
        void goStandby() override;

        /**
         * @brief Archives the online redo log for the replicator.
         *
         * This static method is responsible for archiving the online redo log files associated with
         * the given replicator instance. It is typically invoked by the replicator itself or by
         * external mechanisms to ensure that online redo logs are properly archived for backup
         * and recovery purposes. The method performs the necessary operations to archive the logs,
         * including identifying the current log files, initiating the archival process, and updating
         * the replicator's internal state to reflect the archived logs.
         *
         * This method is a callback function that is registered with the replicator during its
         * initialization phase. It is called automatically by the replicator when it needs to
         * archive the online redo logs, ensuring that the archival process is seamlessly integrated
         * into the replication workflow.
         *
         * @param replicator Pointer to the Replicator instance for which the online redo logs are to be archived.
         * @throws Exception if an error occurs during the archival process or if the replicator is not in a state
         *         suitable for archiving logs.
         */
        static void archGetLogOnline(Replicator* replicator);
    };
}

#endif
