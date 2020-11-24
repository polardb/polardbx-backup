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

/** @file include/lizard0undo.h
  Lizard transaction undo and purge types.

 Created 2020-04-02 by Jianwei.zhao
 *******************************************************/

#ifndef lizard0undo_h
#define lizard0undo_h

#include "trx0types.h"
#include "trx0undo.h"

#include "lizard0dbg.h"
#include "lizard0scn.h"
#include "lizard0undo0types.h"

#include <atomic>
#include <queue>
#include <set>
#include <vector>

struct trx_rseg_t;
struct trx_undo_t;

/**
  Lizard transaction system undo format:

  At the end of undo log header history node:

  8 bytes     SCN number
  8 bytes     UTC time

  Those two option will be included into all INSERT/UPDATE/TXN undo
  log header.


  Start from undo log old header, txn_undo will be different with trx_undo:

  1) txn undo : flag + reserved space

  2) trx undo : XA + GTID

  As the optional info, those will be controlled by TRX_UNDO_FLAGS.

     0x01 TRX_UNDO_FLAG_XID
     0x02 TRX_UNDO_FLAG_GTID
     0x80 TRX_UNDO_FLAG_TXN
*/

/** Those will only exist in txn undo log header*/
/*-------------------------------------------------------------*/
/* Random magic number */
#define TXN_UNDO_LOG_EXT_MAGIC (TRX_UNDO_LOG_XA_HDR_SIZE)
/* Flag how to use reserved space */
#define TXN_UNDO_LOG_EXT_FLAG (TXN_UNDO_LOG_EXT_MAGIC + 4)
/* Unused space */
#define TXN_UNDO_LOG_EXT_RESERVED (TXN_UNDO_LOG_EXT_FLAG + 1)
/* Unused space size */
#define TXN_UNDO_LOG_EXT_RESERVED_LEN 60
/* txn undo log header size */
#define TXN_UNDO_LOG_EXT_HDR_SIZE \
  (TXN_UNDO_LOG_EXT_RESERVED + TXN_UNDO_LOG_EXT_RESERVED_LEN)
/*-------------------------------------------------------------*/
static_assert(TXN_UNDO_LOG_EXT_HDR_SIZE == TRX_UNDO_LOG_HDR_SIZE,
              "txn and trx undo log header size must be equal!");

/** Pls reuse the reserved space */
static_assert(TXN_UNDO_LOG_EXT_HDR_SIZE == 267,
              "txn undo log header size cann't change!");
/** txn magic number */
#define TXN_MAGIC_N 91118498

struct txn_undo_ext_t {
  ulint magic_n;
  ulint flag;
};

