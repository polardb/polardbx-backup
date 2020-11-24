/* Copyright (c) 2014, 2020, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @addtogroup Replication
  @{

  @file statement_events.h

  @brief Contains the classes representing statement events occurring in the
  replication stream. Each event is represented as a byte sequence with logical
  divisions as event header, event specific data and event footer. The header
  and footer are common to all the events and are represented as two different
  subclasses.
*/

#ifndef STATEMENT_EVENT_INCLUDED
#define STATEMENT_EVENT_INCLUDED

#include "control_events.h"
#include "mysql/udf_registration_types.h"

namespace binary_log {
/**
  The following constant represents the maximum of MYSQL_XID domain.
  The maximum XID value practically is never supposed to grow beyond UINT64
  range.
*/
const uint64_t INVALID_XID = 0xffffffffffffffffULL;

/**
  @class Query_event

  A @c Query_event is created for each query that modifies the
  database, unless the query is logged row-based.

  @section Query_event_binary_format Binary format

  See @ref Binary_log_event_binary_format "Binary format for log events" for
  a general discussion and introduction to the binary format of binlog
  events.

  The Post-Header has five components:

  <table>
  <caption>Post-Header for Query_event</caption>

  <tr>
    <th>Name</th>
    <th>Format</th>
    <th>Description</th>
  </tr>

  <tr>
    <td>thread_id</td>
    <td>4 byte unsigned integer</td>
    <td>The ID of the thread that issued this statement. It is needed for
        temporary tables.</td>
  </tr>

  <tr>
    <td>query_exec_time</td>
    <td>4 byte unsigned integer</td>
    <td>The time from when the query started to when it was logged in
    the binlog, in seconds.</td>
  </tr>

  <tr>
    <td>db_len</td>
    <td>1 byte integer</td>
    <td>The length of the name of the currently selected database.</td>
  </tr>

  <tr>
    <td>error_code</td>
    <td>2 byte unsigned integer</td>
    <td>Error code generated by the master. If the master fails, the
    slave will fail with the same error code.
    </td>
  </tr>

  <tr>
    <td>status_vars_len</td>
    <td>2 byte unsigned integer</td>
    <td>The length of the status_vars block of the Body, in bytes. This is not
        present for binlog version 1 and 3. See
    @ref Query_event_status_vars "below".
    </td>
  </tr>
  </table>

  The Body has the following components:

  <table>
  <caption>Body for Query_event</caption>

  <tr>
    <th>Name</th>
    <th>Format</th>
    <th>Description</th>
  </tr>

  <tr>
    <td>@anchor Query_event_status_vars status_vars</td>
    <td>status_vars_len bytes</td>
    <td>Zero or more status variables.  Each status variable consists
    of one byte identifying the variable stored, followed by the value
    of the variable.  The possible variables are listed separately in
    the table @ref Table_query_event_status_vars "below".  MySQL
    always writes events in the order defined below; however, it is
    capable of reading them in any order.  </td>
  </tr>

  <tr>
    <td>m_db</td>
    <td>db_len + 1</td>
    <td>The currently selected database, as a null-terminated string.

    (The trailing zero is redundant since the length is already known;
    it is db_len from Post-Header.)
    </td>
  </tr>

  <tr>
    <td>m_query</td>
    <td>variable length string without trailing zero, extending to the
    end of the event (determined by the length field of the
    Common-Header)
    </td>
    <td>The SQL query.</td>
  </tr>
  </table>

  The following table lists the status variables that may appear in
  the status_vars field.

  @anchor Table_query_event_status_vars
  <table>
  <caption>Status variables for Query_event</caption>

  <tr>
    <th>Status variable</th>
    <th>1 byte identifier</th>
    <th>Format</th>
    <th>Description</th>
  </tr>

  <tr>
    <td>flags2</td>
    <td>Q_FLAGS2_CODE == 0</td>
    <td>4 byte bitfield</td>
    <td>The flags in @c thd->options, binary AND-ed with @c
    OPTIONS_WRITTEN_TO_BIN_LOG.  The @c thd->options bitfield contains
    options for "SELECT".  @c OPTIONS_WRITTEN identifies those options
    that need to be written to the binlog (not all do).  Specifically,
    @c OPTIONS_WRITTEN_TO_BIN_LOG equals (@c OPTION_AUTO_IS_NULL | @c
    OPTION_NO_FOREIGN_KEY_CHECKS | @c OPTION_RELAXED_UNIQUE_CHECKS |
    @c OPTION_NOT_AUTOCOMMIT), or 0x0c084000 in hex.

    These flags correspond to the SQL variables SQL_AUTO_IS_NULL,
    FOREIGN_KEY_CHECKS, UNIQUE_CHECKS, and AUTOCOMMIT, documented in
    the "SET Syntax" section of the MySQL Manual.

    This field is always written to the binlog in version >= 5.0, and
    never written in version < 5.0.
    </td>
  </tr>

  <tr>
    <td>sql_mode</td>
    <td>Q_SQL_MODE_CODE == 1</td>
    <td>8 byte bitfield</td>
    <td>The @c sql_mode variable.  See the section "SQL Modes" in the
    MySQL manual, and see sql_class.h for a list of the possible
    flags. Currently (2007-10-04), the following flags are available:
    <pre>
    MODE_REAL_AS_FLOAT==0x1
    MODE_PIPES_AS_CONCAT==0x2
    MODE_ANSI_QUOTES==0x4
    MODE_IGNORE_SPACE==0x8
    MODE_NOT_USED==0x10
    MODE_ONLY_FULL_GROUP_BY==0x20
    MODE_NO_UNSIGNED_SUBTRACTION==0x40
    MODE_NO_DIR_IN_CREATE==0x80
    MODE_ANSI==0x80000
    MODE_NO_AUTO_VALUE_ON_ZERO==0x100000
    MODE_NO_BACKSLASH_ESCAPES==0x200000
    MODE_STRICT_TRANS_TABLES==0x400000
    MODE_STRICT_ALL_TABLES==0x800000
    MODE_NO_ZERO_IN_DATE==0x1000000
    MODE_NO_ZERO_DATE==0x2000000
    MODE_INVALID_DATES==0x4000000
    MODE_ERROR_FOR_DIVISION_BY_ZERO==0x8000000
    MODE_TRADITIONAL==0x10000000
    MODE_HIGH_NOT_PRECEDENCE==0x40000000
    MODE_PAD_CHAR_TO_FULL_LENGTH==0x80000000
    MODE_TIME_TRUNCATE_FRACTIONAL==0x100000000
   </pre>
    All these flags are replicated from the server.  However, all
    flags except @c MODE_NO_DIR_IN_CREATE are honored by the slave;
    the slave always preserves its old value of @c
    MODE_NO_DIR_IN_CREATE.

    This field is always written to the binlog.
    </td>
  </tr>

  <tr>
    <td>catalog</td>
    <td>Q_CATALOG_NZ_CODE == 6</td>
    <td>Variable-length string: the length in bytes (1 byte) followed
    by the characters (at most 255 bytes)
    </td>
    <td>Stores the client's current catalog.  Every database belongs
    to a catalog, the same way that every table belongs to a
    database.  Currently, there is only one catalog, "std".

    This field is written if the length of the catalog is > 0;
    otherwise it is not written.
    </td>
  </tr>

  <tr>
    <td>auto_increment</td>
    <td>Q_AUTO_INCREMENT == 3</td>
    <td>two 2 byte unsigned integers, totally 2+2=4 bytes</td>

    <td>The two variables auto_increment_increment and
    auto_increment_offset, in that order.  For more information, see
    "System variables" in the MySQL manual.

    This field is written if auto_increment > 1.  Otherwise, it is not
    written.
    </td>
  </tr>

  <tr>
    <td>charset</td>
    <td>Q_CHARSET_CODE == 4</td>
    <td>three 2 byte unsigned integers, totally 2+2+2=6 bytes</td>
    <td>The three variables character_set_client,
    collation_connection, and collation_server, in that order.
    character_set_client is a code identifying the character set and
    collation used by the client to encode the query.
    collation_connection identifies the character set and collation
    that the master converts the query to when it receives it; this is
    useful when comparing literal strings.  collation_server is the
    default character set and collation used when a new database is
    created.

    See also "Connection Character Sets and Collations" in the MySQL
    5.1 manual.

    All three variables are codes identifying a (character set,
    collation) pair.  To see which codes map to which pairs, run the
    query "SELECT id, character_set_name, collation_name FROM
    COLLATIONS".

    Cf. Q_CHARSET_DATABASE_CODE below.

    This field is always written.
    </td>
  </tr>

  <tr>
    <td>time_zone</td>
    <td>Q_TIME_ZONE_CODE == 5</td>
    <td>Variable-length string: the length in bytes (1 byte) followed
    by the characters (at most 255 bytes).
    <td>The time_zone of the master.

    See also "System Variables" and "MySQL Server Time Zone Support"
    in the MySQL manual.

    This field is written if the length of the time zone string is >
    0; otherwise, it is not written.
    </td>
  </tr>

  <tr>
    <td>lc_time_names_number</td>
    <td>Q_LC_TIME_NAMES_CODE == 7</td>
    <td>2 byte integer</td>
    <td>A code identifying a table of month and day names.  The
    mapping from codes to languages is defined in @c sql_locale.cc.

    This field is written if it is not 0, i.e., if the locale is not
    en_US.
    </td>
  </tr>

  <tr>
    <td>charset_database_number</td>
    <td>Q_CHARSET_DATABASE_CODE == 8</td>
    <td>2 byte integer</td>

    <td>The value of the collation_database system variable (in the
    source code stored in @c thd->variables.collation_database), which
    holds the code for a (character set, collation) pair as described
    above (see Q_CHARSET_CODE).

    collation_database was used in old versions (???WHEN).  Its value
    was loaded when issuing a "use db" query and could be changed by
    issuing a "SET collation_database=xxx" query.  It used to affect
    the "LOAD DATA INFILE" and "CREATE TABLE" commands.

    In newer versions, "CREATE TABLE" has been changed to take the
    character set from the database of the created table, rather than
    the character set of the current database.  This makes a
    difference when creating a table in another database than the
    current one.  "LOAD DATA INFILE" has not yet changed to do this,
    but there are plans to eventually do it, and to make
    collation_database read-only.

    This field is written if it is not 0.
    </td>
  </tr>
  <tr>
    <td>table_map_for_update</td>
    <td>Q_TABLE_MAP_FOR_UPDATE_CODE == 9</td>
    <td>8 byte integer</td>

    <td>The value of the table map that is to be updated by the
    multi-table update query statement. Every bit of this variable
    represents a table, and is set to 1 if the corresponding table is
    to be updated by this statement.

    The value of this variable is set when executing a multi-table update
    statement and used by slave to apply filter rules without opening
    all the tables on slave. This is required because some tables may
    not exist on slave because of the filter rules.
    </td>
  </tr>
  <tr>
    <td>master_data_written</td>
    <td>Q_MASTER_DATA_WRITTEN_CODE == 10</td>
    <td>4 byte bitfield</td>

    <td>The value of the original length of a Query_event that comes from a
    master. Master's event is relay-logged with storing the original size of
    event in this field by the IO thread. The size is to be restored by reading
    Q_MASTER_DATA_WRITTEN_CODE-marked event from the relay log.

    This field is not written to slave's server binlog by the SQL thread.
    This field only exists in relay logs where master has binlog_version<4 i.e.
    server_version < 5.0 and the slave has binlog_version=4.
    </td>
  </tr>
  <tr>
    <td>binlog_invoker</td>
    <td>Q_INVOKER == 11</td>
    <td>2 Variable-length strings: the length in bytes (1 byte) followed
    by characters (user), again followed by length in bytes (1 byte) followed
    by characters(host)</td>

    <td>The value of boolean variable m_binlog_invoker is set TRUE if
    CURRENT_USER() is called in account management statements. SQL thread
    uses it as a default definer in CREATE/ALTER SP, SF, Event, TRIGGER or
    VIEW statements.

    The field Q_INVOKER has length of user stored in 1 byte followed by the
    user string which is assigned to 'user' and the length of host stored in
    1 byte followed by host string which is assigned to 'host'.
    </td>
  </tr>
  <tr>
    <td>mts_accessed_dbs</td>
    <td>Q_UPDATED_DB_NAMES == 12</td>
    <td>1 byte character, and a 2-D array</td>
    <td>The total number and the names to of the databases accessed is stored,
        to be propagated to the slave in order to facilitate the parallel
        applying of the Query events.
    </td>
  </tr>
  <tr>
    <td>explicit_defaults_ts</td>
    <td>Q_EXPLICIT_DEFAULTS_FOR_TIMESTAMP</td>
    <td>1 byte boolean</td>
    <td>Stores master connection @@session.explicit_defaults_for_timestamp when
        CREATE and ALTER operate on a table with a TIMESTAMP column. </td>
  </tr>
  <tr>
    <td>ddl_xid</td>
    <td>Q_DDL_LOGGED_WITH_XID</td>
    <td>8 byte integer</td>
    <td>Stores variable carrying xid info of 2pc-aware (recoverable) DDL
        queries. </td>
  </tr>
  <tr>
    <td>default_collation_for_utf8mb4_number</td>
    <td>Q_DEFAULT_COLLATION_FOR_UTF8MB4</td>
    <td>2 byte integer</td>
    <td>Stores variable carrying the the default collation for the utf8mb4
        character set. Mainly used to support replication 5.7- master to a 8.0+
        slave.
    </td>
  </tr>
  <tr>
    <td>sql_require_primary_key</td>
    <td>Q_SQL_REQUIRE_PRIMARY_KEY</td>
    <td>2 byte integer</td>
    <td>Value of the config variable sql_require_primary_key</td>
  </tr>
  <tr>
    <td>default_table_encryption</td>
    <td>Q_DEFAULT_TABLE_ENCRYPTION</td>
    <td>2 byte integer</td>
    <td>Value of the config variable default_table_encryption</td>
  </tr>
  </table>

  @subsection Query_event_notes_on_previous_versions Notes on Previous Versions

  * Status vars were introduced in version 5.0.  To read earlier
  versions correctly, check the length of the Post-Header.

  * The status variable Q_CATALOG_CODE == 2 existed in MySQL 5.0.x,
  where 0<=x<=3.  It was identical to Q_CATALOG_CODE, except that the
  string had a trailing '\0'.  The '\0' was removed in 5.0.4 since it
  was redundant (the string length is stored before the string).  The
  Q_CATALOG_CODE will never be written by a new master, but can still
  be understood by a new slave.

  * See Q_CHARSET_DATABASE_CODE in the table above.

  * When adding new status vars, please don't forget to update the
  MAX_SIZE_LOG_EVENT_STATUS.

*/

class Query_event : public Binary_log_event {
 public:
  /** query event post-header */
  enum Query_event_post_header_offset {
    Q_THREAD_ID_OFFSET = 0,
    Q_EXEC_TIME_OFFSET = 4,
    Q_DB_LEN_OFFSET = 8,
    Q_ERR_CODE_OFFSET = 9,
    Q_STATUS_VARS_LEN_OFFSET = 11,
    Q_DATA_OFFSET = QUERY_HEADER_LEN
  };

