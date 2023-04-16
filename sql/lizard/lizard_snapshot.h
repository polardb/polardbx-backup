/*****************************************************************************

Copyright (c) 2013, 2023, Alibaba and/or its affiliates. All Rights Reserved.

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

#ifndef LIZARD_LIZARD_SNAPSHOT_INCLUDED
#define LIZARD_LIZARD_SNAPSHOT_INCLUDED

#include "lex_string.h"
#include "lizard_iface.h"
#include "my_dbug.h"

class THD;
class Item;
struct Parse_context;
class Table_ref;
struct TABLE;
struct LEX;

namespace lizard {

/** Statement snapshot type at mysql server layer*/
typedef enum {
  AS_OF_NONE,
  AS_OF_TIMESTAMP,
  AS_OF_SCN,
  AS_OF_GCN
} Snapshot_type;

/*------------------------------------------------------------------------------*/
/* Snapshot Hint */
/*------------------------------------------------------------------------------*/

/** Snapshot hint interface, it's worked on TABLE_LIST object through SQL syntax;
    like:

    1) Snapshot scn hint

      SELECT * FROM tbl AS OF SCN [expr]

    2) Snapshot timestamp hint

      SELECT * FROM tbl AS OF TIMESTAMP [expr]

    3) Snapshot gcn hint

      SELECT * FROM tbl AS OF GCN [expr]
 */
class Snapshot_hint {
 public:
  explicit Snapshot_hint(Item *item) : m_item(item) {}

  /** Item type. */
  virtual Snapshot_type type() const = 0;

  /**
    Fix fields
    @retval	true	Failure
    @retval	false	Success
   */
  virtual bool fix_fields(THD *thd) = 0;

  /**
    Itemize the snapshot item and hook onto TABLE_LIST.

    My_error if failure.

    @retval	true	Failure
    @retval	false	Success

  */
  bool itemize(Parse_context *pc, Table_ref *owner);

  /**
    Evoke table snapshot vision.
    My_error if failure.

    @retval HA_ERR_SNAPSHOT_OUT_OF_RANGE, HA_ERR_AS_OF_INTERNAL on error.
    @retval 0 Success
   */
  int evoke_vision(TABLE *table, my_scn_t scn, THD *thd);

  /** Calculate number from hint item. */
  virtual bool val_int(uint64_t *value) = 0;

 protected:
  Item *m_item;
};

/** Parse node special */
struct Table_snapshot_hint_and_alias {
  LEX_CSTRING alias;
  Snapshot_hint *snapshot_hint;
};

/** As of scn hint */
class Snapshot_scn_hint : public Snapshot_hint {
 public:
  Snapshot_scn_hint(Item *item) : Snapshot_hint(item) {}

  virtual Snapshot_type type() const override { return AS_OF_SCN; }

  /**
    Fix fields

    My_error if failure.

    @retval	true	Failure
    @retval	false	Success
   */
  virtual bool fix_fields(THD *thd) override;

  /** Calculate scn from hint item. */
  virtual bool val_int(uint64_t *value) override;
};

/** As of timestamp hint */
class Snapshot_time_hint : public Snapshot_hint {
 public:
  Snapshot_time_hint(Item *item) : Snapshot_hint(item) {}

  virtual Snapshot_type type() const override { return AS_OF_TIMESTAMP; }

  /**
    Fix fields

    My_error if failure.

    @retval	true	Failure
    @retval	false	Success
   */
  virtual bool fix_fields(THD *thd) override;

  /** Calculate second from hint item. */
  virtual bool val_int(uint64_t *value) override;
};

/** As of gcn hint */
class Snapshot_gcn_hint : public Snapshot_hint {
 public:
  explicit Snapshot_gcn_hint(Item *item) : Snapshot_hint(item) {}

  virtual Snapshot_type type() const override { return AS_OF_GCN; }
  /**
    Fix fields

    My_error if failure.

    @retval	true	Failure
    @retval	false	Success
   */
  virtual bool fix_fields(THD *thd) override;

  /** Calculate gcn from hint item. */
  virtual bool val_int(uint64_t *value) override;
};

/*------------------------------------------------------------------------------*/
/* Snapshot Vision */
/*------------------------------------------------------------------------------*/
/** Snapshot Vision interface,

    It's the readview generated from mysql server layer.
 */
class Snapshot_vision {
 public:
  virtual ~Snapshot_vision() {}

  /*------------------------------------------------------------------------------*/
  /* Virtual function */
  /*------------------------------------------------------------------------------*/
  virtual Snapshot_type type() const = 0;

  /**
    Return static_cast number from vision.
  */
  virtual uint64_t val_int() = 0;

  /**
    Do something after myself is activated.
  */
  virtual void after_activate() = 0;

  /** Store number into vision. */
  virtual void store_int(uint64_t value) = 0;
};

/**
  Time vision, it's transformed by snapshot time hint,
  but it's not used by innodb until exchanged into scn vision.
*/
class Snapshot_time_vision : public Snapshot_vision {
 public:
  Snapshot_time_vision() : m_second(0) {}
  /*------------------------------------------------------------------------------*/
  /* Virtual function */
  /*------------------------------------------------------------------------------*/
  virtual Snapshot_type type() const override { return AS_OF_TIMESTAMP; }

