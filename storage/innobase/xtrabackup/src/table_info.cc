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

#include "xtrabackup.h"
#include "table_info.h"
#include "common.h"
#include "backup_copy.h" // for trim_dotslash(), end_with() utils
#include "ha_prototypes.h" // for xb_innobase_convert_identifier

#include <list>
#include <cJSON.h>
#include <unordered_map>

/* Check #define in univ.i for MAX_FULL_NAME_LEN */
#define FULL_NAME_LEN 320 + 320 + 14

typedef enum {
  XB_FILE_INNODB_DATA,
  XB_FILE_INNODB_SYS,
  XB_FILE_INNODB_LOG,
  XB_FILE_TOKUDB,
  XB_FILE_ROCKSDB,
  XB_FILE_MYISAM,
  XB_FILE_DB_OPT,
  XB_FILE_OTHER /* CSV, ARCHIVE, MERGE, TRIGGER, PARTITION, etc.  */
} xb_file_type;

const char * file_type_name[] = {
  "InnoDB Data",
  "InnoDB System",
  "InnoDB Log",
  "TokuDB",
  "RocksDB",
  "MyISAM",
  "opt",
  "Other"
};

typedef enum {
  XB_FILE_CAT_SINGLE,
  XB_FILE_CAT_COMMON
} xb_file_category;

typedef struct {
  char db[FULL_NAME_LEN + 1];
  char table[FULL_NAME_LEN + 1];
  char filepath[FULL_NAME_LEN + 1];
  char extra[FULL_NAME_LEN + 1];
       xb_file_type file_type;
       xb_file_category file_cat;
  size_t begin;
  size_t end;
} xb_table_info;

static std::list<xb_table_info*> table_info_list;
static std::unordered_map<std::string, cJSON *> table_json_cache;

typedef struct {
  char full_name[FN_REFLEN * 2];
  char engine_name[128]; /* 128 is enough for storage engine name */
} table_entry_t;

static bool table_info_mode_enabled = false;

extern void *(* xb_tmp_prepare_info) (const char *filepath);
extern void (* xb_tmp_finish_info) (void *info);


#define MAX_EXT_NUM 10
typedef struct {
  xb_file_type file_type;
  const char *pattern_list[MAX_EXT_NUM];
} xb_file_pattern_info;

xb_file_pattern_info xb_pat_map[] = {
  {XB_FILE_INNODB_DATA, {".*\\.ibd$", NULL}},
  /* FIXME this may not hold if user choose a different innodb_data_file_path conf,
  but it's will always be ibdata1 for RDS env. */
  {XB_FILE_INNODB_SYS, {".*\\ibdata[[:digit:]]+$", NULL}},
  {XB_FILE_INNODB_LOG, {"xtrabackup_logfile.*[[:digit:]]*$", NULL}},

  {XB_FILE_TOKUDB, {".*\\.tokudb$",  /* datafile */
         ".*\\.tokulog[[:digit:]]+$", /* redo log */
         ".*\\.directory$",  /* table dictionary */
         ".*\\.rollback$",  /* undo log */
         ".*\\.environment$",  /* meta info */
         NULL}},

  {XB_FILE_ROCKSDB, {".*\\.sst$",
         /* Maybe conflict with slow/general log,
         but they won't be backed up */
         ".*\\.log$",
         ".*MANIFEST-[[:digit:]]+$",
         ".*CURRENT$",
         ".*OPTIONS-[[:digit:]]+$",
         NULL}},

  {XB_FILE_MYISAM, {".*\\.MYD$", ".*\\.MYI$", NULL}},
  {XB_FILE_DB_OPT, {".*\\.opt$", NULL}}
};


static xb_file_type get_file_type(const char *filepath) {
  xb_file_type file_type;
  int i, map_size;

  if (!strcmp(filepath, "mysql.ibd")) {
    return XB_FILE_OTHER;
  }

  map_size = sizeof(xb_pat_map) / sizeof(xb_pat_map[0]);
  file_type = XB_FILE_OTHER; // Default type

  for (i = 0; i < map_size; i ++) {
    if (filename_matches_regex(filepath, xb_pat_map[i].pattern_list)) {
      file_type = xb_pat_map[i].file_type;
      break;
    }
  }

  return file_type;
}

