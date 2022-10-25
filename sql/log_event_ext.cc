/* Copyright (c) 2000, 2019, Alibaba and/or its affiliates. All rights reserved.

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

#include "sql/log_event.h"

#ifndef MYSQL_SERVER
void Gcn_log_event::print(FILE *, PRINT_EVENT_INFO *print_event_info) const {
  DBUG_ASSERT(flags != 0);
  IO_CACHE *const head = &print_event_info->head_cache;

  if (!print_event_info->short_form)
  {
    print_header(head, print_event_info, false);
    my_b_printf(head, "\tGcn\n");
  }

  if (flags & FLAG_COMMITTED_GCN) {
    my_b_printf(head, "SET @@session.innodb_commit_seq=%llu%s\n",
                (ulonglong)commit_gcn, print_event_info->delimiter);
  }
}
#endif

