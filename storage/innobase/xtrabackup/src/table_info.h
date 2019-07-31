/******************************************************
XtraBackup: hot backup tool for InnoDB
(c) 2009-2018 Aliyun and/or its affiliates.
Originally Created 10/06/2018 Fungo Wang
Written by Fungo Wang

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

/* Table information for XtraBackup */

#ifndef TABLE_INFO_H
#define TABLE_INFO_H

bool table_info_init();

void *xb_prepare_table_info(const char *filepath);

void xb_finish_table_info(void *info_p);

bool serialize_table_info_and_backup(ds_ctxt_t *ds);

void table_info_deinit();

#endif /* TABLE_INFO_H */