  /* these are codes, not offsets; not more than 256 values (1 byte). */
  enum Query_event_status_vars {
    Q_FLAGS2_CODE = 0,
    Q_SQL_MODE_CODE,
    /*
      Q_CATALOG_CODE is catalog with end zero stored; it is used only by MySQL
      5.0.x where 0<=x<=3. We have to keep it to be able to replicate these
      old masters.
    */
    Q_CATALOG_CODE,
    Q_AUTO_INCREMENT,
    Q_CHARSET_CODE,
    Q_TIME_ZONE_CODE,
    /*
      Q_CATALOG_NZ_CODE is catalog withOUT end zero stored; it is used by MySQL
      5.0.x where x>=4. Saves one byte in every Query_event in binlog,
      compared to Q_CATALOG_CODE. The reason we didn't simply re-use
      Q_CATALOG_CODE is that then a 5.0.3 slave of this 5.0.x (x>=4)
      master would crash (segfault etc) because it would expect a 0 when there
      is none.
    */
    Q_CATALOG_NZ_CODE,
    Q_LC_TIME_NAMES_CODE,
    Q_CHARSET_DATABASE_CODE,
    Q_TABLE_MAP_FOR_UPDATE_CODE,
    /* It is just a placeholder after 8.0.2*/
    Q_MASTER_DATA_WRITTEN_CODE,
    Q_INVOKER,
    /*
      Q_UPDATED_DB_NAMES status variable collects information of accessed
      databases i.e. the total number and the names to be propagated to the
      slave in order to facilitate the parallel applying of the Query events.
    */
    Q_UPDATED_DB_NAMES,
    Q_MICROSECONDS,
    /*
     A old (unused now) code for Query_log_event status similar to G_COMMIT_TS.
   */
    Q_COMMIT_TS,
    /*
     An old (unused after migration to Gtid_event) code for
     Query_log_event status, similar to G_COMMIT_TS2.
   */
    Q_COMMIT_TS2,
    /*
      The master connection @@session.explicit_defaults_for_timestamp which
      is recorded for queries, CREATE and ALTER table that is defined with
      a TIMESTAMP column, that are dependent on that feature.
      For pre-WL6292 master's the associated with this code value is zero.
    */
    Q_EXPLICIT_DEFAULTS_FOR_TIMESTAMP,
    /*
      The variable carries xid info of 2pc-aware (recoverable) DDL queries.
    */
    Q_DDL_LOGGED_WITH_XID,
    /*
      This variable stores the default collation for the utf8mb4 character set.
      Used to support cross-version replication.
    */
    Q_DEFAULT_COLLATION_FOR_UTF8MB4,

