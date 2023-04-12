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


static Sys_var_ulonglong Sys_innodb_snapshot_seq(
    "innodb_snapshot_seq", "Innodb snapshot sequence.",
    HINT_UPDATEABLE SESSION_ONLY(innodb_snapshot_gcn), CMD_LINE(REQUIRED_ARG),
    VALID_RANGE(1024, MYSQL_GCN_NULL), DEFAULT(MYSQL_GCN_NULL), BLOCK_SIZE(1));

static Sys_var_ulonglong Sys_innodb_commit_seq(
    "innodb_commit_seq", "Innodb commit sequence",
    HINT_UPDATEABLE SESSION_ONLY(innodb_commit_gcn), CMD_LINE(REQUIRED_ARG),
    VALID_RANGE(1024, MYSQL_GCN_NULL), DEFAULT(MYSQL_GCN_NULL), BLOCK_SIZE(1));

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
