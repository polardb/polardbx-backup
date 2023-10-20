/* Copyright (c) 2018, 2021, Alibaba and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL/Apsara GalaxySQL hereby grant you an
   additional permission to link the program and your derivative works with the
   separately licensed software that they have included with
   MySQL/Apsara GalaxySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "sys_vars.h"
#include "sys_vars_ext.h"
#include "sql/ha_sequence.h"

#include "sql/xa/lizard_xa_trx.h"

#include "sql_statistics_common.h"
#include "my_config.h"
#include "sql/ccl/ccl.h"
#include "sql/ccl/ccl_bucket.h"
#include "sql/ccl/ccl_interface.h"
#include "sql/recycle_bin/recycle_scheduler.h"
#include "sql/recycle_bin/recycle_table.h"
#include "sql/outline/outline_interface.h"
#include "sql/sys_vars.h"
#include "sql_statistics_common.h"
#include "sys_vars.h"


#include "plugin/performance_point/pps.h"
/**
  Performance_point is statically compiled plugin,
  so include it directly
*/
#include "plugin/performance_point/pps_server.h"
static Sys_var_bool Sys_opt_tablestat("opt_tablestat",
                                      "When this option is enabled,"
                                      "it will accumulate the table statistics",
                                      GLOBAL_VAR(opt_tablestat),
                                      CMD_LINE(OPT_ARG), DEFAULT(true),
                                      NO_MUTEX_GUARD, NOT_IN_BINLOG,
                                      ON_CHECK(nullptr), ON_UPDATE(nullptr));

static Sys_var_bool Sys_opt_indexstat("opt_indexstat",
                                      "When this option is enabled,"
                                      "it will accumulate the index statistics",
                                      GLOBAL_VAR(opt_indexstat),
                                      CMD_LINE(OPT_ARG), DEFAULT(true),
                                      NO_MUTEX_GUARD, NOT_IN_BINLOG,
                                      ON_CHECK(nullptr), ON_UPDATE(nullptr));

// static Sys_var_bool Sys_opt_performance_point_enabled(
//     "performance_point_enabled",
//     "whether open the performance point system plugin",
//     READ_ONLY GLOBAL_VAR(opt_performance_point_enabled), CMD_LINE(OPT_ARG),
//     DEFAULT(true), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(nullptr), ON_UPDATE(nullptr));

// static Sys_var_ulong Sys_performance_point_iostat_volume_size(
//     "performance_point_iostat_volume_size",
//     "The max iostat records that keeped in memory",
//     READ_ONLY GLOBAL_VAR(performance_point_iostat_volume_size),
//     CMD_LINE(REQUIRED_ARG), VALID_RANGE(0, 100000), DEFAULT(10000),
//     BLOCK_SIZE(1));

// static Sys_var_ulong Sys_performance_point_iostat_interval(
//     "performance_point_iostat_interval",
//     "The time interval every iostat aggregation (second time)",
//     GLOBAL_VAR(performance_point_iostat_interval), CMD_LINE(REQUIRED_ARG),
//     VALID_RANGE(1, 60), DEFAULT(2), BLOCK_SIZE(1));

// static Sys_var_bool Sys_opt_performance_point_lock_rwlock_enabled(
//     "performance_point_lock_rwlock_enabled",
//     "Enable Performance Point statement level rwlock aggregation(enabled by "
//     "default)",
//     GLOBAL_VAR(opt_performance_point_lock_rwlock_enabled), CMD_LINE(OPT_ARG),
//     DEFAULT(true), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(0), ON_UPDATE(0));

// static Sys_var_bool Sys_opt_performance_point_dbug_enabled(
//     "performance_point_dbug_enabled", "Enable Performance Point debug mode",
//     GLOBAL_VAR(opt_performance_point_dbug_enabled), CMD_LINE(OPT_ARG),
//     DEFAULT(false), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(0), ON_UPDATE(0));