    /*
      Replicate sql_require_primary_key.
    */
    Q_SQL_REQUIRE_PRIMARY_KEY,

    /*
      Replicate default_table_encryption.
    */
    Q_DEFAULT_TABLE_ENCRYPTION
  };
  const char *query;
  const char *db;
  const char *catalog;
  const char *time_zone_str;

 protected:
  const char *user;
  size_t user_len;
  const char *host;
  size_t host_len;

  /* Required by the MySQL server class Log_event::Query_event */
  unsigned long data_len;
  /*
    Copies data into the buffer in the following fashion
    +--------+-----------+------+------+---------+----+-------+----+
    | catlog | time_zone | user | host | db name | \0 | Query | \0 |
    +--------+-----------+------+------+---------+----+-------+----+
  */
  int fill_data_buf(unsigned char *dest, unsigned long len);

 public:
  /* data members defined in order they are packed and written into the log */
  uint32_t thread_id;
  uint32_t query_exec_time;
  size_t db_len;
  uint16_t error_code;
  /*
    We want to be able to store a variable number of N-bit status vars:
    (generally N=32; but N=64 for SQL_MODE) a user may want to log the number
    of affected rows (for debugging) while another does not want to lose 4
    bytes in this.
    The storage on disk is the following:
    status_vars_len is part of the post-header,
    status_vars are in the variable-length part, after the post-header, before
    the db & query.
    status_vars on disk is a sequence of pairs (code, value) where 'code' means
    'sql_mode', 'affected' etc. Sometimes 'value' must be a short string, so
    its first byte is its length. For now the order of status vars is:
    flags2 - sql_mode - catalog - autoinc - charset
    We should add the same thing to Load_event, but in fact
    LOAD DATA INFILE is going to be logged with a new type of event (logging of
    the plain text query), so Load_event would be frozen, so no need. The
    new way of logging LOAD DATA INFILE would use a derived class of
    Query_event, so automatically benefit from the work already done for
    status variables in Query_event.
  */
  uint16_t status_vars_len;
  /*
    If we already know the length of the query string
    we pass it with q_len, so we would not have to call strlen()
    otherwise, set it to 0, in which case, we compute it with strlen()
  */
  size_t q_len;

