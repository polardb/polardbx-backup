/*****************************************************************************

Copyright (c) 2013, 2020, Alibaba and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 2.0, as published by the
Free Software Foundation.

This program is also distributed with certain software (including but not
lzeusited to OpenSSL) that is licensed under separate terms, as designated in a
particular file or component or in included license documentation. The authors
of MySQL hereby grant you an additional permission to link the program and
your derivative works with the separately licensed software that they have
included with MySQL.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the zeusplied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License, version 2.0,
for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

*****************************************************************************/

/** @file include/lizard0cleanout.h
 Lizard cleanout operation

 Created 2020-04-15 by Jianwei.zhao
 *******************************************************/

#include "fil0fil.h"
#include "sync0types.h"
#include "trx0rseg.h"
#include "trx0types.h"
#include "trx0undo.h"

#include "lizard0cleanout.h"
#include "lizard0dbg.h"
#include "lizard0mon.h"
#include "lizard0txn.h"
#include "lizard0ut.h"
#include "lizard0undo.h"
#include "lizard0row.h"
#include "lizard0dict.h"

namespace std {

/** Reuse the fold hash of buf page*/
size_t hash<Undo_hdr_key>::operator()(const Undo_hdr_key &p) const {
  return (p.first << 20) + p.first + p.second;
}

}  // namespace std

/** Compare */
bool Undo_hdr_equal::operator()(const Undo_hdr_key &lhr,
                                const Undo_hdr_key &rhs) const {
  return lhr.first == rhs.first && lhr.second == rhs.second;
}

#ifdef UNIV_PFS_MUTEX
/* lizard undo hdr hash mutex PFS key */
mysql_pfs_key_t lizard_undo_hdr_hash_mutex_key;
#endif