  virtual void store_int(uint64_t value) override { m_second = value; }

  /** Do nothing since of never activated. */
  virtual void after_activate() override{
      // TODO:
      // assert(0);
  }
  virtual uint64_t val_int() override { return m_second; }

 private:
  uint64_t m_second;
};

/**
  SCN vision, it's transformed by snapshot scn hint,
*/
class Snapshot_scn_vision : public Snapshot_vision {
 public:
  Snapshot_scn_vision() : m_scn(MYSQL_SCN_NULL) {}

  /*------------------------------------------------------------------------------*/
  /* Virtual function */
  /*------------------------------------------------------------------------------*/
  virtual Snapshot_type type() const override { return AS_OF_SCN; }

  virtual void store_int(uint64_t value) override {
    m_scn = static_cast<my_scn_t>(value);
  }
  /** Do nothing, can be used directly by innodb. */
  virtual void after_activate() override {}

  virtual uint64_t val_int() override { return static_cast<uint64_t>(m_scn); }

 private:
  my_scn_t m_scn;
};

/**
  GCN vision, it's transformed by snapshot gcn hint,
*/
class Snapshot_gcn_vision : public Snapshot_vision {
 public:
  Snapshot_gcn_vision()
      : m_gcn(MYSQL_GCN_NULL), m_current_scn(MYSQL_SCN_NULL) {}

  /*------------------------------------------------------------------------------*/
  /* Virtual function */
  /*------------------------------------------------------------------------------*/
  virtual Snapshot_type type() const override { return AS_OF_GCN; }

  virtual void store_int(uint64_t value) override {
    m_gcn = static_cast<my_gcn_t>(value);
  }

  /** Do pushup GCS gcn if come from outer. */
  virtual void after_activate() override;

  virtual uint64_t val_int() override { return static_cast<uint64_t>(m_gcn); }

  void store_current_scn(my_scn_t scn) { m_current_scn = scn; }

 private:
  my_gcn_t m_gcn;

  /** Current scn must be acquire from innodb whatever vision. */
  my_scn_t m_current_scn;
};

/**
  Invalid vision from asof_none enum type.
 */
class Snapshot_noop_vision : public Snapshot_vision {
 public:
  Snapshot_noop_vision() {}
  /*------------------------------------------------------------------------------*/
  /* Virtual function */
  /*------------------------------------------------------------------------------*/
  virtual Snapshot_type type() const override { return AS_OF_NONE; }

  virtual void store_int(uint64_t) override { assert(0); }

  virtual void after_activate() override { assert(0); }

  virtual uint64_t val_int() override { return MYSQL_SCN_NULL; }
};

/** Table snapshot worked on TABLE object.
    Only can be used by innodb after activated by snapshot hint
 */
class Table_snapshot {
 public:
  Table_snapshot()
      : m_noop_vision(),
        m_time_vision(),
        m_scn_vision(),
        m_gcn_vision(),
        m_vision(&m_noop_vision) {}

  /** Return predefined vision */
  Snapshot_vision *get(Snapshot_type type) {
    switch (type) {
      case AS_OF_NONE:
        return &m_noop_vision;
      case AS_OF_SCN:
        return &m_scn_vision;
      case AS_OF_TIMESTAMP:
        return &m_time_vision;
      case AS_OF_GCN:
        return &m_gcn_vision;
      default:
        assert(0);
        return &m_noop_vision;
    }
  }

  /** Return current vision. */
  Snapshot_vision *vision() { return m_vision; }

  /** Activate a vision that can be used by innodb later.
  return true if error. */
  int activate(Snapshot_vision *vision, THD *thd) {
    int error;
    assert(vision == get(vision->type()));

    error = do_exchange(&vision, thd);

    if (!error) {
      m_vision = vision;

      vision->after_activate();
    }

    return error;
  }

  bool is_activated() { return m_vision->type() != AS_OF_NONE; }

  void release_vision() { m_vision = &m_noop_vision; }

  /** Whether it's a real vision. */
  bool is_vision() {
    return m_vision->type() == AS_OF_SCN || m_vision->type() == AS_OF_GCN;
  }

 private:
  int exchange_timestamp_vision_to_scn_vision(Snapshot_vision **vision,
                                              THD *thd);

  /**
    Change Snapshot_time_vision to Snapshot_scn_vision.

    @param[in/out]   vision
    @param[in]       thd       THD

    @retval HA_ERR_SNAPSHOT_OUT_OF_RANGE, HA_ERR_AS_OF_INTERNAL on error.
    @retval 0 Success
  */
  int do_exchange(Snapshot_vision **vision, THD *thd) {
    if ((*vision)->type() != AS_OF_TIMESTAMP) {
      return 0;
    }

    return exchange_timestamp_vision_to_scn_vision(vision, thd);
  }

 private:
  Snapshot_noop_vision m_noop_vision;
  Snapshot_time_vision m_time_vision;
  Snapshot_scn_vision m_scn_vision;
  Snapshot_gcn_vision m_gcn_vision;

  Snapshot_vision *m_vision;
};

extern void init_table_snapshot(TABLE *table, THD *thd);

extern void simulate_snapshot_clause(THD *thd, Table_ref *all_tables);

extern bool evaluate_snapshot(THD *thd, const LEX *lex);

}  // namespace lizard

#endif