static bool set_owned_vision_gcn_on_update(sys_var *, THD *thd, enum_var_type) {
  if (thd->variables.innodb_snapshot_gcn == MYSQL_GCN_NULL) {
    thd->owned_vision_gcn.reset();
  } else {
    thd->owned_vision_gcn.set(
        MYSQL_CSR_ASSIGNED, thd->variables.innodb_snapshot_gcn, MYSQL_SCN_NULL);
  }
  return false;
}

static Sys_var_ulonglong Sys_innodb_snapshot_seq(
    "innodb_snapshot_seq", "Innodb snapshot sequence.",
    HINT_UPDATEABLE SESSION_ONLY(innodb_snapshot_gcn), CMD_LINE(REQUIRED_ARG),
    VALID_RANGE(MYSQL_GCN_MIN, MYSQL_GCN_NULL), DEFAULT(MYSQL_GCN_NULL),
    BLOCK_SIZE(1), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(0),
    ON_UPDATE(set_owned_vision_gcn_on_update));

static bool set_owned_commit_gcn_on_update(sys_var *, THD *thd, enum_var_type) {
  thd->owned_commit_gcn.set(thd->variables.innodb_commit_gcn,
                            MYSQL_CSR_ASSIGNED);
  return false;
}

static Sys_var_ulonglong Sys_innodb_commit_seq(
    "innodb_commit_seq", "Innodb commit sequence",
    HINT_UPDATEABLE SESSION_ONLY(innodb_commit_gcn), CMD_LINE(REQUIRED_ARG),
    VALID_RANGE(MYSQL_GCN_MIN, MYSQL_GCN_NULL), DEFAULT(MYSQL_GCN_NULL),
    BLOCK_SIZE(1), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(0),
    ON_UPDATE(set_owned_commit_gcn_on_update));

static Sys_var_bool Sys_only_report_warning_when_skip(
    "only_report_warning_when_skip_sequence",
    "Whether reporting warning when the value skipped to is not valid "
    "instead of raising error",
    GLOBAL_VAR(opt_only_report_warning_when_skip_sequence), CMD_LINE(OPT_ARG),
    DEFAULT(true), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(0), ON_UPDATE(0));

static Sys_var_bool Sys_innodb_current_snapshot_gcn(
    "innodb_current_snapshot_seq",
    "Get snapshot_seq from innodb,"
    "the value is current max snapshot sequence and plus one",
    HINT_UPDATEABLE SESSION_ONLY(innodb_current_snapshot_gcn), CMD_LINE(OPT_ARG),
    DEFAULT(false), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(0), ON_UPDATE(0));

/**
  If enable the hb_freezer, pretend to send heartbeat before updating, so it
  won't be blocked because of timeout.
*/
bool freeze_db_if_no_cn_heartbeat_enable_on_update(sys_var *, THD *,
                                                   enum_var_type) {
  const bool is_enable = lizard::xa::no_heartbeat_freeze;

  /** 1. Pretend to send heartbeat. */
  if (is_enable) {
    lizard::xa::hb_freezer_heartbeat();
  }

  lizard::xa::opt_no_heartbeat_freeze = lizard::xa::no_heartbeat_freeze;
  return false;
}
static Sys_var_bool Sys_freeze_db_if_no_cn_heartbeat_enable(
    "innodb_freeze_db_if_no_cn_heartbeat_enable",
    "If set to true, will freeze purge sys and updating "
    "if there is no heartbeat.",
    GLOBAL_VAR(lizard::xa::no_heartbeat_freeze), CMD_LINE(OPT_ARG),
    DEFAULT(false), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(0),
    ON_UPDATE(freeze_db_if_no_cn_heartbeat_enable_on_update));