  /* The members below represent the status variable block */

  /*
    'flags2' is a second set of flags (on top of those in Log_event), for
    session variables. These are thd->options which is & against a mask
    (OPTIONS_WRITTEN_TO_BIN_LOG).
    flags2_inited helps make a difference between flags2==0 (3.23 or 4.x
    master, we don't know flags2, so use the slave server's global options) and
    flags2==0 (5.0 master, we know this has a meaning of flags all down which
    must influence the query).
  */
  bool flags2_inited;
  bool sql_mode_inited;
  bool charset_inited;

  uint32_t flags2;
  /* In connections sql_mode is 32 bits now but will be 64 bits soon */
  uint64_t sql_mode;
  uint16_t auto_increment_increment, auto_increment_offset;
  char charset[6];
  size_t time_zone_len; /* 0 means uninited */
  /*
    Binlog format 3 and 4 start to differ (as far as class members are
    concerned) from here.
  */
  size_t catalog_len;            // <= 255 char; 0 means uninited
  uint16_t lc_time_names_number; /* 0 means en_US */
  uint16_t charset_database_number;
  /*
    map for tables that will be updated for a multi-table update query
    statement, for other query statements, this will be zero.
  */
  uint64_t table_map_for_update;
  /*
    The following member gets set to OFF or ON value when the
    Query-log-event is marked as dependent on
    @@explicit_defaults_for_timestamp. That is the member is relevant
    to queries that declare TIMESTAMP column attribute, like CREATE
    and ALTER.
    The value is set to @c TERNARY_OFF when @@explicit_defaults_for_timestamp
    encoded value is zero, otherwise TERNARY_ON.
  */
  enum enum_ternary {
    TERNARY_UNSET,
    TERNARY_OFF,
    TERNARY_ON
  } explicit_defaults_ts;
  /*
    number of updated databases by the query and their names. This info
    is requested by both Coordinator and Worker.
  */
  unsigned char mts_accessed_dbs;
  char mts_accessed_db_names[MAX_DBS_IN_EVENT_MTS][NAME_LEN];
  /* XID value when the event is a 2pc-capable DDL */
  uint64_t ddl_xid;
  /* Default collation for the utf8mb4 set. Used in cross-version replication */
  uint16_t default_collation_for_utf8mb4_number;
  uint8_t sql_require_primary_key;