namespace lizard {

/** Initial value of undo ptr  */
constexpr undo_ptr_t UNDO_PTR_NULL = std::numeric_limits<undo_ptr_t>::min();

/** Fake UBA for internal table */
constexpr undo_ptr_t UNDO_PTR_FAKE = (undo_ptr_t)1 << 55;

/* Lizard transaction undo header operation */
/*-----------------------------------------------------------------------------*/
#if defined UNIV_DEBUG || defined LIZARD_DEBUG
/** Check the UBA validation */
bool undo_addr_validation(const undo_addr_t *undo_addr,
                          const dict_index_t *index);
/**
  Validate the page is undo page

  @param[in]      page      undo page
  @return         true      it's undo page
*/
bool trx_undo_page_validation(const page_t *page);

/** Confirm the consistent of scn, undo type, undo state. */
bool undo_scn_validation(const trx_undo_t *undo);

#endif  // UNIV_DEBUG || LIZARD_DEBUG

/**
  Initial the NULL value on SCN and UTC when create undo log header.
  include all kinds of undo log header type.
  The redo log logic is included in "MLOG_UNDO_HDR_CREATE";

  @param[in]      log_hdr       undo log header
  @param[in]      mtr           current mtr context
*/

extern void trx_undo_hdr_init_scn(trx_ulogf_t *log_hdr, mtr_t *mtr);

/**
  Write the scn and utc when commit.
  Include the redo log

  @param[in]      log_hdr       undo log header
  @param[in]      commit_scn    commit scn number
  @param[in]      mtr           current mtr context
*/
extern void trx_undo_hdr_write_scn(trx_ulogf_t *log_hdr,
                                   std::pair<scn_t, utc_t> &cmmt_scn,
                                   mtr_t *mtr);

/**
  Read the scn and utc.

  @param[in]      log_hdr       undo log header
  @param[in]      mtr           current mtr context
*/
extern std::pair<scn_t, utc_t> trx_undo_hdr_read_scn(trx_ulogf_t *log_hdr,
                                                     mtr_t *mtr);

/**
  Add the space for the txn especially.

  @param[in]      undo_page     undo log header page
  @param[in]      log_hdr       undo log hdr
  @param[in]      mtr
*/
extern void trx_undo_hdr_add_space_for_txn(page_t *undo_page,
                                           trx_ulogf_t *log_hdr, mtr_t *mtr);
/**
  Init the txn extension information.

  @param[in]      undo
  @param[in]      undo_page     undo log header page
  @param[in]      log_hdr       undo log hdr
  @param[in]      mtr
*/
void trx_undo_hdr_init_for_txn(trx_undo_t *undo, page_t *undo_page,
                               trx_ulogf_t *log_hdr, mtr_t *mtr);
/**
  Read the txn undo log header extension information.

  @param[in]      undo page
  @param[in]      undo log hdr
  @param[in]      mtr
  @param[out]     txn_undo_ext
*/
void trx_undo_hdr_read_txn(const page_t *undo_page,
                           const trx_ulogf_t *undo_header, mtr_t *mtr,
                           txn_undo_ext_t *txn_undo_ext);

/*-----------------------------------------------------------------------------*/

/* Lizard transaction rollback segment operation */
/*-----------------------------------------------------------------------------*/
/**
  Whether the txn rollback segment has been assigned
  @param[in]      trx
*/
bool trx_is_txn_rseg_assigned(trx_t *trx);
/**
  Whether the txn undo log has modified.
*/
bool trx_is_txn_rseg_updated(const trx_t *trx);
/**
  Always assign transaction rollback segment for trx
  @param[in]      trx
*/
void trx_assign_txn_rseg(trx_t *trx);
/**
  Always assign a txn undo log for transaction.

  @param[in]        trx         current transaction

  @return           DB_SUCCESS  Success
*/
dberr_t trx_always_assign_txn_undo(trx_t *trx);
/*-----------------------------------------------------------------------------*/
/**
  Init the txn description as NULL initial value.
  @param[in]      trx       current transaction
*/
void trx_init_txn_desc(trx_t *trx);
/**
  Assign a new commit scn for the transaction when commit

  @param[in]      trx       current transaction
  @param[in/out]  scn_ptr   Commit scn which was generated only once
  @param[in]      undo      txn undo log
  @param[in]      undo page txn undo log header page
  @param[in]      offset    txn undo log header offset
  @param[in]      mtr       mini transaction

  @retval         scn       commit scn struture
*/
commit_scn_t trx_commit_scn(trx_t *trx, commit_scn_t *scn_ptr, trx_undo_t *undo,
                            page_t *undo_hdr_page, ulint hdr_offset,
                            mtr_t *mtr);
/**
  Cleanup txn undo log segment when commit,

  It will :
    1) Add the UBA header into rseg->history
    2) Reinit the rseg->slot as FIL_NULL
    3) Destroy or reuse the undo mem object

  @param[in]      trx         trx owning the txn undo log
  @param[in/out]  undo_ptr    txn undo log structure
  @param[in]      undo_page   txn undo log header page, x-latched
  @param[in]      update_rseg_history_len
                              if true: update rseg history
                              len else skip updating it.
  @param[in]      n_added_logs
                              number of logs added
  @param[in]      mtr
*/
void trx_txn_undo_cleanup(trx_t *trx, txn_undo_ptr_t *undo_ptr,
                          page_t *undo_page, bool update_rseg_history_len,
                          ulint n_added_logs, mtr_t *mtr);
/**
   Resurrect txn undo log segment,
   Maybe the trx didn't have m_redo update/insert undo log.
*/
void trx_resurrect_txn(trx_t *trx, trx_undo_t *undo, trx_rseg_t *rseg);

/** Prepares a transaction for given rollback segment. */
lsn_t txn_prepare_low(
    trx_t *trx,               /*!< in/out: transaction */
    txn_undo_ptr_t *undo_ptr, /*!< in/out: pointer to rollback
                              segment scheduled for prepare. */
    mtr_t *mtr);

/**
  Put the txn undo log segment into free list after purge all.

  @param[in]        rseg        rollback segment
  @param[in]        hdr_addr    txn log hdr address
*/
void txn_purge_segment_to_free_list(trx_rseg_t *rseg, fil_addr_t hdr_addr);

}  // namespace lizard


/** Delcare the functions which were defined in other cc files.*/
/*=============================================================================*/
extern trx_undo_t *trx_undo_reuse_cached(trx_t *trx, trx_rseg_t *rseg,
                                         ulint type, trx_id_t trx_id,
                                         const XID *xid, bool is_gtid,
                                         mtr_t *mtr);

extern dberr_t trx_undo_create(trx_t *trx, trx_rseg_t *rseg, ulint type,
                               trx_id_t trx_id, const XID *xid, bool is_gtid,
                               trx_undo_t **undo, mtr_t *mtr);

void trx_resurrect_update_in_prepared_state(trx_t *trx, const trx_undo_t *undo);

void trx_purge_remove_log_hdr(trx_rsegf_t *rseg_hdr, trx_ulogf_t *log_hdr,
                              mtr_t *mtr);

trx_undo_t *trx_undo_mem_create(trx_rseg_t *rseg, ulint id, ulint type,
                                trx_id_t trx_id, const XID *xid,
                                page_no_t page_no, ulint offset);

