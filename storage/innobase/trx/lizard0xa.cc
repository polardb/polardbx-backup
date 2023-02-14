/*****************************************************************************

Copyright (c) 2013, 2021, Alibaba and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 2.0, as published by the
Free Software Foundation.

This program is also distributed with certain software (including but not
limited to OpenSSL) that is licensed under separate terms, as designated in a
particular file or component or in included license documentation. The authors
of MySQL hereby grant you an additional permission to link the program and
your derivative works with the separately licensed software that they have
included with MySQL.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License, version 2.0,
for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

*****************************************************************************/

/** @file trx/lizard0xa.cc
  Lizard XA transaction structure.

 Created 2021-08-10 by Jianwei.zhao
 *******************************************************/

#include "lizard0xa.h"
#include "lizard0read0types.h"
#include "m_ctype.h"
#include "mysql/plugin.h"
#include "sql/xa.h"
#include "trx0sys.h"

/** The following function, which is really good to hash different fields, is
copyed from boost::hash_combine. */
template <class T>
static inline void hash_combine(std::size_t &s, const T &v) {
  std::hash<T> h;
  s ^= h(v) + 0x9e3779b9 + (s << 6) + (s >> 2);
}

std::size_t hash_xid(const XID *xid) {
  std::size_t res = 0;
  auto formatID = xid->get_format_id();
  const char *data = xid->get_data();
  std::string gtrid(data, xid->get_gtrid_length());
  std::string bqual(data + xid->get_gtrid_length(), xid->get_bqual_length());

  hash_combine(res, formatID);
  hash_combine(res, gtrid);
  hash_combine(res, bqual);

  return res;
}

