/* Copyright (c) 2000, 2021, Oracle and/or its affiliates.

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

/* classes to use when handling where clause */

#ifndef SQL_RANGE_OPTIMIZER_RANGE_OPTIMIZER_H_
#define SQL_RANGE_OPTIMIZER_RANGE_OPTIMIZER_H_

#include <assert.h>
#include <sys/types.h>
#include <algorithm>

#include "my_base.h"
#include "my_inttypes.h"
#include "my_table_map.h"
#include "prealloced_array.h"  // Prealloced_array
#include "sql/field.h"         // Field
#include "sql/handler.h"
#include "sql/item_func.h"
#include "sql/key_spec.h"
#include "sql/malloc_allocator.h"  // IWYU pragma: keep
#include "sql/sql_bitmap.h"
#include "sql/sql_const.h"
#include "sql_string.h"

class Item;
class Opt_trace_context;
class Query_block;
class THD;
struct MY_BITMAP;
struct TABLE;
class TABLE_READ_PLAN;

struct KEY_PART {
  uint16 key, part;
  /* See KEY_PART_INFO for meaning of the next two: */
  uint16 store_length, length;
  uint8 null_bit;
  /*
    Keypart flags (0 when this structure is used by partition pruning code
    for fake partitioning index description)
  */
  uint16 flag;
  Field *field;
  Field::imagetype image_type;
};

class QUICK_RANGE {
 public:
  uchar *min_key, *max_key;
  uint16 min_length, max_length;

  /// Stores bitwise-or'ed bits defined in enum key_range_flags.
  uint16 flag;

  /**
    Stores one of the HA_READ_MBR_XXX items in enum ha_rkey_function, only
    effective when flag has a GEOM_FLAG bit.
  */
  enum ha_rkey_function rkey_func_flag;
  key_part_map min_keypart_map,  // bitmap of used keyparts in min_key
      max_keypart_map;           // bitmap of used keyparts in max_key

  QUICK_RANGE(); /* Full range */
  QUICK_RANGE(const uchar *min_key_arg, uint min_length_arg,
              key_part_map min_keypart_map_arg, const uchar *max_key_arg,
              uint max_length_arg, key_part_map max_keypart_map_arg,
              uint flag_arg, enum ha_rkey_function rkey_func);

  /**
     Initalizes a key_range object for communication with storage engine.

     This function facilitates communication with the Storage Engine API by
     translating the minimum endpoint of the interval represented by this
     QUICK_RANGE into an index range endpoint specifier for the engine.

     @param kr Pointer to an uninitialized key_range C struct.

     @param prefix_length The length of the search key prefix to be used for
     lookup.

     @param keypart_map A set (bitmap) of keyparts to be used.
  */
  void make_min_endpoint(key_range *kr, uint prefix_length,
                         key_part_map keypart_map) {
    make_min_endpoint(kr);
    kr->length = std::min(kr->length, prefix_length);
    kr->keypart_map &= keypart_map;
  }

  /**
     Initalizes a key_range object for communication with storage engine.

     This function facilitates communication with the Storage Engine API by
     translating the minimum endpoint of the interval represented by this
     QUICK_RANGE into an index range endpoint specifier for the engine.

     @param kr Pointer to an uninitialized key_range C struct.
  */
  void make_min_endpoint(key_range *kr) {
    kr->key = (const uchar *)min_key;
    kr->length = min_length;
    kr->keypart_map = min_keypart_map;
    kr->flag = ((flag & NEAR_MIN) ? HA_READ_AFTER_KEY
                                  : (flag & EQ_RANGE) ? HA_READ_KEY_EXACT
                                                      : HA_READ_KEY_OR_NEXT);
  }

  /**
     Initalizes a key_range object for communication with storage engine.

     This function facilitates communication with the Storage Engine API by
     translating the maximum endpoint of the interval represented by this
     QUICK_RANGE into an index range endpoint specifier for the engine.

     @param kr Pointer to an uninitialized key_range C struct.

     @param prefix_length The length of the search key prefix to be used for
     lookup.

     @param keypart_map A set (bitmap) of keyparts to be used.
  */
  void make_max_endpoint(key_range *kr, uint prefix_length,
                         key_part_map keypart_map) {
    make_max_endpoint(kr);
    kr->length = std::min(kr->length, prefix_length);
    kr->keypart_map &= keypart_map;
  }

