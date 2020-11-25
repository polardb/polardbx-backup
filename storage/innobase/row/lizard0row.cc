/*****************************************************************************

Copyright (c) 2013, 2020, Alibaba and/or its affiliates. All Rights Reserved.

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

/** @file include/lizard0row.cc
 lizard row operation.

 Created 2020-04-06 by Jianwei.zhao
 *******************************************************/

#include "lizard0row.h"
#include "lizard0data0types.h"
#include "lizard0undo.h"
#include "lizard0dict.h"
#include "lizard0cleanout.h"
#include "lizard0page.h"

#include "my_dbug.h"

#include "que0que.h"
#include "row0ins.h"
#include "row0log.h"
#include "row0row.h"
#include "row0upd.h"

#ifdef UNIV_DEBUG
extern void page_zip_header_cmp(const page_zip_des_t *, const byte *);
#endif /* UNIV_DEBUG */

namespace lizard {

/*=============================================================================*/
/* lizard record insert */
/*=============================================================================*/

/**
  Allocate row buffers for lizard fields.

  @param[in]      node      Insert node
*/
void ins_alloc_lizard_fields(ins_node_t *node) {
  dict_table_t *table;
  byte *ptr;
  dtuple_t *row;
  mem_heap_t *heap;
  const dict_col_t *col;
  dfield_t *dfield;

  row = node->row;
  table = node->table;
  heap = node->entry_sys_heap;

  ut_ad(row && table && heap);
  ut_ad(dtuple_get_n_fields(row) == table->get_n_cols());

  /** instrinsic table didn't need the SCN and UBA columns. */
  if (table->is_intrinsic()) return;

  ptr = static_cast<byte *>(mem_heap_zalloc(heap, DATA_LIZARD_TOTAL_LEN));

  /* 1. Populate scn it */
  col = table->get_sys_col(DATA_SCN_ID);
  dfield = dtuple_get_nth_field(row, dict_col_get_no(col));
  dfield_set_data(dfield, ptr, DATA_SCN_ID_LEN);
  ptr += DATA_SCN_ID_LEN;

  /* 2. Populate UBA */
  col = table->get_sys_col(DATA_UNDO_PTR);
  dfield = dtuple_get_nth_field(row, dict_col_get_no(col));
  dfield_set_data(dfield, ptr, DATA_UNDO_PTR_LEN);
}

/*=============================================================================*/
/* lizard record update */
/*=============================================================================*/

/**
  Fill SCN and UBA into index entry.
  @param[in]    thr       query
  @param[in]    entry     dtuple
  @param[in]    index     cluster index
*/
void row_upd_index_entry_lizard_field(que_thr_t *thr, dtuple_t *entry,
                                      dict_index_t *index) {
  dfield_t *dfield;
  byte *ptr;
  ulint pos;
  const txn_desc_t *txn_desc = nullptr;

  ut_ad(thr && entry && index);
  ut_ad(index->is_clustered());
  ut_ad(!index->table->is_intrinsic());

  if (index->table->is_temporary()) {
    txn_desc = &TXN_DESC_TEMP;
  } else {
    trx_t *trx = thr_get_trx(thr);
    assert_txn_desc_allocated(trx);
    txn_desc = &trx->txn_desc;
  }

  /** 1. Populate SCN */
  pos = index->get_sys_col_pos(DATA_SCN_ID);
  dfield = dtuple_get_nth_field(entry, pos);
  ptr = static_cast<byte *>(dfield_get_data(dfield));
  trx_write_scn(ptr, txn_desc);

  /** 2. Populate UBA */
  pos = index->get_sys_col_pos(DATA_UNDO_PTR);
  dfield = dtuple_get_nth_field(entry, pos);
  ptr  = static_cast<byte*>(dfield_get_data(dfield));
  trx_write_undo_ptr(ptr, txn_desc);
}

/**
  Get address of scn field in record.
  @param[in]      rec       record
  @paramp[in]     index     cluster index
  @param[in]      offsets   rec_get_offsets(rec, idnex)
  @retval pointer to scn_id
*/
byte *row_get_scn_ptr_in_rec(rec_t *rec, const dict_index_t *index,
                             const ulint *offsets) {
  ulint len;
  ulint scn_pos;
  byte *field;
  ut_ad(index->is_clustered());

  scn_pos = index->get_sys_col_pos(DATA_SCN_ID);
  field =
      const_cast<byte *>(rec_get_nth_field(index, rec, offsets, scn_pos, &len));

  ut_ad(len == DATA_SCN_ID_LEN);
  ut_ad(field + DATA_SCN_ID_LEN ==
        rec_get_nth_field(index, rec, offsets, scn_pos + 1, &len));
  ut_ad(len == DATA_UNDO_PTR_LEN);

  return field;
}

/**
  Write the scn and undo_ptr of the physical record.
  @param[in]      ptr       scn pointer
  @param[in]      txn_desc  txn description
*/
void row_upd_rec_write_scn_and_uba(byte *ptr,
                                   const scn_t scn,
                                   const undo_ptr_t uba) {
  trx_write_scn(ptr, scn);
  trx_write_undo_ptr(ptr + DATA_SCN_ID_LEN, uba);
}

/**
  Write the scn and undo_ptr of the physical record.
  @param[in]      ptr       scn pointer
  @param[in]      scn       SCN
  @param[in]      undo_ptr  UBA
*/
void row_upd_rec_write_scn_and_undo_ptr(byte *ptr,
                                        const scn_t scn,
                                        const undo_ptr_t undo_ptr) {
  mach_write_to_8(ptr, scn);
  mach_write_to_7(ptr + DATA_SCN_ID_LEN, undo_ptr);
}


/**
  Modify the scn and undo_ptr of record. It will handle compress pages.
  @param[in]      rec       record
  @param[in]      page_zip
  @paramp[in]     index     cluster index
  @param[in]      offsets   rec_get_offsets(rec, idnex)
  @param[in]      txn_desc  txn description
*/
static void row_upd_rec_lizard_fields_low(rec_t *rec,
                                          page_zip_des_t *page_zip,
                                          const dict_index_t *index,
                                          const ulint *offsets,
                                          const scn_t scn,
                                          const undo_ptr_t uba) {
  byte *field;
  ut_ad(index->is_clustered());
  assert_undo_ptr_allocated(uba);

  if (page_zip) {
    ulint pos = index->get_sys_col_pos(DATA_SCN_ID);
    page_zip_write_scn_and_undo_ptr(page_zip, index, rec, offsets, pos, scn,
                                    uba);

  } else {
    /** Get pointer of scn_id in record */
    field = row_get_scn_ptr_in_rec(rec, index, offsets);
    row_upd_rec_write_scn_and_uba(field, scn, uba);
  }
}

/**
  Modify the scn and undo_ptr of record.
  @param[in]      rec       record
  @param[in]      page_zip
  @paramp[in]     index     cluster index
  @param[in]      offsets   rec_get_offsets(rec, idnex)
  @param[in]      txn       txn description
*/
void row_upd_rec_lizard_fields(rec_t *rec, page_zip_des_t *page_zip,
                               const dict_index_t *index,
                               const ulint *offsets,
                               const txn_desc_t *txn) {
  const txn_desc_t *txn_desc;
  ut_ad(index->is_clustered());
  ut_ad(!index->table->skip_alter_undo);
  ut_ad(!index->table->is_intrinsic());

  if (index->table->is_temporary()) {
    txn_desc = &TXN_DESC_TEMP;
  } else {
    txn_desc = txn;
    assert_undo_ptr_allocated(txn_desc->undo_ptr);
  }

  row_upd_rec_lizard_fields_low(rec, page_zip, index, offsets,
                                txn_desc->scn.first,
                                txn_desc->undo_ptr);
}

/**
  Updates the scn and undo_ptr field in a clustered index record when
  cleanout because of update.
  @param[in/out]  rec       record
  @param[in/out]  page_zip  compressed page, or NULL
  @param[in]      pos       SCN position in rec
  @param[in]      index     cluster index
  @param[in]      offsets   rec_get_offsets(rec, idnex)
  @param[in]      scn       SCN
  @param[in]      undo_ptr  UBA
*/
void row_upd_rec_lizard_fields_in_cleanout(rec_t *rec, page_zip_des_t *page_zip,
                                           const dict_index_t *index,
                                           const ulint *offsets,
                                           const txn_rec_t *txn_rec) {
  ut_ad(index->is_clustered());
  ut_ad(!index->table->skip_alter_undo);
  ut_ad(!index->table->is_temporary());

  lizard_ut_ad(!lizard_undo_ptr_is_active(txn_rec->undo_ptr));
  row_upd_rec_lizard_fields_low(rec, page_zip, index, offsets,
                                txn_rec->scn, txn_rec->undo_ptr);
}

/**
  Updates the scn and undo_ptr field in a clustered index record in
  database recovery.
  @param[in/out]  rec       record
  @param[in/out]  page_zip  compressed page, or NULL
  @param[in]      pos       SCN position in rec
  @param[in]      index     cluster index
  @param[in]      offsets   rec_get_offsets(rec, idnex)
  @param[in]      scn       SCN
  @param[in]      undo_ptr  UBA
*/
void row_upd_rec_lizard_fields_in_recovery(rec_t *rec,
                                           page_zip_des_t *page_zip,
                                           const dict_index_t *index,
                                           ulint pos,
                                           const ulint *offsets,
                                           const scn_t scn,
                                           const undo_ptr_t undo_ptr) {
  /** index->type (Log_Dummy) will not be set rightly if it's non-compact
  format, see function **mlog_parse_index** */
  ut_ad(!rec_offs_comp(offsets) || index->is_clustered());
  ut_ad(rec_offs_validate(rec, NULL, offsets));

  /** Lizard: This assertion is left, because we wonder if
  there will be a false case */
  /**
    Revision:
    Since 8029, because the redo of the instant ddl v2 version did not fully
    restore the state of the data dictionary during mlog_parse_index, so
    "index->get_sys_col_pos(DATA_SCN_ID)" may have an error.

    The example found so far is:
    modify or insert a new record after instant drop column.
  */
  // lizard_ut_ad(!(*rec_offs_base(offsets) & REC_OFFS_COMPACT) ||
  //              index->get_sys_col_pos(DATA_SCN_ID) == pos);

  if (page_zip) {
    page_zip_write_scn_and_undo_ptr(page_zip, index, rec, offsets,
                                    pos, scn, undo_ptr);
  } else {
    byte *field;
    ulint len;

    field =
        const_cast<byte *>(rec_get_nth_field(index, rec, offsets, pos, &len));
    ut_ad(len == DATA_SCN_ID_LEN);

    row_upd_rec_write_scn_and_undo_ptr(field, scn, undo_ptr);
  }
}

/**
  Validate the scn and undo_ptr fields in record.
  @param[in]      index     dict_index_t
  @param[in]      scn_ptr_in_rec   scn_id position in record
  @param[in]      scn_pos   scn_id no in system cols
  @param[in]      rec       record
  @param[in]      offsets   rec_get_offsets(rec, idnex)

  @retval true if verification passed, abort otherwise
*/
bool validate_lizard_fields_in_record(const dict_index_t *index,
                                      const byte *scn_ptr_in_rec, ulint scn_pos,
                                      const rec_t *rec, const ulint *offsets) {
  ulint len;

  ut_a(scn_ptr_in_rec ==
       const_cast<byte *>(rec_get_nth_field(index, rec, offsets, scn_pos, &len)));
  ut_a(len == DATA_SCN_ID_LEN);
  ut_a(scn_ptr_in_rec + DATA_SCN_ID_LEN ==
       rec_get_nth_field(index, rec, offsets, scn_pos + 1, &len));
  ut_a(len == DATA_UNDO_PTR_LEN);

  return true;
}

/*=============================================================================*/
/* lizard fields read/write from table record */
/*=============================================================================*/

/**
  Read the scn id from record

  @param[in]      rec         record
  @param[in]      index       dict_index_t, must be cluster index
  @param[in]      offsets     rec_get_offsets(rec, index)

  @retval         scn id
*/
scn_id_t row_get_rec_scn_id(const rec_t *rec, const dict_index_t *index,
                            const ulint *offsets) {
  ulint offset;
  ut_ad(index->is_clustered());
  assert_lizard_dict_index_check(index);

  offset = row_get_lizard_offset(index, DATA_SCN_ID, offsets);

  return trx_read_scn(rec + offset);
}

/**
  Read the undo ptr from record

  @param[in]      rec         record
  @param[in]      index       dict_index_t, must be cluster index
  @param[in]      offsets     rec_get_offsets(rec, index)

  @retval         scn id
*/
undo_ptr_t row_get_rec_undo_ptr(const rec_t *rec, const dict_index_t *index,
                                const ulint *offsets) {
  ulint offset;
  ut_ad(index->is_clustered());
  assert_lizard_dict_index_check(index);

  offset = row_get_lizard_offset(index, DATA_UNDO_PTR, offsets);
  return trx_read_undo_ptr(rec + offset);
}

/**
  Read the undo ptr state from record

  @param[in]      rec         record
  @param[in]      index       dict_index_t, must be cluster index
  @param[in]      offsets     rec_get_offsets(rec, index)

  @retval         scn id
*/
bool row_get_rec_undo_ptr_is_active(const rec_t *rec, const dict_index_t *index,
                                    const ulint *offsets) {
  undo_ptr_t undo_ptr = row_get_rec_undo_ptr(rec, index, offsets);

  return lizard_undo_ptr_is_active(undo_ptr);
}

/**
  Get the relative offset in record by offsets
  @param[in]      index
  @param[in]      type
  @param[in]      offsets
*/
ulint row_get_lizard_offset(const dict_index_t *index, ulint type,
                            const ulint *offsets) {
  ulint pos;
  ulint offset;
  ulint len;
  ut_ad(index->is_clustered());
  ut_ad(!index->table->is_intrinsic());

  pos = index->get_sys_col_pos(type);
  ut_ad(pos == index->n_uniq + type - 1);

  offset = rec_get_nth_field_offs(index, offsets, pos, &len);
  if (type == DATA_SCN_ID) {
    ut_ad(len == DATA_SCN_ID_LEN);
  } else if (type == DATA_UNDO_PTR) {
    ut_ad(len == DATA_UNDO_PTR_LEN);
  } else {
    ut_ad(0);
  }
  return offset;
}

/**
  Write the scn and undo ptr into the update vector
  @param[in]      trx         transaction context
  @param[in]      index       index object
  @param[in]      update      update vector
  @param[in]      field_nth   the nth from SCN id field
  @param[in]      txn_info    txn information
  @param[in]      heap        memory heap
*/
void trx_undo_update_rec_by_lizard_fields(const dict_index_t *index,
                                          upd_t *update, ulint field_nth,
                                          txn_info_t txn_info,
                                          mem_heap_t *heap) {
  byte *buf;
  upd_field_t *upd_field;
  ut_ad(update && heap);

  upd_field = upd_get_nth_field(update, field_nth);
  buf = static_cast<byte *>(mem_heap_alloc(heap, DATA_SCN_ID_LEN));
  trx_write_scn(buf, txn_info.scn);
  upd_field_set_field_no(upd_field, index->get_sys_col_pos(DATA_SCN_ID), index);
  dfield_set_data(&(upd_field->new_val), buf, DATA_SCN_ID_LEN);

  upd_field = upd_get_nth_field(update, field_nth + 1);
  buf = static_cast<byte *>(mem_heap_alloc(heap, DATA_UNDO_PTR_LEN));
  trx_write_undo_ptr(buf, txn_info.undo_ptr);
  upd_field_set_field_no(upd_field, index->get_sys_col_pos(DATA_UNDO_PTR),
                         index);
  dfield_set_data(&(upd_field->new_val), buf, DATA_UNDO_PTR_LEN);
}

/**
  Read the scn and undo_ptr from undo record
  @param[in]      ptr       undo record
  @param[out]     txn_info  SCN and UBA info

  @retval begin of the left undo data.
*/
byte *trx_undo_update_rec_get_lizard_cols(const byte *ptr,
                                          txn_info_t *txn_info) {
  txn_info->scn = mach_u64_read_next_compressed(&ptr);
  txn_info->undo_ptr = mach_u64_read_next_compressed(&ptr);

  return const_cast<byte *>(ptr);
}

/**
  Write redo log to the buffer about updates of scn and uba.
  @param[in]      index     clustered index of the record
  @param[in]      txn_rec   txn info of the record
  @param[in]      log_ptr   pointer to a buffer opened in mlog
  @param[in]      mtr       mtr

  @return new pointer to mlog
*/
byte* row_upd_write_lizard_vals_to_log(const dict_index_t *index,
                                       const txn_rec_t *txn_rec,
                                       byte *log_ptr,
                                       mtr_t *mtr MY_ATTRIBUTE((unused))) {
  ut_ad(index->is_clustered());
  ut_ad(mtr);
  assert_undo_ptr_allocated(txn_rec->undo_ptr);

  log_ptr +=
      mach_write_compressed(log_ptr, index->get_sys_col_pos(DATA_SCN_ID));

  log_ptr += mach_u64_write_compressed(log_ptr, txn_rec->scn);

  trx_write_undo_ptr(log_ptr, txn_rec->undo_ptr);
  log_ptr += DATA_UNDO_PTR_LEN;

  return log_ptr;
}

/**
  Get the real SCN of a record by UBA, and write back to records in physical page,
  when we make a btr update / delete.
  @param[in]      trx_id    trx_id of the transactions
                            who updates / deletes the record
  @param[in]      rec       record
  @param[in]      offsets   rec_get_offsets(rec)
  @param[in/out]  block     buffer block of the record
  @param[in/out]  mtr       mini-transaction
*/
void row_lizard_cleanout_when_modify_rec(const trx_id_t trx_id, rec_t *rec,
                                         const dict_index_t *index,
                                         const ulint *offsets,
                                         const buf_block_t *block,
                                         mtr_t *mtr) {
  trx_id_t rec_id;
  bool cleanout;
  txn_rec_t rec_txn;

  ut_ad(trx_id > 0);

  rec_id = row_get_rec_trx_id(rec, index, offsets);

  if (trx_id == rec_id) {
    /* update a exist row which has been modified by
    the current active transaction */
    return;
  }

  /** scn must be consistent with the undo_ptr */
  row_lizard_valid(rec, index, offsets);
  ut_ad(index->is_clustered());
  ut_ad(!index->table->is_intrinsic());

  rec_txn.scn = row_get_rec_scn_id(rec, index, offsets);
  rec_txn.undo_ptr = row_get_rec_undo_ptr(rec, index, offsets);
  rec_txn.trx_id = rec_id;

  /** lookup the scn by UBA address */
  cleanout = txn_undo_hdr_lookup(&rec_txn);

  if (cleanout) {
    ut_ad(mtr_memo_contains_flagged(mtr, block, MTR_MEMO_PAGE_X_FIX));
    lizard::row_upd_rec_lizard_fields_in_cleanout(
        const_cast<rec_t*>(rec),
        const_cast<page_zip_des_t *>(buf_block_get_page_zip(block)),
        index, offsets, &rec_txn);

    /** Write redo log */
    btr_cur_upd_lizard_fields_clust_rec_log(rec, index, &rec_txn, mtr);
  }
}

/**
  Parses the log data of lizard field values.
  @param[in]      ptr       buffer
  @param[in]      end_ptr   buffer end
  @param[out]     pos       SCN position in record
  @param[out]     scn       scn
  @param[out]     undo_ptr  uba

  @return log data end or NULL
*/
byte *row_upd_parse_lizard_vals(const byte *ptr, const byte *end_ptr,
                                ulint *pos, scn_t *scn,
                                undo_ptr_t *undo_ptr) {
  *pos = mach_parse_compressed(&ptr, end_ptr);

  if (ptr == nullptr) return nullptr;

  if (end_ptr < ptr + DATA_UNDO_PTR_LEN) return nullptr;

  *scn = mach_u64_parse_compressed(&ptr, end_ptr);

  *undo_ptr = trx_read_undo_ptr(ptr);
  ptr += DATA_UNDO_PTR_LEN;

  return const_cast<byte *>(ptr);
}


#if defined UNIV_DEBUG || defined LIZARD_DEBUG
/*=============================================================================*/
/* lizard field debug */
/*=============================================================================*/

/**
  Debug the scn id in record is initial state
  @param[in]      rec       record
  @param[in]      index     cluster index
  @parma[in]      offsets   rec_get_offsets(rec, index)

  @retval         true      Success
*/
bool row_scn_initial(const rec_t *rec, const dict_index_t *index,
                     const ulint *offsets) {
  scn_id_t scn = row_get_rec_scn_id(rec, index, offsets);
  ut_a(scn == SCN_NULL);
  return true;
}

/**
  Debug the undo_ptr state in record is active state
  @param[in]      rec       record
  @param[in]      index     cluster index
  @parma[in]      offsets   rec_get_offsets(rec, index)

  @retval         true      Success
*/
bool row_undo_ptr_is_active(const rec_t *rec,
                            const dict_index_t *index,
                            const ulint *offsets) {
  bool is_active = row_get_rec_undo_ptr_is_active(rec, index, offsets);
  ut_a(is_active);
  return true;
}

/**
  Debug the undo_ptr and scn in record is matched.
  @param[in]      rec       record
  @param[in]      index     cluster index
  @parma[in]      offsets   rec_get_offsets(rec, index)

  @retval         true      Success
*/
bool row_lizard_valid(const rec_t *rec, const dict_index_t *index,
                      const ulint *offsets) {
  scn_id_t scn;
  undo_ptr_t undo_ptr;
  bool is_active;
  undo_addr_t undo_addr;
  ulint comp;

  /** If we are in recovery, we don't make a validation, because purge
  sys might have not been started */
  if (recv_recovery_is_on()) return true;

  comp = *rec_offs_base(offsets) & REC_OFFS_COMPACT;

  /** Temporary table can not be seen in other transactions */
  if (!index->is_clustered() || index->table->is_intrinsic()) return true;
  /**
    Skip the REC_STATUS_NODE_PTR, REC_STATUS_INFIMUM, REC_STATUS_SUPREMUM
    Skip the non-compact record
  */
  if (comp && rec_get_status(rec) == REC_STATUS_ORDINARY) {
    scn = row_get_rec_scn_id(rec, index, offsets);
    undo_ptr = row_get_rec_undo_ptr(rec, index, offsets);

    is_active = row_get_rec_undo_ptr_is_active(rec, index, offsets);

    undo_decode_undo_ptr(undo_ptr, &undo_addr);

    /** UBA is valid */
    lizard_undo_addr_validation(&undo_addr, index);

    /** Scn and trx state are matched */
    ut_a(is_active == (scn == SCN_NULL));
  }
  return true;
}

/**
  Debug the undo_ptr and scn in record is matched.
  @param[in]      rec       record
  @param[in]      index     cluster index
  @parma[in]      offsets   rec_get_offsets(rec, index)

  @retval         true      Success
*/
bool row_lizard_has_cleanout(const rec_t *rec, const dict_index_t *index,
                             const ulint *offsets) {
  scn_id_t scn;
  undo_ptr_t undo_ptr;
  bool is_active;
  undo_addr_t undo_addr;

  if (!index->is_clustered() || index->table->is_intrinsic()) return true;

  /**
    Skip the REC_STATUS_NODE_PTR, REC_STATUS_INFIMUM, REC_STATUS_SUPREMUM
  */
  if (rec_get_status(rec) == REC_STATUS_ORDINARY) {
    scn = row_get_rec_scn_id(rec, index, offsets);
    undo_ptr = row_get_rec_undo_ptr(rec, index, offsets);
    is_active = row_get_rec_undo_ptr_is_active(rec, index, offsets);

    undo_decode_undo_ptr(undo_ptr, &undo_addr);

    /** UBA is valid */
    lizard_undo_addr_validation(&undo_addr, index);
    /** commit */
    ut_a(!is_active);
    /** valid scn */
    ut_a(scn != SCN_NULL);
  }
  return true;
}

#endif /* UNIV_DEBUG || LIZARD_DEBUG */

} /* namespace lizard */