  uint8_t default_table_encryption;

  /**
    The constructor will be used while creating a Query_event, to be
    written to the binary log.
  */
  Query_event(const char *query_arg, const char *catalog_arg,
              const char *db_arg, uint32_t query_length,
              unsigned long thread_id_arg, unsigned long long sql_mode_arg,
              unsigned long auto_increment_increment_arg,
              unsigned long auto_increment_offset_arg, unsigned int number,
              unsigned long long table_map_for_update_arg, int errcode);

  /**
    The constructor receives a buffer and instantiates a Query_event filled in
    with the data from the buffer

    <pre>
    The fixed event data part buffer layout is as follows:
    +---------------------------------------------------------------------+
    | thread_id | query_exec_time | db_len | error_code | status_vars_len |
    +---------------------------------------------------------------------+
    </pre>

    <pre>
    The fixed event data part buffer layout is as follows:
    +--------------------------------------------+
    | Zero or more status variables | db | query |
    +--------------------------------------------+
    </pre>

    @param buf  Contains the serialized event.
    @param fde  An FDE event (see Rotate_event constructor for more info).
    @param event_type  Required to determine whether the event type is
                       QUERY_EVENT or EXECUTE_LOAD_QUERY_EVENT
  */
  Query_event(const char *buf, const Format_description_event *fde,
              Log_event_type event_type);
  /**
    The simplest constructor that could possibly work.  This is used for
    creating static objects that have a special meaning and are invisible
    to the log.
  */
  Query_event(Log_event_type type_arg = QUERY_EVENT);
  ~Query_event() override {}

#ifndef HAVE_MYSYS
  void print_event_info(std::ostream &info) override;
  void print_long_info(std::ostream &info) override;
#endif
};

/*
  Check how many bytes are available on buffer.

  @param buf_start    Pointer to buffer start.
  @param buf_current  Pointer to the current position on buffer.
  @param buf_len      Buffer length.

  @return             Number of bytes available on event buffer.
*/
template <class T>
T available_buffer(const char *buf_start, const char *buf_current, T buf_len) {
  /* Sanity check */
  if (buf_current < buf_start ||
      buf_len < static_cast<T>(buf_current - buf_start))
    return static_cast<T>(0);

  return static_cast<T>(buf_len - (buf_current - buf_start));
}

/**
  Check if jump value is within buffer limits.

  @param jump         Number of positions we want to advance.
  @param buf_start    Pointer to buffer start
  @param buf_current  Pointer to the current position on buffer.
  @param buf_len      Buffer length.

  @retval      True   If jump value is within buffer limits.
  @retval      False  Otherwise.
*/
template <class T>
bool valid_buffer_range(T jump, const char *buf_start, const char *buf_current,
                        T buf_len) {
  return (jump <= available_buffer(buf_start, buf_current, buf_len));
}

/**
  @class User_var_event

  Written every time a statement uses a user variable; precedes other
  events for the statement. Indicates the value to use for the user
  variable in the next statement. This is written only before a QUERY_EVENT
  and is not used with row-based logging

  The Post-Header has following components:

  <table>
  <caption>Post-Header for Format_description_event</caption>

  <tr>
    <th>Name</th>
    <th>Format</th>
    <th>Description</th>
  </tr>

  <tr>
    <td>Value_type</td>
    <td>enum</td>
    <td>The user variable type.</td>
  </tr>
  <tr>
    <td>User_var_event_data</td>
    <td>enum</td>
    <td>User_var event data</td>
  </tr>
  <tr>
    <td>name</td>
    <td>const char pointer</td>
    <td>User variable name.</td>
  </tr>
  <tr>
    <td>name_len</td>
    <td>unsigned int</td>
    <td>Length of the user variable name</td>
  </tr>
  <tr>
    <td>val</td>
    <td>char pointer</td>
    <td>value of the user variable.</td>
  </tr>
  <tr>
    <td>val_len</td>
    <td>unsigned long</td>
    <td>Length of the value of the user variable</td>
  </tr>
  <tr>
    <td>type</td>
    <td>enum Value_type</td>
    <td>Type of the user variable</td>
  </tr>
  <tr>
    <td>charset_number</td>
    <td>unsigned int</td>
    <td>The number of the character set for the user variable (needed for a
        string variable). The character set number is really a collation
        number that indicates a character set/collation pair.</td>
  </tr>
  <tr>
    <td>is_null</td>
    <td>bool</td>
    <td>Non-zero if the variable value is the SQL NULL value, 0 otherwise.</td>
  </tr>
  </table>
*/
class User_var_event : public Binary_log_event {
 public:
  using Value_type = Item_result;
  enum { UNDEF_F, UNSIGNED_F };
  enum User_var_event_data {
    UV_VAL_LEN_SIZE = 4,
    UV_VAL_IS_NULL = 1,
    UV_VAL_TYPE_SIZE = 1,
    UV_NAME_LEN_SIZE = 4,
    UV_CHARSET_NUMBER_SIZE = 4
  };

