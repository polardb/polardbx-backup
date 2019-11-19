/******************************************************
Copyright (c) 2011-2019 Percona LLC and/or its affiliates.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

*******************************************************/

#ifndef XTRABACKUP_BACKUP_MYSQL_H
#define XTRABACKUP_BACKUP_MYSQL_H

#include <mysql.h>
#include <string>
#include <unordered_set>
#include <vector>

#include "xtrabackup.h"

/* mysql flavor and version */
enum mysql_flavor_t {
  FLAVOR_UNKNOWN,
  FLAVOR_MYSQL,
  FLAVOR_PERCONA_SERVER,
  FLAVOR_MARIADB,
  FLAVOR_X_CLUSTER
};
extern mysql_flavor_t server_flavor;
extern unsigned long mysql_server_version;

struct replication_channel_status_t {
  std::string channel_name;
  std::string relay_log_file;
  uint64_t relay_log_position;
  std::string relay_master_log_file;
  uint64_t exec_master_log_position;
};

struct rocksdb_wal_t {
  size_t log_number;
  std::string path_name;
  size_t file_size_bytes;
};

struct log_status_t {
  std::string filename;
  uint64_t position;
  std::string gtid_executed;
  lsn_t lsn;
  lsn_t lsn_checkpoint;
  std::vector<replication_channel_status_t> channels;
  std::vector<rocksdb_wal_t> rocksdb_wal_files;
};

struct mysql_variable {
  const char *name;
  char **value;
};

#define ROCKSDB_SUBDIR ".rocksdb"

class Myrocks_datadir {
 public:
  using file_list = std::vector<datadir_entry_t>;

  using const_iterator = file_list::const_iterator;

  Myrocks_datadir(const std::string &datadir,
                  const std::string &wal_dir = std::string()) {
    rocksdb_datadir = datadir;
    rocksdb_wal_dir = wal_dir;
  }

  file_list files(const char *dest_data_dir = ROCKSDB_SUBDIR,
                  const char *dest_wal_dir = ROCKSDB_SUBDIR) const;

  file_list data_files(const char *dest_datadir = ROCKSDB_SUBDIR) const;

  file_list wal_files(const char *dest_wal_dir = ROCKSDB_SUBDIR) const;

  file_list meta_files(const char *dest_wal_dir = ROCKSDB_SUBDIR) const;

 private:
  std::string rocksdb_datadir;
  std::string rocksdb_wal_dir;

  enum scan_type_t { SCAN_ALL, SCAN_WAL, SCAN_DATA, SCAN_META };

  void scan_dir(const std::string &dir, const char *dest_data_dir,
                const char *dest_wal_dir, scan_type_t scan_type,
                file_list &result) const;
};

class Myrocks_checkpoint {
 private:
  std::string checkpoint_dir;
  std::string rocksdb_datadir;
  std::string rocksdb_wal_dir;

  MYSQL *con;

 public:
  using file_list = Myrocks_datadir::file_list;

  Myrocks_checkpoint() {}

  /* create checkpoint and optionally disable file deletions */
  void create(MYSQL *con, bool disable_file_deletions);

  /* remove checkpoint */
  void remove() const;

  /* enable file deletions */
  void enable_file_deletions() const;

  /* get the list of live WAL files */
  file_list wal_files(const log_status_t &log_status) const;

  /* get the list of checkpoint files */
  file_list checkpoint_files(const log_status_t &log_status) const;

  /* get the list of sst files */
  file_list data_files() const;
};

#define XENGINE_BACKUP_TMP_DIR "hotbackup_tmp"
#define XENGINE_BACKUP_EXTENT_IDS_FILE "extent_ids.inc"
#define XENGINE_BACKUP_EXTENTS_FILE "extent.inc"

class Xengine_datadir
{
public:
  Xengine_datadir(const std::string &xengine_backup_dir)
      : xengine_backup_dir_(xengine_backup_dir)
  {}
  virtual ~Xengine_datadir() {}

  using file_list = std::vector<datadir_entry_t>;
  using const_iterator = file_list::const_iterator;

  // Get all files from xengine backup dir
  file_list files(const char *dest_dir) const;

  // Get all wal files from xengine backup dir
  file_list wal_files(const char *dest_dir) const;

  file_list copy_back_files(const char *dest_dir) const;

private:
  void scan_dir(const std::string &dir, const char *dest_dir, file_list &result) const;

private:
  const std::string xengine_backup_dir_;
};

class Xengine_backup
{
  struct extent_copy_t
  {
    extent_copy_t(const int64_t idx, const int32_t file_num,
        const int32_t offset, const File file)
        : idx_(idx), file_num_(file_num), offset_(offset), sst_file_(file)
    {}
    // The index in extent_ids.inc
    int64_t idx_;
    // The file number of this extent
    int32_t file_num_;
    // The offset of this extent in the file
    int32_t offset_;
    // The fd of sst file
    File sst_file_;
  };

  using extent_list = std::vector<extent_copy_t>;
  using file_list = Xengine_datadir::file_list;
  using copy_extent_func = std::function<void (const extent_list::const_iterator &,
                                               const extent_list::const_iterator &,
                                               size_t)>;
  using copy_file_func = std::function<void (const file_list::const_iterator &,
                                             const file_list::const_iterator &,
                                             size_t)>;

public:
  Xengine_backup() : con_(nullptr)
  {}