static Sys_var_ulonglong Sys_freeze_db_if_no_cn_heartbeat_timeout_sec(
    "innodb_freeze_db_if_no_cn_heartbeat_timeout_sec",
    "If the heartbeat has not been received after the "
    "timeout, freezing the purge sys and updating.",
    GLOBAL_VAR(lizard::xa::opt_no_heartbeat_freeze_timeout),
    CMD_LINE(REQUIRED_ARG), VALID_RANGE(1, 24 * 60 * 60), DEFAULT(10),
    BLOCK_SIZE(1), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(0), ON_UPDATE(0));

extern bool opt_gcn_write_event;
static Sys_var_bool Sys_gcn_write_event(
    "gcn_write_event",
    "Writting a gcn event which content is gcn number for every transaction.",
    READ_ONLY NON_PERSIST GLOBAL_VAR(opt_gcn_write_event),
    CMD_LINE(OPT_ARG), DEFAULT(true), NO_MUTEX_GUARD, NOT_IN_BINLOG,
    ON_CHECK(0), ON_UPDATE(0));

static Sys_var_ulong Sys_ccl_wait_timeout(
    "ccl_wait_timeout", "Timeout in seconds to wait when concurrency control.",
    GLOBAL_VAR(im::ccl_wait_timeout), CMD_LINE(REQUIRED_ARG),
    VALID_RANGE(1, LONG_TIMEOUT), DEFAULT(CCL_LONG_WAIT), BLOCK_SIZE(1));

static Sys_var_ulong Sys_ccl_max_waiting(
    "ccl_max_waiting_count", "max waiting count in one ccl rule or bucket",
    GLOBAL_VAR(im::ccl_max_waiting_count), CMD_LINE(REQUIRED_ARG),
    VALID_RANGE(0, INT_MAX64),
    DEFAULT(CCL_DEFAULT_WAITING_COUNT), BLOCK_SIZE(1), NO_MUTEX_GUARD,
    NOT_IN_BINLOG, ON_CHECK(0), ON_UPDATE(0));

static bool update_ccl_queue(sys_var *, THD *, enum_var_type) {
  im::System_ccl::instance()->get_queue_buckets()->init_queue_buckets(
      im::ccl_queue_bucket_count, im::ccl_queue_bucket_size,
      im::Ccl_error_level::CCL_WARNING);
  return false;
}

static Sys_var_ulong Sys_ccl_queue_size(
    "ccl_queue_bucket_size", "The max concurrency allowed when use ccl queue",
    GLOBAL_VAR(im::ccl_queue_bucket_size), CMD_LINE(REQUIRED_ARG),
    VALID_RANGE(1, CCL_QUEUE_BUCKET_SIZE_MAX),
    DEFAULT(CCL_QUEUE_BUCKET_SIZE_DEFAULT), BLOCK_SIZE(1), NO_MUTEX_GUARD,
    NOT_IN_BINLOG, ON_CHECK(0), ON_UPDATE(update_ccl_queue));

static Sys_var_ulong Sys_ccl_queue_bucket(
    "ccl_queue_bucket_count", "How many groups when use ccl queue",
    GLOBAL_VAR(im::ccl_queue_bucket_count), CMD_LINE(REQUIRED_ARG),
    VALID_RANGE(1, CCL_QUEUE_BUCKET_COUNT_MAX),
    DEFAULT(CCL_QUEUE_BUCKET_COUNT_DEFAULT), BLOCK_SIZE(1), NO_MUTEX_GUARD,
    NOT_IN_BINLOG, ON_CHECK(0), ON_UPDATE(update_ccl_queue));

static Sys_var_bool Sys_recycle_bin(
    "recycle_bin", "Whether recycle the table which is going to be dropped",
    SESSION_VAR(recycle_bin), CMD_LINE(OPT_ARG), DEFAULT(false), NO_MUTEX_GUARD,
    NOT_IN_BINLOG, ON_CHECK(0), ON_UPDATE(0));

static Sys_var_ulong Sys_recycle_scheduler_interval(
    "recycle_scheduler_interval", "Interval in seconds for recycle scheduler.",
    GLOBAL_VAR(im::recycle_bin::recycle_scheduler_interval),
    CMD_LINE(REQUIRED_ARG), VALID_RANGE(1, 60), DEFAULT(30), BLOCK_SIZE(1));