  /**
    This constructor will initialize the instance variables and the type_code,
    it will be used only by the server code.
  */
  User_var_event(const char *name_arg, unsigned int name_len_arg, char *val_arg,
                 unsigned long val_len_arg, Value_type type_arg,
                 unsigned int charset_number_arg, unsigned char flags_arg)
      : Binary_log_event(USER_VAR_EVENT),
        name(bapi_strndup(name_arg, name_len_arg)),
        name_len(name_len_arg),
        val(val_arg),
        val_len(val_len_arg),
        type(type_arg),
        charset_number(charset_number_arg),
        is_null(!val),
        flags(flags_arg) {}

  /**
    The constructor receives a buffer and instantiates a User_var_event filled
    in with the data from the buffer
    Written every time a statement uses a user variable, precedes other
    events for the statement. Indicates the value to use for the
    user variable in the next statement. This is written only before a
    QUERY_EVENT and is not used with row-based logging.

    The buffer layout for variable data part is as follows:
    <pre>
    +-------------------------------------------------------------------+
    | name_len | name | is_null | type | charset_number | val_len | val |
    +-------------------------------------------------------------------+
    </pre>

    @param buf  Contains the serialized event.
    @param fde  An FDE event (see Rotate_event constructor for more info).
  */
  User_var_event(const char *buf, const Format_description_event *fde);
  ~User_var_event() override;
  const char *name;
  unsigned int name_len;
  char *val;
  uint32_t val_len;
  Value_type type;
  unsigned int charset_number;
  bool is_null;
  unsigned char flags;
#ifndef HAVE_MYSYS
  void print_event_info(std::ostream &info) override;
  void print_long_info(std::ostream &info) override;
  const char *get_value_type_string(Value_type type_arg) const {
    switch (type_arg) {
      case STRING_RESULT:
        return "String";
      case REAL_RESULT:
        return "Real";
      case INT_RESULT:
        return "Integer";
      case ROW_RESULT:
        return "Row";
      case DECIMAL_RESULT:
        return "Decimal";
      default:
        return "Unknown";
    }
  }
#endif
};

/**
  @class Intvar_event

  An Intvar_event will be created just before a Query_event,
  if the query uses one of the variables LAST_INSERT_ID or INSERT_ID.
  Each Intvar_event holds the value of one of these variables.

  @section Intvar_event_binary_format Binary Format

  The Post-Header for this event type is empty. The Body has two
  components:

  <table>
  <caption>Body for Intvar_event</caption>

  <tr>
    <th>Name</th>
    <th>Format</th>
    <th>Description</th>
  </tr>

  <tr>
    <td>type</td>
    <td>1 byte enumeration</td>
    <td>One byte identifying the type of variable stored.  Currently,
    two identifiers are supported: LAST_INSERT_ID_EVENT == 1 and
    INSERT_ID_EVENT == 2.
    </td>
  </tr>

  <tr>
    <td>val</td>
    <td>8 byte unsigned integer</td>
    <td>The value of the variable.</td>
  </tr>

  </table>
*/
class Intvar_event : public Binary_log_event {
 public:
  uint8_t type;
  uint64_t val;