/********************************************************************//**
Prepare table info according the file path, this function must be
called before copying the file indentified by filepath to get the begin
position.

1. Get type info according the suffix, store in info->type
2. Separate db and table name, and store them in info->db and info->table
3. Partition table is explictly indentified, in the info->exta
4. Get the stream position, and use it as the info->begin

@return the point to the allocated table_info object */
void *xb_prepare_table_info(const char *filepath) {
  xb_table_info * info;
  xb_file_type type;
  const char *db, *table;
  char *s, *par;
  char full_name[MAX_FULL_NAME_LEN + 1];

  if (!table_info_mode_enabled) {
    return NULL;
  }

  /* remove leading '.' or '/' */
  strcpy(full_name, trim_dotslash(filepath));

  type = get_file_type(full_name);

  info = static_cast<xb_table_info *> (ut_malloc_nokey(sizeof(xb_table_info)));
  memset(info, '\0', sizeof(xb_table_info));

  db = "";
  table = full_name;

  msg_ts("xb_prepare_table_info type=%d table=%s\n", (int)type, table);

  /* Separate db and table for InnoDB table */
  if (type == XB_FILE_INNODB_DATA) {
    s = strchr(full_name, '/');
    ut_a(s != NULL);
    *s = '\0';
    db = full_name;
    xb_innobase_convert_identifier(info->db, MAX_FULL_NAME_LEN + 1,
                 db, strlen(db), true);

    /* innodb data file must ending with .ibd */
    table = ++s;
    ut_a(s[strlen(s) - 4] == '.');
    s[strlen(s) - 4] = '\0';

    char table_name[MAX_FULL_NAME_LEN + 1];
    xb_innobase_convert_identifier(table_name, MAX_FULL_NAME_LEN + 1,
                 table, strlen(table), false);

    /* check whether partition table */
    if ((par = strstr(table_name, " /* Partition")) != NULL) {

      *(par++) = '\0';
      strcpy(info->extra, par);
    }
    /* remove surrounding '"' */
    table_name[strlen(table_name) - 1] = '\0';
    strcpy(info->table, table_name + 1);

  } else if (type == XB_FILE_DB_OPT) {
    /* db */
    s = strchr(full_name, '/');
    ut_a(s != NULL);
    *(s++) = '\0';
    db = full_name;
    xb_innobase_convert_identifier(info->db, MAX_FULL_NAME_LEN + 1,
                 db, strlen(db), true);
    /* table, which is 'db.opt' */
    if (*s == '/') {
      *(s++) = '\0';
    }
    table = s;
    ut_a(!strcmp(table, "db.opt"));
    strcpy(info->table, table);
  } else {
    strcpy(info->db, db);
    /* strip full path for ibdata1 */
    if (type == XB_FILE_INNODB_SYS) {
      strcpy(info->table, basename(table));
    } else {
      strcpy(info->table, table);
    }
  }

  if (type == XB_FILE_INNODB_SYS) {
    strcpy(info->filepath, basename(filepath));
  } else {
    strcpy(info->filepath, filepath);
  }

  info->file_type = type;

  if (type == XB_FILE_INNODB_DATA) {

    if (!strcmp(info->db, "mysql") || !strcmp(info->db, "sys")) {

      info->file_cat = XB_FILE_CAT_COMMON;

    } else {

      info->file_cat = XB_FILE_CAT_SINGLE;
    }

  } else {

    info->file_cat = XB_FILE_CAT_COMMON;
  }

  ds_size(ds_dest, NULL, &(info->begin));

  return static_cast<void *> (info);
}

/********************************************************************//**
Finish table info, this function must be called after copying the file
indentified by filepath to get the end position.

1. Get stream position, and use it as end position
2. Store the info in info_list */
void xb_finish_table_info(void *info_p) {
  xb_table_info *info;

  if (!table_info_mode_enabled) {
    return;
  }

  info = static_cast<xb_table_info *> (info_p);

  ds_size(ds_dest, NULL, &(info->end));

  /* FIXME protect using mutex */
  table_info_list.push_back(info);
}

