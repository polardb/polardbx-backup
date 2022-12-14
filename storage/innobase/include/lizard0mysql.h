
#ifndef lizard0mysql_h
#define lizard0mysql_h

#include "lizard0scn.h"
#include "lizard0read0types.h"

struct row_prebuilt_t;

namespace lizard {

/** Whether to enable use as of query (true by default) */
extern bool srv_scn_valid_enabled;

/** The max tolerable lease time of a snapshot */
extern ulint srv_scn_valid_volumn;

class Vision;

/**
  as of query context in row_prebuilt_t
*/
struct scn_query_context_t {
  /** If it has been set, only set once */
  bool m_is_set;

  /** Determine if it is a as of query */
  bool m_is_scn_query;

  /** SCN_NULL if it's not a as-of query */
  scn_t m_scn;

  /** TODO: ban constructor <16-10-20, zanye.zjy> */
  explicit scn_query_context_t()
    : m_is_set(false),
      m_is_scn_query(false),
      m_scn(SCN_NULL)
  {}

  ~scn_query_context_t() {
    reset();
  }

  bool is_set() {
    return m_is_set;
  }

  bool is_scn_query() const {
    return m_is_scn_query;
  }

  void set(scn_t scn) {
    if (scn != SCN_NULL) {
      m_is_scn_query = true;
      m_scn = scn;
    }
    /** Only set once */
    m_is_set = true;
  }

  void reset() {
    m_is_set = false;
    m_is_scn_query = false;
    m_scn = SCN_NULL;
  }
};

/**
  Try to convert flashback query in mysql to as-of query in innodb.

  @param[in/out]    prebuilt    row_prebuilt_t::m_scn_query could be modified

  @return           dberr_t     DB_SUCCESS if convert successfully or it's not
                                a flashback query.
                                ERROR: DB_AS_OF_INTERNAL, DB_SNAPSHOT_OUT_OF_RANGE,
                                DB_AS_OF_TABLE_DEF_CHANGED
*/
dberr_t convert_fbq_ctx_to_innobase(row_prebuilt_t *prebuilt);

/**
  Reset row_prebuilt_t::m_scn_query, query block level.

  @param[in/out]    prebuilt    row_prebuilt_t

  @return           dberr_t     DB_SUCCESS, or DB_SNAPSHOT_OUT_OF_RANGE.
*/
dberr_t reset_prebuilt_flashback_query_ctx(row_prebuilt_t *prebuilt);

}

#endif