  /*
    The enum recognizes the type of variables that can occur in an
    INTVAR_EVENT. The two types supported are LAST_INSERT_ID and
    INSERT_ID, in accordance to the SQL query using LAST_INSERT_ID
    or INSERT_ID.
  */
  enum Int_event_type {
    INVALID_INT_EVENT,
    LAST_INSERT_ID_EVENT,
    INSERT_ID_EVENT
  };

  /**
    moving from pre processor symbols from global scope in log_event.h
    to an enum inside the class, since these are used only by
    members of this class itself.
  */
  enum Intvar_event_offset { I_TYPE_OFFSET = 0, I_VAL_OFFSET = 1 };

  /**
    This method returns the string representing the type of the variable
    used in the event. Changed the definition to be similar to that
    previously defined in log_event.cc.
  */
  std::string get_var_type_string() const {
    switch (type) {
      case INVALID_INT_EVENT:
        return "INVALID_INT";
      case LAST_INSERT_ID_EVENT:
        return "LAST_INSERT_ID";
      case INSERT_ID_EVENT:
        return "INSERT_ID";
      default: /* impossible */
        return "UNKNOWN";
    }
  }

  /**
    Constructor receives a packet from the MySQL master or the binary
    log and decodes it to create an Intvar_event.

    The post header for the event is empty. Buffer layout for the variable
    data part is as follows:
    <pre>
      +--------------------------------+
      | type (4 bytes) | val (8 bytes) |
      +--------------------------------+
    </pre>

    @param buf  Contains the serialized event.
    @param fde  An FDE event (see Rotate_event constructor for more info).
  */
  Intvar_event(const char *buf, const Format_description_event *fde);
  /**
   The minimal constructor for Intvar_event it initializes the instance
   variables type & val and set the type_code as INTVAR_EVENT in the header
   object in Binary_log_event
  */
  Intvar_event(uint8_t type_arg, uint64_t val_arg)
      : Binary_log_event(INTVAR_EVENT), type(type_arg), val(val_arg) {}