/********************************************************************//**
Convert infos in table list to JSON format.

The JSON info is organized in this way:
1. All innodb table files except those under 'mysql' database are grouped
by database name
2. Other files, include MyISAM, TokuDB, MyRocks, frm, etc., are put in one
common group, using null group name.
@return a string contains JSON info, who gets it must free it. */
static char *convert_table_info_to_json() {
  cJSON *root_json = cJSON_CreateArray();
  cJSON *db_json = NULL;
  cJSON *table_array = NULL;
  cJSON *table_json = NULL;
  cJSON *common_db_json = NULL;
  cJSON *common_table_array = NULL;
  char *json_string = NULL;
  const char *cur_db = "";

  /* all files except InnoDB data are put into common part */
  common_db_json = cJSON_CreateObject();
  cJSON_AddStringToObject(common_db_json, "type", "common");
  cJSON_AddStringToObject(common_db_json, "name", "common-tables");
  cJSON_AddItemToObject(common_db_json, "tables", common_table_array = cJSON_CreateArray());

  std::list<xb_table_info *>::iterator it;
  for (it = table_info_list.begin(); it != table_info_list.end(); it++) {
    xb_table_info *info = *it;

    if (info->file_cat == XB_FILE_CAT_SINGLE) {

      cur_db = info->db;
      msg_ts("convert_table_info_to_json cur_db=%s\n", cur_db);
      table_array = table_json_cache[cur_db];
      if (table_array == NULL) {
        cJSON_InsertItemInArray(root_json, 0, db_json = cJSON_CreateObject());
        cJSON_AddStringToObject(db_json, "type", "db");
        cJSON_AddStringToObject(db_json, "name", cur_db);
        cJSON_AddItemToObject(db_json, "tables", table_array = cJSON_CreateArray());

        table_json_cache[cur_db] = table_array;
      }

      cJSON_InsertItemInArray(table_array, 0, table_json = cJSON_CreateObject());
      cJSON_AddStringToObject(table_json, "db", info->db);
      cJSON_AddStringToObject(table_json, "name", info->table);
      cJSON_AddStringToObject(table_json, "type", "table");
      cJSON_AddStringToObject(table_json, "filepath", info->filepath);
      cJSON_AddStringToObject(table_json, "filetype", file_type_name[info->file_type]);
      cJSON_AddStringToObject(table_json, "extra", info->extra);
      cJSON_AddNumberToObject(table_json, "begin", info->begin);
      cJSON_AddNumberToObject(table_json, "end", info->end);

    } else {

      cJSON_InsertItemInArray(common_table_array, 0, table_json = cJSON_CreateObject());
      cJSON_AddStringToObject(table_json, "db", info->db);
      cJSON_AddStringToObject(table_json, "name", info->table);
      cJSON_AddStringToObject(table_json, "type", "table");
      cJSON_AddStringToObject(table_json, "filepath", info->filepath);
      cJSON_AddStringToObject(table_json, "filetype", file_type_name[info->file_type]);
      cJSON_AddStringToObject(table_json, "extra", info->extra);
      cJSON_AddNumberToObject(table_json, "begin", info->begin);
      cJSON_AddNumberToObject(table_json, "end", info->end);

    }
  }

  cJSON_InsertItemInArray(root_json, 0, common_db_json);

  /* we'd better print unformatted to save space */
  // char *out = cJSON_PrintUnformatted(root_json);
  json_string = cJSON_Print(root_json);

  /* clean up JSON object */
  table_json_cache.clear();
  cJSON_Delete(root_json);

  return json_string;
}

/********************************************************************//**
Serialize table info list to a JSON string, write to a file and
backup the file. */
bool serialize_table_info_and_backup(ds_ctxt_t *ds) {
  char json_file_name[FN_REFLEN];
  char *json = NULL;
  size_t json_len;
  FILE *fp = NULL;
  bool ret = TRUE;
  ds_ctxt_t *old_ds_data;

  if (!table_info_mode_enabled) {
    return TRUE;
  }

  /* convert table info list to json string */
  json = convert_table_info_to_json();

  /* write json string to a local file */
  snprintf(json_file_name, sizeof(json_file_name), "%s/%s_%u.log",
     xtrabackup_target_dir, "rds_table_info_json", getpid());
  json_len = strlen(json);
  fp = fopen(json_file_name, "w");

  if(!fp) {
    msg_ts("xtrabackup: Error: cannot open %s\1\n", json_file_name);
    ret = FALSE;
    goto out;
  }

  if (fwrite(json, json_len, 1, fp) < 1) {
    ret = FALSE;
    goto out;
  }

  /*
    Backup json string, some notes:
    1. FIXME this is a little hacking, we change ds_data to the ds the caller specified,
    backup_file_print() will use the global ds_data point. The original ds_data may
    already be destroied.
    2. A new xb_table_info entry will be pushed into table_info_list (see backup_file_print()),
    it will not be displayed in the json string.
  */

  old_ds_data = ds_data;
  ds_data = ds;
  backup_file_print(basename(json_file_name), json, json_len);
  ds_data = old_ds_data;
out:
  if (fp) {

    fclose(fp);
  }

  if (json) {
    free(json);
  }
  return ret;
}

bool table_info_init() {
  table_info_mode_enabled = true;
  xb_tmp_prepare_info = xb_prepare_table_info;
  xb_tmp_finish_info = xb_finish_table_info;

  return FALSE;
}

void table_info_deinit() {
  if (!table_info_mode_enabled) {
    return;
  }

  /* free all table info elements */
  while (!table_info_list.empty()) {
    xb_table_info * info = table_info_list.front();
    ut_free(info);
    table_info_list.pop_front();
  }
}