  /**
     Initalizes a key_range object for communication with storage engine.

     This function facilitates communication with the Storage Engine API by
     translating the maximum endpoint of the interval represented by this
     QUICK_RANGE into an index range endpoint specifier for the engine.

     @param kr Pointer to an uninitialized key_range C struct.
  */
  void make_max_endpoint(key_range *kr) {
    kr->key = (const uchar *)max_key;
    kr->length = max_length;
    kr->keypart_map = max_keypart_map;
    /*
      We use READ_AFTER_KEY here because if we are reading on a key
      prefix we want to find all keys with this prefix
    */
    kr->flag = (flag & NEAR_MAX ? HA_READ_BEFORE_KEY : HA_READ_AFTER_KEY);
  }
};

enum RangeScanType {
  QS_TYPE_RANGE,
  QS_TYPE_INDEX_MERGE,
  QS_TYPE_ROR_INTERSECT,
  QS_TYPE_ROR_UNION,
  QS_TYPE_GROUP_MIN_MAX,
  QS_TYPE_SKIP_SCAN
};

/*
  Quick select interface.
  This class is a parent for all QUICK_*_SELECT classes.

  The usage scenario is as follows:
  1. Create quick select
    quick = new (mem_root) QUICK_XXX_SELECT(...);

  2. Perform lightweight initialization.
    if (quick->init())
    {
      //the only valid action after failed init() call is destroy
      destroy(quick);
    }

  3. Perform zero, one, or more scans.
    while (...)
    {
      // initialize quick select for scan. This may allocate
      // buffers and/or prefetch rows.
      if (quick->reset())
      {
        //the only valid action after failed reset() call is destroy
        destroy(quick);
        //abort query
      }

      // perform the scan
      do
      {
        res= quick->get_next();
      } while (res && ...)
    }

  4. Destroy the select:
    destroy(quick);
*/

class QUICK_SELECT_I {
 public:
  TABLE *m_table;

  QUICK_SELECT_I() = default;
  virtual ~QUICK_SELECT_I() = default;

  /*
    Do post-constructor initialization.
    SYNOPSIS
      init()

    init() performs initializations that should have been in constructor if
    it was possible to return errors from constructors. The join optimizer may
    create and then destroy quick selects without retrieving any rows so init()
    must not contain any IO or CPU intensive code.

    If init() call fails the only valid action is to destroy this quick select,
    reset() and get_next() must not be called.

    RETURN
      0      OK
      other  Error code
  */
  virtual int init() = 0;

  /*
    Initialize quick select for row retrieval.
    SYNOPSIS
      reset()

    reset() should be called when it is certain that row retrieval will be
    necessary. This call may do heavyweight initialization like buffering first
    N records etc. If reset() call fails get_next() must not be called.
    Note that reset() may be called several times if
     * the quick select is executed in a subselect
     * a JOIN buffer is used

    RETURN
      0      OK
      other  Error code
  */
  virtual int reset(void) = 0;

  /* get next record to retrieve */
  virtual int get_next() = 0;

  /*
    Row ID of last row retrieved by this quick select. This is used only when
    doing ROR-index_merge selects. Updated on successful get_next().
  */
  uchar *last_rowid = nullptr;
};

using Quick_ranges = Mem_root_array<QUICK_RANGE *>;
using Quick_ranges_array = Mem_root_array<Quick_ranges *>;

int test_quick_select(THD *thd, MEM_ROOT *return_mem_root,
                      MEM_ROOT *temp_mem_root, Key_map keys_to_use,
                      table_map prev_tables, table_map read_tables,
                      ha_rows limit, bool force_quick_range,
                      const enum_order interesting_order, TABLE *table,
                      bool skip_records_in_range, Item *cond,
                      Key_map *needed_reg, bool ignore_table_scan,
                      Query_block *query_block, TABLE_READ_PLAN **trp);

void store_key_image_to_rec(Field *field, uchar *ptr, uint len);

extern String null_string;

/// Global initialization of the null_element. Call on server start.
void range_optimizer_init();

/// Global destruction of the null_element. Call on server stop.
void range_optimizer_free();

/**
  Test if 'value' is comparable to 'field' when setting up range
  access for predicate "field OP value". 'field' is a field in the
  table being optimized for while 'value' is whatever 'field' is
  compared to.

  @param cond_func   the predicate item that compares 'field' with 'value'
  @param field       field in the predicate
  @param itype       itMBR if indexed field is spatial, itRAW otherwise
  @param comp_type   comparator for the predicate
  @param value       whatever 'field' is compared to

  @return true if 'field' and 'value' are comparable, false otherwise
*/

bool comparable_in_index(Item *cond_func, const Field *field,
                         const Field::imagetype itype,
                         Item_func::Functype comp_type, const Item *value);

#endif  // SQL_RANGE_OPTIMIZER_RANGE_OPTIMIZER_H_