  virtual ~Xengine_backup();
  // Do a checkpoint in xengine and start xengine backup
  bool do_checkpoint();
  // Acquire xengine snapshots, must be invoked in backup lock
  bool acquire_snapshots();
  // Record incremental extent ids between do_checkpoint to acuqire_snapshots
  bool record_incemental_extent_ids();
  // Record incremental extents according to incremental extent ids into a file
  bool record_incemental_extents();
  // Release xengine snapshots
  bool release_snapshots();
  // In prepare, copy incremental extents to backuped sst files
  bool replay_sst_files(const std::string backup_dir_path);
  // Parallel copy files
  bool copy_files(ds_ctxt_t *ds, file_list &files, const bool record_files = true);

private:
  bool read_and_copy_extent_content(const std::string &backup_tmp_dir_path,
                                    const int sst_open_flags,
                                    copy_extent_func &copy);

  std::string make_table_file_name(const std::string &path, uint64_t number);

  static void par_copy_extents(const extent_list::const_iterator &start,
                               const extent_list::const_iterator &end,
                               size_t thread_n,
                               File dest_file,
                               bool *result);

  static void par_replay_extents(const extent_list::const_iterator &start,
                                 const extent_list::const_iterator &end,
                                 size_t thread_n,
                                 File src_file,
                                 bool *result);

  static void par_copy_xengine_files(const file_list::const_iterator &start,
                                     const file_list::const_iterator &end,
                                     size_t thread_n,
                                     ds_ctxt_t *ds,
                                     bool *result);

private:
  static const int64_t EXTENT_IDS_BUF_SIZE = 16 * 1024;
  static const int64_t extent_size = 2 * 1024 * 1024;
  MYSQL *con_;
  std::set<std::string> copied_files_;
};

struct Backup_context {
  log_status_t log_status;
  Myrocks_checkpoint myrocks_checkpoint;
  std::unordered_set<std::string> rocksdb_files;
  Xengine_backup xengine_backup;
};

/* server capabilities */
extern bool have_changed_page_bitmaps;
extern bool have_backup_locks;
extern bool have_lock_wait_timeout;
extern bool have_galera_enabled;
extern bool have_flush_engine_logs;
extern bool have_multi_threaded_slave;
extern bool have_gtid_slave;
extern bool have_rocksdb;
extern bool have_xengine;

/* History on server */
extern time_t history_start_time;
extern time_t history_end_time;
extern time_t history_lock_time;
extern time_t history_innodb_log_backup_time;
extern time_t history_innodb_data_backup_time;
extern time_t backup_consistent_time;

extern bool sql_thread_started;
extern std::string mysql_slave_position;
extern std::string mysql_binlog_position;
extern char *buffer_pool_filename;

/** connection to mysql server */
extern MYSQL *mysql_connection;

void capture_tool_command(int argc, char **argv);

bool select_history();

bool flush_changed_page_bitmaps();

void backup_cleanup();

bool get_mysql_vars(MYSQL *connection);

bool detect_mysql_capabilities_for_backup();

MYSQL *xb_mysql_connect();

MYSQL_RES *xb_mysql_query(MYSQL *connection, const char *query, bool use_result,
                          bool die_on_error = true);

my_ulonglong xb_mysql_numrows(MYSQL *connection, const char *query,
                              bool die_on_error);

char *read_mysql_one_value(MYSQL *connection, const char *query);

void read_mysql_variables(MYSQL *connection, const char *query,
                          mysql_variable *vars, bool vertical_result);

void free_mysql_variables(mysql_variable *vars);

void unlock_all(MYSQL *connection);

bool write_current_binlog_file(MYSQL *connection);

void get_key_id_filename(MYSQL *connection, char *filename, size_t len);

/** Read binaty log position and InnoDB LSN from p_s.log_status.
@param[in]   conn         mysql connection handle */
const log_status_t &log_status_get(MYSQL *conn);

/*********************************************************************/ /**
 Retrieves MySQL binlog position and
 saves it in a file. It also prints it to stdout.
 @param[in]   connection  MySQL connection handler
 @return true if success. */
bool write_binlog_info(MYSQL *connection);

char *get_xtrabackup_info(MYSQL *connection);

bool write_xtrabackup_info(MYSQL *connection);

bool write_backup_config_file();

bool write_xtrabackup_xengine_info();

bool lock_tables_for_backup(MYSQL *connection, int timeout, int retry_count);

bool lock_tables_maybe(MYSQL *connection, int timeout, int retry_count);

bool wait_for_safe_slave(MYSQL *connection);

bool write_galera_info(MYSQL *connection);

bool write_slave_info(MYSQL *connection);

void parse_show_engine_innodb_status(MYSQL *connection);

void mdl_lock_init();

void mdl_lock_table(ulint space_id);

void mdl_unlock_all();

bool has_innodb_buffer_pool_dump();

bool has_innodb_buffer_pool_dump_pct();

void dump_innodb_buffer_pool(MYSQL *connection);

void check_dump_innodb_buffer_pool(MYSQL *connection);

#endif