static bool recycle_scheduler_retention_update(sys_var *, THD *,
                                               enum_var_type) {
  im::recycle_bin::Recycle_scheduler::instance()->wakeup();
  return false;
}

static Sys_var_ulong Sys_recycle_bin_retention(
    "recycle_bin_retention",
    "Seconds before really purging the recycled table.",
    GLOBAL_VAR(im::recycle_bin::recycle_bin_retention), CMD_LINE(REQUIRED_ARG),
    VALID_RANGE(1, 365 * 24 * 60 * 60), DEFAULT(7 * 24 * 60 * 60),
    BLOCK_SIZE(1), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(0),
    ON_UPDATE(recycle_scheduler_retention_update));

static bool recycle_scheduler_update(sys_var *, THD *, enum_var_type) {
  bool res = false;
  bool value = im::recycle_bin::opt_recycle_scheduler;
  mysql_mutex_unlock(&LOCK_global_system_variables);

  if (value) {
    res = im::recycle_bin::Recycle_scheduler::instance()->start();
  } else {
    res = im::recycle_bin::Recycle_scheduler::instance()->stop();
  }
  mysql_mutex_lock(&LOCK_global_system_variables);
  if (res) {
    im::recycle_bin::opt_recycle_scheduler = false;
    my_error(ER_EVENT_SET_VAR_ERROR, MYF(0), 0);
  }
  return res;
}

static Sys_var_bool Sys_recycle_scheduler(
    "recycle_scheduler", "Enable the recycle scheduler.",
    GLOBAL_VAR(im::recycle_bin::opt_recycle_scheduler), CMD_LINE(OPT_ARG),
    DEFAULT(false), NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(0),
    ON_UPDATE(recycle_scheduler_update));

static Sys_var_bool Sys_recycle_scheduler_purge_table_print(
    "recycle_scheduler_purge_table_print",
    "Print the recycle scheduler process.",
    GLOBAL_VAR(im::recycle_bin::recycle_scheduler_purge_table_print),
    CMD_LINE(OPT_ARG), DEFAULT(false), NO_MUTEX_GUARD, NOT_IN_BINLOG,
    ON_CHECK(0), ON_UPDATE(0));
static Sys_var_ulong Sys_outline_partitions(
    "outline_partitions", "How many parititon of system outline structure.",
    READ_ONLY GLOBAL_VAR(im::outline_partitions), CMD_LINE(REQUIRED_ARG),
    VALID_RANGE(1, 256), DEFAULT(16), BLOCK_SIZE(1));

static Sys_var_bool Sys_opt_outline_enabled(
    "opt_outline_enabled",
    "When this option is enabled,"
    "it will invoke statement outline when execute sql",
    GLOBAL_VAR(im::opt_outline_enabled), CMD_LINE(OPT_ARG), DEFAULT(true),
    NO_MUTEX_GUARD, NOT_IN_BINLOG, ON_CHECK(0), ON_UPDATE(0));

static Sys_var_bool Sys_outline_allowed_sql_digest_truncate(
    "outline_allowed_sql_digest_truncate",
    "Whether allowed the incomplete of sql digest when add outline",
    SESSION_VAR(outline_allowed_sql_digest_truncate),
    CMD_LINE(OPT_ARG), DEFAULT(true), NO_MUTEX_GUARD,
    NOT_IN_BINLOG, ON_CHECK(0), ON_UPDATE(0));
static Sys_var_bool Sys_auto_savepoint("auto_savepoint",
                                       "Whether to make implicit savepoint for "
                                       "each INSERT/DELETE/UPDATE statement",
                                       SESSION_VAR(auto_savepoint), NO_CMD_LINE,
                                       DEFAULT(FALSE), NO_MUTEX_GUARD,
                                       NOT_IN_BINLOG, ON_CHECK(0),
                                       ON_UPDATE(0));