  ~Intvar_event() override {}

#ifndef HAVE_MYSYS
  void print_event_info(std::ostream &info) override;
  void print_long_info(std::ostream &info) override;
#endif
};

/**
  @class Rand_event

  Logs random seed used by the next RAND(), and by PASSWORD() in 4.1.0.
  4.1.1 does not need it (it's repeatable again) so this event needn't be
  written in 4.1.1 for PASSWORD() (but the fact that it is written is just a
  waste, it does not cause bugs).

  The state of the random number generation consists of 128 bits,
  which are stored internally as two 64-bit numbers.

  @section Rand_event_binary_format Binary Format

  The Post-Header for this event type is empty.  The Body has two
  components:

  <table>
  <caption>Body for Rand_event</caption>

  <tr>
    <th>Name</th>
    <th>Format</th>
    <th>Description</th>
  </tr>

  <tr>
    <td>seed1</td>
    <td>8 byte unsigned integer</td>
    <td>64 bit random seed1.</td>
  </tr>

  <tr>
    <td>seed2</td>
    <td>8 byte unsigned integer</td>
    <td>64 bit random seed2.</td>
  </tr>
  </table>
*/
class Rand_event : public Binary_log_event {
 public:
  unsigned long long seed1;
  unsigned long long seed2;
  enum Rand_event_data { RAND_SEED1_OFFSET = 0, RAND_SEED2_OFFSET = 8 };

  /**
    This will initialize the instance variables seed1 & seed2, and set the
type_code as RAND_EVENT in the header object in Binary_log_event
  */
  Rand_event(unsigned long long seed1_arg, unsigned long long seed2_arg)
      : Binary_log_event(RAND_EVENT) {
    seed1 = seed1_arg;
    seed2 = seed2_arg;
  }

  /**
    Written every time a statement uses the RAND() function; precedes other
    events for the statement. Indicates the seed values to use for generating a
    random number with RAND() in the next statement. This is written only before
    a QUERY_EVENT and is not used with row-based logging

    <pre>
    The buffer layout for variable part is as follows:
    +----------------------------------------------+
    | value for first seed | value for second seed |
    +----------------------------------------------+
    </pre>

    @param buf  Contains the serialized event.
    @param fde  An FDE event (see Rotate_event constructor for more info).
  */
  Rand_event(const char *buf, const Format_description_event *fde);
#ifndef HAVE_MYSYS
  void print_event_info(std::ostream &info) override;
  void print_long_info(std::ostream &info) override;
#endif
};
}  // end namespace binary_log
/**
  @} (end of group Replication)
*/
#endif /* STATEMENT_EVENTS_INCLUDED */
