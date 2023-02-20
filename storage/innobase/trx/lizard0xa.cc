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
#include "lizard0xa0iface.h"
#include "lizard0undo.h"
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

namespace lizard {
namespace xa {

const char *Transaction_state_str[] = {"COMMIT", "ROLLBACK"};

bool get_transaction_info_by_xid(const XID *xid, Transaction_info *info) {
  trx_rseg_t *rseg;
  txn_undo_hdr_t txn_hdr;
  bool found;

  rseg = get_txn_rseg_by_xid(xid);

  ut_ad(rseg);

  found = txn_rseg_find_trx_info_by_xid(rseg, xid, &txn_hdr);

  if (found) {
    switch (txn_hdr.state) {
      case TXN_UNDO_LOG_COMMITED:
      case TXN_UNDO_LOG_PURGED:
        info->state = txn_hdr.is_rollback() ? TRANS_STATE_ROLLBACK
                                            : TRANS_STATE_COMMITTED;
        break;
      case TXN_UNDO_LOG_ACTIVE:
        /** Can't be active. */
        /** fall through */
      default:
        ut_error;
    }
    info->gcn = txn_hdr.image.gcn;
  }

  return found;
}

const char *transaction_state_to_str(const enum Transaction_state state) {
  return Transaction_state_str[state];
}

}  // namespace xa
}  // namespace lizard

