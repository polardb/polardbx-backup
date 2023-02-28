/* Copyright (c) 2018, 2023, Alibaba and/or its affiliates. All rights reserved.

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

#include "storage/innobase/include/lizard0xa0iface.h"
#include "sql/xa/xa_proc.h"

namespace lizard {
namespace xa {
int binlog_start_trans(THD *thd);
}
}  // namespace lizard

namespace im {
/* All concurrency control system memory usage */
PSI_memory_key key_memory_xa_proc;

/* The uniform schema name for xa */
const LEX_STRING XA_PROC_SCHEMA = {C_STRING_WITH_LEN("dbms_xa")};

/* Singleton instance for find_by_gtrid */
Proc *Xa_proc_find_by_gtrid::instance() {
  static Proc *proc = new Xa_proc_find_by_gtrid(key_memory_xa_proc);
  return proc;
}

Sql_cmd *Xa_proc_find_by_gtrid::evoke_cmd(THD *thd, List<Item> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

/**
  Parse the GTRID from the parameter list

  @param[in]  list    parameter list
  @param[out] gtrid   GTRID
  @param[out] length  length of gtrid

  @retval     true if parsing error.
*/
bool get_gtrid(const List<Item> *list, char *gtrid, unsigned &length) {
  char buff[128];

  String str(buff, sizeof(buff), system_charset_info);
  String *res;

  /* gtrid */
  res = (*list)[0]->val_str(&str);
  length = res->length();
  if (length > MAXGTRIDSIZE) {
    return true;
  }
  memcpy(gtrid, res->ptr(), length);

  return false;
}

/**
  Parse the XID from the parameter list

  @param[in]  list  parameter list
  @param[out] xid   XID

  @retval     true if parsing error.
*/
bool get_xid(const List<Item> *list, XID *xid) {
  char buff[256];
  char gtrid[MAXGTRIDSIZE];
  char bqual[MAXBQUALSIZE];
  size_t gtrid_length;
  size_t bqual_length;
  size_t formatID;

  String str(buff, sizeof(buff), system_charset_info);
  String *res;

  /* gtrid */
  res = (*list)[0]->val_str(&str);
  gtrid_length = res->length();
  if (gtrid_length > MAXGTRIDSIZE) {
    return true;
  }
  memcpy(gtrid, res->ptr(), gtrid_length);

  /* bqual */
  res = (*list)[1]->val_str(&str);
  bqual_length = res->length();
  if (bqual_length > MAXBQUALSIZE) {
    return true;
  }
  memcpy(bqual, res->ptr(), bqual_length);

  /* formatID */
  formatID = (*list)[2]->val_int();

  /** Set XID. */
  xid->set(formatID, gtrid, gtrid_length, bqual, bqual_length);

  return false;
}

bool Sql_cmd_xa_proc_find_by_gtrid::pc_execute(THD *) {
  DBUG_ENTER("Sql_cmd_xa_proc_find_by_gtrid::pc_execute");
  DBUG_RETURN(false);
}

void Sql_cmd_xa_proc_find_by_gtrid::send_result(THD *thd, bool error) {
  DBUG_ENTER("Sql_cmd_xa_proc_find_by_gtrid::send_result");

  Protocol *protocol;
  XID xid;
  lizard::xa::Transaction_info info;
  bool found;
  char gtrid[MAXGTRIDSIZE];
  unsigned gtrid_length;

  protocol = thd->get_protocol();

  if (error) {
    DBUG_ASSERT(thd->is_error());
    DBUG_VOID_RETURN;
  }

  if (get_gtrid(m_list, gtrid, gtrid_length)) {
    my_error(ER_XA_PROC_WRONG_GTRID, MYF(0), MAXGTRIDSIZE);
    DBUG_VOID_RETURN;
  }

  if (m_proc->send_result_metadata(thd)) DBUG_VOID_RETURN;

  found = lizard::xa::get_transaction_info_by_gtrid(gtrid, gtrid_length, &info);

  if (found) {
    protocol->start_row();
    protocol->store((ulonglong)info.gcn);

    const char *state = lizard::xa::transaction_state_to_str(info.state);
    protocol->store_string(state, strlen(state), system_charset_info);

    if (protocol->end_row()) DBUG_VOID_RETURN;
  }

  my_eof(thd);
  DBUG_VOID_RETURN;
}

Proc *Xa_proc_prepare_with_trx_slot::instance() {
  static Proc *proc = new Xa_proc_prepare_with_trx_slot(key_memory_xa_proc);
  return proc;
}

Sql_cmd *Xa_proc_prepare_with_trx_slot::evoke_cmd(THD *thd, List<Item> *list) const {
  return new (thd->mem_root) Sql_cmd_type(thd, list, this);
}

class Xa_active_pretender {
 public:
  Xa_active_pretender(THD *thd) {
    m_xid_state = thd->get_transaction()->xid_state();

    DBUG_ASSERT(m_xid_state->has_state(XID_STATE::XA_IDLE));

    m_xid_state->set_state(XID_STATE::XA_ACTIVE);
  }

  ~Xa_active_pretender() {
    DBUG_ASSERT(m_xid_state->has_state(XID_STATE::XA_ACTIVE));
    m_xid_state->set_state(XID_STATE::XA_IDLE);
  }

 private:
  XID_STATE *m_xid_state;
};

bool Sql_cmd_xa_proc_prepare_with_trx_slot::pc_execute(THD *thd) {
  DBUG_ENTER("Sql_cmd_xa_proc_prepare_with_trx_slot");

  XID xid;
  XID_STATE *xid_state = thd->get_transaction()->xid_state();

  /** 1. parsed XID from parameters list. */
  if (get_xid(m_list, &xid)) {
    my_error(ER_XA_PROC_WRONG_XID, MYF(0), MAXGTRIDSIZE, MAXBQUALSIZE);
    DBUG_RETURN(true);
  }

  /** 2. Check whether it is an xa transaction that has completed "XA END" */
  if (!xid_state->has_state(XID_STATE::XA_IDLE)) {
    my_error(ER_XAER_RMFAIL, MYF(0), xid_state->state_name());
    DBUG_RETURN(true);
  } else if (!xid_state->has_same_xid(&xid)) {
    my_error(ER_XAER_NOTA, MYF(0));
    DBUG_RETURN(true);
  }

  {
    /**
      Lizard: In the past, the binlog can only be registered in the XA ACTIVE
      state. But now the registration is completed in the IDLE state.

      So the pretender is introduced to pretend the XA is still in ACTIVE
      status.
    */
    Xa_active_pretender pretender(thd);

    /** 3. Try to assign a transaction slot. */
    if (lizard::xa::trx_slot_assign_for_xa(thd, &m_tsa)) {
      my_error(ER_XA_PROC_BLANK_XA_TRX, MYF(0));
      DBUG_RETURN(true);
    }

    /** 4. Register binlog handlerton. */
    if (lizard::xa::binlog_start_trans(thd)) {
      my_error(ER_XA_PROC_START_BINLOG_WRONG, MYF(0));
      DBUG_RETURN(true);
    }
  }

  /** 5. Do xa prepare. */
  Sql_cmd_xa_prepare *executor = new (thd->mem_root) Sql_cmd_xa_prepare(&xid);
  executor->set_delay_ok();
  if (executor->execute(thd)) {
    DBUG_RETURN(true);
  }

  DBUG_RETURN(false);
}

void Sql_cmd_xa_proc_prepare_with_trx_slot::send_result(THD *thd, bool error) {
  DBUG_ENTER("Sql_cmd_xa_proc_prepare_with_trx_slot::send_result");
  Protocol *protocol;

  if (error) {
    DBUG_ASSERT(thd->is_error());
    DBUG_VOID_RETURN;
  }

  protocol = thd->get_protocol();

  if (m_proc->send_result_metadata(thd)) DBUG_VOID_RETURN;

  protocol->start_row();
  DBUG_ASSERT(strlen(server_uuid_ptr) <= 256);
  protocol->store_string(server_uuid_ptr, strlen(server_uuid_ptr),
                         system_charset_info);
  protocol->store((ulonglong)m_tsa);
  if (protocol->end_row()) DBUG_VOID_RETURN;

  my_eof(thd);
  DBUG_VOID_RETURN;
}
}