void trx_undo_page_init(page_t *undo_page, /*!< in: undo log segment page */
                        ulint type,        /*!< in: undo log segment type */
                        mtr_t *mtr);       /*!< in: mtr */

ulint trx_undo_header_create(page_t *undo_page, /*!< in/out: undo log segment
                                                header page, x-latched; it is
                                                assumed that there is
                                                TRX_UNDO_LOG_HDR_SIZE bytes
                                                free space on it */
                             trx_id_t trx_id,   /*!< in: transaction id */
                             mtr_t *mtr);       /*!< in: mtr */
/** Remove an rseg header from the history list.
@param[in,out]	rseg_hdr	rollback segment header
@param[in]	log_hdr		undo log segment header
@param[in,out]	mtr		mini transaction. */
void trx_purge_remove_log_hdr(trx_rsegf_t *rseg_hdr, trx_ulogf_t *log_hdr,
                              mtr_t *mtr);
/** Adds space for the XA XID after an undo log old-style header.
@param[in,out]	undo_page	undo log segment header page
@param[in,out]	log_hdr		undo log header
@param[in,out]	mtr		mini transaction
@param[in]	add_gtid	add space for GTID */
void trx_undo_header_add_space_for_xid(page_t *undo_page, trx_ulogf_t *log_hdr,
                                       mtr_t *mtr, bool add_gtid);
/*=============================================================================*/

#define TXN_DESC_NULL \
  { lizard::UNDO_PTR_NULL, COMMIT_SCN_NULL }

#if defined UNIV_DEBUG || defined LIZARD_DEBUG

/* Assert the txn_desc is initial */
#define assert_txn_desc_initial(trx)                                          \
  do {                                                                        \
    ut_a((trx)->txn_desc.undo_ptr == lizard::UNDO_PTR_NULL &&                 \
         lizard::commit_scn_state((trx)->txn_desc.scn) == SCN_STATE_INITIAL); \
  } while (0)

#define assert_undo_ptr_initial(undo_ptr)         \
  do {                                            \
    ut_a((*(undo_ptr)) == lizard::UNDO_PTR_NULL); \
  } while (0)

#define assert_undo_ptr_allocated(undo_ptr)       \
  do {                                            \
    ut_a((*(undo_ptr)) != lizard::UNDO_PTR_NULL); \
  } while (0)

#define assert_trx_undo_ptr_initial(trx) \
  assert_undo_ptr_initial((&(trx)->txn_desc.undo_ptr))

#define assert_trx_undo_ptr_allocated(trx) \
  assert_undo_ptr_allocated(&((trx)->txn_desc.undo_ptr))

#define lizard_trx_undo_page_validation(page)     \
  do {                                            \
    ut_a(lizard::trx_undo_page_validation(page)); \
  } while (0)

#define lizard_trx_undo_hdr_txn_validation(undo_page, undo_hdr, mtr)        \
  do {                                                                      \
    txn_undo_ext_t txn_undo_ext;                                            \
    lizard::trx_undo_hdr_read_txn(undo_page, undo_hdr, mtr, &txn_undo_ext); \
    ut_a(txn_undo_ext.magic_n == TXN_MAGIC_N && txn_undo_ext.flag == 0);    \
  } while (0)

#define lizard_undo_addr_validation(undo_addr, index)     \
  do {                                                    \
    ut_a(lizard::undo_addr_validation(undo_addr, index)); \
  } while (0)

#define lizard_undo_scn_validation(undo)     \
  do {                                       \
    ut_a(lizard::undo_scn_validation(undo)); \
  } while (0)

#define assert_trx_in_recovery(trx)                                            \
  do {                                                                         \
    if ((trx)->rsegs.m_txn.rseg != NULL && (trx)->rsegs.m_redo.rseg == NULL) { \
      ut_a((trx)->is_recovered);                                               \
    }                                                                          \
  } while (0)

#define lizard_txn_undo_free_list_validate(rseg_hdr, undo_page, mtr)     \
  do {                                                                   \
    ut_a(lizard::txn_undo_free_list_validate(rseg_hdr, undo_page, mtr)); \
  } while (0)
#else

#define lizard_trx_undo_page_validation(page)
#define assert_txn_desc_initial(trx)
#define assert_undo_ptr_initial(undo_ptr)
#define assert_undo_ptr_allocated(undo_ptr)
#define assert_trx_undo_ptr_initial(trx)
#define assert_trx_undo_ptr_allocated(trx)

#define lizard_trx_undo_hdr_txn_validation(undo_page, undo_hdr, mtr)
#define lizard_undo_addr_validation(undo_addr, index)
#define lizard_undo_scn_validation(undo)
#define assert_trx_in_recovery(trx)
#define lizard_txn_undo_free_list_validate(rseg_hdr, undo_page, mtr)

#endif

#endif  // lizard0undo_h