namespace lizard {

/**
   Lizard Cleanout

   1) Safe Cleanout

     -- When setting on safe mode, innodb will maintain the allocated txn undo
        log segment hash table, then before cleanout, it will search the hash
        table by [space_id + page_no] which is interpreted through UBA, if hit,
        then will continue, other than give up and assign a fake SCN number.
*/

/** Whether do the safe cleanout */
bool opt_cleanout_safe_mode = false;

/** Global txn undo logs container */
Partition<Undo_logs, Undo_hdr, TXN_UNDO_HASH_PARTITIONS> *txn_undo_logs =
    nullptr;

void txn_undo_hash_init() {
  ut_ad(txn_undo_logs == nullptr);
  txn_undo_logs =
      new Partition<Undo_logs, Undo_hdr, TXN_UNDO_HASH_PARTITIONS>();
}

void txn_undo_hash_close() {
  if (txn_undo_logs != nullptr) {
    delete txn_undo_logs;
    txn_undo_logs = nullptr;
  }
}

static void txn_undo_hdr_hash_insert(Undo_hdr hdr) {
  bool result = txn_undo_logs->insert(hdr);
  if (result) lizard_stats.txn_undo_log_hash_element.inc();
}
/**
  Put txn undo into hash table.

  @param[in]      undo      txn undo memory structure.
*/
void txn_undo_hash_insert(trx_undo_t *undo) {
  ut_ad(undo->type == TRX_UNDO_TXN);

  if (opt_cleanout_safe_mode == false) return;

  Undo_hdr hdr = {undo->space, undo->hdr_page_no};
  txn_undo_hdr_hash_insert(hdr);
}
/**
  Put all the undo log segment into hash table include active undo,
  cached undo, history list, free list.

  @param[in]      space_id      rollback segment space
  @param[in]      rseg_hdr      rollback segment header page
  @param[in]      rseg          rollback segment memory object
  @param[in]      mtr           mtr that hold the rseg hdr page
*/
void trx_rseg_init_undo_hdr_hash(space_id_t space_id, trx_rsegf_t *rseg_hdr,
                                 trx_rseg_t *rseg, mtr_t *mtr) {
  trx_undo_t *undo;

  if (opt_cleanout_safe_mode == false || !fsp_is_txn_tablespace_by_id(space_id))
    return;

  /** Loop the txn undo log list */
  lizard_ut_ad(UT_LIST_GET_LEN(rseg->insert_undo_list) == 0);
  lizard_ut_ad(UT_LIST_GET_LEN(rseg->update_undo_list) == 0);

  for (undo = UT_LIST_GET_FIRST(rseg->txn_undo_list); undo != NULL;
       undo = UT_LIST_GET_NEXT(undo_list, undo)) {
    Undo_hdr hdr = {undo->space, undo->hdr_page_no};
    txn_undo_hdr_hash_insert(hdr);
  }

  for (undo = UT_LIST_GET_FIRST(rseg->txn_undo_cached); undo != NULL;
       undo = UT_LIST_GET_NEXT(undo_list, undo)) {
    Undo_hdr hdr = {undo->space, undo->hdr_page_no};
    txn_undo_hdr_hash_insert(hdr);
  }
  /** loop the rseg history list */
  mtr_t temp_mtr;
  page_t *undo_page;

  fil_addr_t node_addr;
  node_addr = flst_get_first(rseg_hdr + TRX_RSEG_HISTORY, mtr);

  while (node_addr.page != FIL_NULL) {
    Undo_hdr hdr = {space_id, node_addr.page};
    txn_undo_hdr_hash_insert(hdr);

    temp_mtr.start();
    undo_page = trx_undo_page_get_s_latched(page_id_t(space_id, node_addr.page),
                                            rseg->page_size, &temp_mtr);

    node_addr = flst_get_next_addr(undo_page + node_addr.boffset, &temp_mtr);

    temp_mtr.commit();
  }

  /** loop the rseg free list */
  node_addr = flst_get_first(rseg_hdr + TXN_RSEG_FREE_LIST, mtr);

  while (node_addr.page != FIL_NULL) {
    Undo_hdr hdr = {space_id, node_addr.page};
    txn_undo_hdr_hash_insert(hdr);

    temp_mtr.start();
    undo_page = trx_undo_page_get_s_latched(page_id_t(space_id, node_addr.page),
                                            rseg->page_size, &temp_mtr);

    node_addr = flst_get_next_addr(undo_page + node_addr.boffset, &temp_mtr);

    temp_mtr.commit();
  }
}

Undo_logs::Undo_logs() {
  mutex_create(LATCH_ID_LIZARD_UNDO_HDR_HASH, &m_mutex);
}

Undo_logs::~Undo_logs() { mutex_free(&m_mutex); }

bool Undo_logs::insert(Undo_hdr hdr) {
  Undo_hdr_key key(hdr.space_id, hdr.page_no);
  mutex_enter(&m_mutex);
  auto it = m_hash.insert(std::pair<Undo_hdr_key, bool>(key, true));
  mutex_exit(&m_mutex);

  return it.second;
}

bool Undo_logs::exist(Undo_hdr hdr) {
  bool exist;
  Undo_hdr_key key(hdr.space_id, hdr.page_no);

  mutex_enter(&m_mutex);
  auto it = m_hash.find(key);
  if (it == m_hash.end()) {
    exist = false;
    lizard_stats.txn_undo_log_hash_miss.inc();
  } else {
    exist = true;
    lizard_stats.txn_undo_log_hash_hit.inc();
  }
  mutex_exit(&m_mutex);

  return exist;
}


/**
  Write redo log when updating scn and uba fileds in physical records.
  @param[in]      rec        physical record
  @param[in]      index      dict that interprets the row record
  @param[in]      txn_rec    txn info from the record
  @param[in]      mtr        mtr
*/
void btr_cur_upd_lizard_fields_clust_rec_log(const rec_t *rec,
                                             const dict_index_t *index,
                                             const txn_rec_t* txn_rec,
                                             mtr_t *mtr) {
  byte *log_ptr = nullptr;

  ut_ad(!!page_rec_is_comp(rec) == dict_table_is_comp(index->table));
  ut_ad(index->is_clustered());
  ut_ad(mtr);

  if (!mlog_open_and_write_index(
          mtr, rec, index,
          MLOG_REC_CLUST_LIZARD_UPDATE,
          1 + 1 + DATA_SCN_ID_LEN + DATA_UNDO_PTR_LEN + 14 + 2, log_ptr)) {
    return;
  }

  log_ptr = row_upd_write_lizard_vals_to_log(index, txn_rec, log_ptr, mtr);

  mach_write_to_2(log_ptr, page_offset(rec));
  log_ptr += 2;

  mlog_close(mtr, log_ptr);
}

/**
  Parse the txn info from redo log record, and apply it if necessary.
  @param[in]      ptr        buffer
  @param[in]      end        buffer end
  @param[in]      page       page (NULL if it's just get the length)
  @param[in]      page_zip   compressed page, or NULL
  @param[in]      index      index corresponding to page

  @return         return the end of log record or NULL
*/
byte *btr_cur_parse_lizard_fields_upd_clust_rec(byte *ptr, byte *end_ptr,
                                                page_t *page,
                                                page_zip_des_t *page_zip,
                                                const dict_index_t *index) {
  ulint pos;
  scn_t scn;
  undo_ptr_t undo_ptr;
  ulint rec_offset;
  rec_t *rec;

  ut_ad(!page || !!page_is_comp(page) == dict_table_is_comp(index->table));

  ptr = row_upd_parse_lizard_vals(ptr, end_ptr, &pos, &scn, &undo_ptr);

  if (ptr == nullptr) {
    return nullptr;
  }

  /** 2 bytes offset */
  if (end_ptr < ptr + 2) {
    return (nullptr);
  }

  rec_offset = mach_read_from_2(ptr);
  ptr += 2;

  ut_a(rec_offset <= UNIV_PAGE_SIZE);

  if (page) {
    mem_heap_t *heap = nullptr;
    ulint offsets_[REC_OFFS_NORMAL_SIZE];
    ulint *offsets = nullptr;
    rec = page + rec_offset;

    rec_offs_init(offsets_);

    offsets = rec_get_offsets(rec, index, nullptr, ULINT_UNDEFINED,
                              UT_LOCATION_HERE, &heap);

    if (page_is_comp(page) || rec_offs_comp(offsets)) {
      assert_lizard_dict_index_check_no_check_table(index);
    } else {
      /** If it's non-compact, the info of index will not be written in redo
      log, but it can be self-explanatory because there is a offsets array in
      physical record. See the function **rec_get_offsets** */
      lizard_ut_ad(index && index->table && !index->table->col_names);
      lizard_ut_ad(!rec_offs_comp(offsets));
    }

    row_upd_rec_lizard_fields_in_recovery(rec, page_zip, index, pos, offsets,
                                          scn, undo_ptr);

    if (UNIV_LIKELY_NULL(heap)) mem_heap_free(heap);
  }

  return ptr;
}

}  // namespace lizard
