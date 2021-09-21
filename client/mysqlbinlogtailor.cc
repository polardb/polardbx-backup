/**
   mysqlbinlogtailor

   Ver 0.1
   ==========
   This tool is used to truncate binlog at given time, which means all
   the binlog events after the give time is discarded.
   The trncate is inplace, so please use this on a copy of binlog.

   The code is based on mysqlbinlog.cc, and the main reason to invent
   this tool is to manipulate binary logs, so maybe in future more featrue
   will be added in, not just truncate binlog for now, that why use the name
   *mysqlbinlogtailor*, like a tailor for binlog. :-)

*/

#define MYSQL_CLIENT
#undef MYSQL_SERVER
#include "client_priv.h"
#include "my_default.h"
#include <my_time.h>
/* That one is necessary for defines of OPTION_NO_FOREIGN_KEY_CHECKS etc */
#include "query_options.h"
#include <signal.h>
#include <my_dir.h>

/*
  error() is used in macro BINLOG_ERROR which is invoked in
  rpl_gtid.h, hence the early forward declaration.
*/
void error(const char *format, ...) MY_ATTRIBUTE((format(printf, 1, 2)));
/* comment out for now */
/* static void warning(const char *format, ...) ATTRIBUTE_FORMAT(printf, 1, 2); */

static void error_or_warning(const char *format, va_list args, const char *msg)
    MY_ATTRIBUTE((format(printf, 1, 0)));
#if 0
static void sql_print_error(const char *format, ...)
    MY_ATTRIBUTE((format(printf, 1, 2)));
#endif
#include "rpl_gtid.h"
#include "sql/binlog_reader.h"
#include "sql/log_event.h"
// #include "consensus_log_event.h"
// #include "log_event_old.h"
#include "sql_common.h"
#include "my_dir.h"
#include <welcome_copyright_notice.h> // ORACLE_WELCOME_COPYRIGHT_NOTICE
#include "sql_string.h"
#include "my_decimal.h"
#include "rpl_constants.h"

#include <algorithm>

using std::min;
using std::max;

#define BIN_LOG_HEADER_SIZE 4U
#define PROBE_HEADER_LEN (EVENT_LEN_OFFSET+4)
#define INTVAR_DYNAMIC_INIT 16
#define INTVAR_DYNAMIC_INCR 1


static char *truncate_datetime_str= NULL;
my_time_t truncate_datetime= MY_TIME_T_MAX;
bool binlog_need_checksum= false;
static bool trim_tail_incomplete_trx= 0;
static bool force_append_rotate_event= 0;
static bool show_index_info=0;
static ulonglong truncate_index_from=UINT64_MAX;

/* define the variables use in server's cc files, see the at the end of this file */
ulong bytes_sent= 0L, bytes_received= 0L;
ulong mysqld_net_retry_count= 10L;
ulong open_files_limit;
ulong opt_binlog_rows_event_max_size;
uint test_flags= 0;
// static bool force_opt= 0;
bool short_form= 0;
ulong opt_server_id_mask= 0;

char server_version[SERVER_VERSION_LENGTH];

Sid_map *global_sid_map= NULL;
Checkable_rwlock *global_sid_lock= NULL;
Gtid_set *gtid_set_included= NULL;
Gtid_set *gtid_set_excluded= NULL;

/**
  Pointer to the Format_description_log_event of the currently active binlog.

  This will be changed each time a new Format_description_log_event is
  found in the binlog. It is finally destroyed at program termination.
*/
Format_description_log_event* glob_description_event= NULL;

/**
  Exit status for functions in this file.
*/
enum Exit_status {
  /** No error occurred and execution should continue. */
  OK_CONTINUE= 0,
  /** An error occurred and execution should stop. */
  ERROR_STOP,
  /** No error occurred but execution should stop. */
  OK_STOP
};

static struct my_option my_long_options[] =
{
  {"help", '?', "Display this help and exit.",
   0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"truncate-datetime", OPT_TRUNCATE_DATETIME,
   "Truncate the binlog at first event having a datetime equal or"
   "posterior to the argument; the argument must be a date and time "
   "in the local time zone, in any format accepted by the MySQL server "
   "for DATETIME and TIMESTAMP types, for example: 2004-12-25 11:25:56 "
   "(you should probably use quotes for your shell to set it properly).",
   &truncate_datetime_str, &truncate_datetime_str,
   0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
  {"trim-tail-incomplete-trx", OPT_TRIM_TAIL_INCOMPLETE_TRX,
   "Trim tail incomplete transaction, and append a rotate event at end. "
   "Although transaction can not span two binary logs, there are some situations "
   "that binlog ends with an incomplete transaction, such as crash, or active "
   "binary log.",
   &trim_tail_incomplete_trx, &trim_tail_incomplete_trx, 0,
   GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"force-append-rotate-event", OPT_APPEND_ROTATE_EVENT,
   "Force appending a rorate event at end of binlog (this option should not be used "
   "together with --truncate-datetime and --trim-tail-incomplete-trx).",
   &force_append_rotate_event, &force_append_rotate_event, 0,
   GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"show-index-info", OPT_SHOW_INDEX_INFO,
    "Show index info in a binlog, [start_index:start_term, end_index:end_term]",
    &show_index_info, &show_index_info,
    0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"truncate-index-from", OPT_TRUNCATE_INDEX_FROM,
    "Truncate index from start index in a binlog, [start, ...)",
    &truncate_index_from, &truncate_index_from,
    0, GET_ULL, REQUIRED_ARG, -1, 0, UINT64_MAX, 0, 0, 0},
  {"version", 'V', "Print version and exit.", 0, 0, 0, GET_NO_ARG, NO_ARG, 0,
   0, 0, 0, 0, 0},
  {0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};

/**
  Auxiliary function used by error() and warning().

  Prints the given text (normally "WARNING: " or "ERROR: "), followed
  by the given vprintf-style string, followed by a newline.

  @param format Printf-style format string.
  @param args List of arguments for the format string.
  @param msg Text to print before the string.
*/
static void error_or_warning(const char *format, va_list args, const char *msg)
{
  fprintf(stderr, "%s: ", msg);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
}

/**
  Prints a message to stderr, prefixed with the text "ERROR: " and
  suffixed with a newline.

  @param format Printf-style format string, followed by printf
  varargs.
*/
void error(const char *format,...)
{
  va_list args;
  va_start(args, format);
  error_or_warning(format, args, "ERROR");
  va_end(args);
}

#if 0
/**
  This function is used in log_event.cc to report errors.

  @param format Printf-style format string, followed by printf
  varargs.
*/
static void sql_print_error(const char *format,...)
{
  va_list args;
  va_start(args, format);
  error_or_warning(format, args, "ERROR");
  va_end(args);
}
#endif
/**
  Prints a message to stderr, prefixed with the text "WARNING: " and
  suffixed with a newline.

  @param format Printf-style format string, followed by printf
  varargs.
*/
/* comment out for now
static void warning(const char *format,...)
{
  va_list args;
  va_start(args, format);
  error_or_warning(format, args, "WARNING");
  va_end(args);
  } */

static void print_version()
{
  printf("%s Ver 0.1 for %s at %s\n", my_progname, SYSTEM_TYPE, MACHINE_TYPE);
}


static void usage()
{
  print_version();
  printf("\
Manipulates a MySQL binary log file in request manner, such as truncate to\n\
a given datetime.\n\n");
  printf("Usage: %s [options] log-files\n", my_progname);
  my_print_help(my_long_options);
  my_print_variables(my_long_options);
}


static my_time_t convert_str_to_timestamp(const char* str)
{
  MYSQL_TIME_STATUS status;
  MYSQL_TIME l_time;
  long dummy_my_timezone;
  bool dummy_in_dst_time_gap;
  /* We require a total specification (date AND time) */
  if (str_to_datetime(str, (uint) strlen(str), &l_time, 0, &status) ||
      l_time.time_type != MYSQL_TIMESTAMP_DATETIME || status.warnings)
  {
    error("Incorrect date and time argument: %s", str);
    exit(1);
  }
  /*
    Note that Feb 30th, Apr 31st cause no error messages and are mapped to
    the next existing day, like in mysqld. Maybe this could be changed when
    mysqld is changed too (with its "strict" mode?).
  */
  return
    my_system_gmt_sec(l_time, &dummy_my_timezone, &dummy_in_dst_time_gap);
}


extern "C" bool
get_one_option(int optid, const struct my_option *opt __attribute__((unused)),
               char *)
{
  switch (optid) {
  case OPT_TRUNCATE_DATETIME:
    truncate_datetime= convert_str_to_timestamp(truncate_datetime_str);
    break;
  case 'V':
    print_version();
    exit(0);
  case '?':
    usage();
    exit(0);
  }
  return 0;
}

static int parse_args(int *argc, char*** argv)
{
  int ho_error;
  if ((ho_error=handle_options(argc, argv, my_long_options, get_one_option)))
    exit(ho_error);
  return 0;
}

/* check whether is the very last event and invalid */
static bool is_tail_invalid_event(IO_CACHE* file,
                                      const Format_description_log_event
                                      *description_event,
                                      my_off_t file_size)
{
  my_off_t pos = my_b_tell(file);
  uint header_size= min<uint>(description_event->common_header_len,
                              LOG_EVENT_MINIMAL_HEADER_LEN);
  /* can not parse a valid header */
  if (pos + header_size > file_size)
  {
    return true;
  }

  char head[LOG_EVENT_MINIMAL_HEADER_LEN];
  /* TODO this rarely happened and is a fatal error */
  if (unlikely(my_b_read(file, (uchar *) head, header_size)))
  {
    return true;
  }
  else
  {
    ulong data_len = uint4korr(head + EVENT_LEN_OFFSET);
    if (pos + data_len > file_size)
      return true;
  }
  return false;
}

/**
  Reads the @c Format_description_log_event from the beginning of a
  local input file.

  If this is an old binlog, a fake @c Format_description_event is created,
  in other case, it is assumed that a @c Format_description_log_event will
  be found when reading events the usual way.

  @param file The file to which a @c Format_description_log_event will
  be printed.

  @retval ERROR_STOP An error occurred - the program should terminate.
  @retval OK_CONTINUE No error, the program should continue.
*/

static Exit_status check_header(IO_CACHE* file, my_off_t* size)

{
  DBUG_ENTER("check_header");
  uchar header[BIN_LOG_HEADER_SIZE];
  uchar buf[PROBE_HEADER_LEN];
  my_off_t tmp_pos, pos;
  MY_STAT my_file_stat;

  delete glob_description_event;
  if (!(glob_description_event= new Format_description_log_event()))
  {
    error("Failed creating Format_description_log_event; out of memory?");
    DBUG_RETURN(ERROR_STOP);
  }

  /* hold the old position */
  pos= my_b_tell(file);

  /* fstat the file to check if the file is a regular file. */
  if (my_fstat(file->file, &my_file_stat) == -1)
  {
    error("Unable to stat the file.");
    DBUG_RETURN(ERROR_STOP);
  }
  *size = my_file_stat.st_size;

  if ((my_file_stat.st_mode & S_IFMT) == S_IFREG)
    my_b_seek(file, (my_off_t)0);

  /* check if it is a binlog file, BINLOG_MAGIC !!! */
  if (my_b_read(file, header, sizeof(header)))
  {
    error("Failed reading header; probably an empty file.");
    DBUG_RETURN(ERROR_STOP);
  }
  if (memcmp(header, BINLOG_MAGIC, sizeof(header)))
  {
    error("File is not a binary log file.");
    DBUG_RETURN(ERROR_STOP);
  }

  /**
    We need to read the first events of the log, those around offset 4,
    to dertermine the binlog fomart version, whether it's the v1(3.23)
    or v3(4.x). v4 format will be handled in truncate_binlog.
   */
    tmp_pos= my_b_tell(file);
    if (my_b_read(file, buf, sizeof(buf)))
    {
      error("Could not read entry at offset %llu: "
            "Error in log format or read error.", (ulonglong)tmp_pos);
      DBUG_RETURN(ERROR_STOP);
    }
    else
    {
      /* always test for a Start_v3 */
      if (buf[EVENT_TYPE_OFFSET] == binary_log::START_EVENT_V3)
      {
        /* This is 3.23 or 4.x */
        if (uint4korr(buf + EVENT_LEN_OFFSET) <
            (LOG_EVENT_MINIMAL_HEADER_LEN + Binary_log_event::START_V3_HEADER_LEN))
        {
          /* This is 3.23 (format 1) */
          delete glob_description_event;
          if (!(glob_description_event= new Format_description_log_event()))
          {
            error("Failed creating Format_description_log_event; "
                  "out of memory?");
            DBUG_RETURN(ERROR_STOP);
          }
        }
      }
    }
    my_b_seek(file, pos);
    DBUG_RETURN(OK_CONTINUE);
}

/**
  Append a Rotate_log_event to a binlog after specified position.

  @param[in] logname Name of the binlog.
  @param[in] pos Position to append after.

  @retval ERROR_STOP An error occurred - the program should terminate.
  @retval OK_CONTINUE No error, the program should continue.
*/
/*
static Exit_status append_rotate_event(const char* logname, my_off_t pos)
{
  File fd= -1;
  IO_CACHE cache, *file= &cache;
  Exit_status retval= OK_CONTINUE;
  Rotate_log_event* ev= NULL;

  if ((fd= my_open(logname, O_RDWR | O_BINARY, MYF(MY_WME))) < 0)
    return ERROR_STOP;

  if (init_io_cache(file, fd, 0, WRITE_CACHE, pos, 0,
                    MYF(MY_WME | MY_NABP)))
  {
    my_close(fd, MYF(MY_WME));
    return ERROR_STOP;
  }

  ev= new Rotate_log_event(logname + dirname_length(logname),
                           0, pos, Rotate_log_event::DUP_NAME);

  // ev->write() will update log_pos to correct value when log_pos is 0
  ev->common_header->log_pos= 0;
  if (ev->write(file))
    retval= ERROR_STOP;

  if (end_io_cache(file))
    retval= ERROR_STOP;

  if (fd >= 0)
    my_close(fd, MYF(MY_WME));

  return retval;
}
*/

/* whether this event starts a group */
bool starts_group(Log_event *ev)
{
  Log_event_type ev_type= ev->get_type_code();

  /* consensus log event always starts a group */
  if (ev_type == binary_log::CONSENSUS_LOG_EVENT)
  {
    return true;
  }

  return false;
}

/* whether this event ends a group */
bool ends_group(Log_event *ev)
{
  Log_event_type ev_type= ev->get_type_code();

  if (ev_type == binary_log::QUERY_EVENT)
  {
    return ((Query_log_event*) ev)->ends_group();
  }
  /* xid event always ends a group */
  else if (ev_type == binary_log::XID_EVENT)
  {
    return true;
  }
  return false;
}

/* truncate at given position and append a rotate event after that */
static bool do_truncate_and_append(const File fd,
                                   const char* logname,
                                   const my_off_t pos)
{
  bool ret= false;
  char llbuff[21];

  if (my_chsize(fd, pos, 0, MYF(MY_WME)))
  {
    error("Failed to truncate binlog '%s' to '%s'.",
          logname, llstr(pos,llbuff));
    ret= true;
  }
  else
  {
    // if (append_rotate_event(logname, pos) != OK_CONTINUE)
    // {
    //   error("Failed to append Rotate_log_event to '%s' after '%s'.",
    //       logname, llstr(pos, llbuff));
    //   ret= true;
    // }
    printf("binlog '%s' truncated to '%s'.\n", logname, llstr(pos, llbuff));
  }

  return ret;
}

/**
   Two things are done in this class:
   - rewrite the database name in event_data if rewrite option is configured.
   - Skip the extra BINLOG_MAGIC when reading event data if
     m_multiple_binlog_magic is set. It is used for the case when users feed
     more than one binlog files through stdin.
 */
class Mysqlbinlog_event_data_istream : public Binlog_event_data_istream {
 public:
  using Binlog_event_data_istream::Binlog_event_data_istream;

  template <class ALLOCATOR>
  bool read_event_data(unsigned char **buffer, unsigned int *length,
                       ALLOCATOR *allocator, bool verify_checksum,
                       enum_binlog_checksum_alg checksum_alg) {
    return Binlog_event_data_istream::read_event_data(
               buffer, length, allocator, verify_checksum, checksum_alg);
    // || rewrite_db(buffer, length);
  }

  void set_multi_binlog_magic() { m_multi_binlog_magic = true; }

 private:
  bool m_multi_binlog_magic = false;
#if 0
  bool rewrite_db(unsigned char **buffer, unsigned int *length) {
    ulong len = *length;
    if (rewrite_db_filter(reinterpret_cast<char **>(buffer), &len,
                          glob_description_event)) {
      error("Error applying filter while reading event");
      return m_error->set_type(Binlog_read_error::MEM_ALLOCATE);
    }
    DBUG_ASSERT(len < UINT_MAX);
    *length = static_cast<unsigned int>(len);
    return false;
  }
#endif
  bool read_event_header() override {
    if (Binlog_event_data_istream::read_event_header()) return true;
    /*
      If there are more than one binlog files in the stdin, it checks and skips
      the binlog magic heads of following binlog files.
    */
    if (m_multi_binlog_magic &&
        memcmp(m_header, BINLOG_MAGIC, BINLOG_MAGIC_SIZE) == 0) {
      size_t header_len = LOG_EVENT_MINIMAL_HEADER_LEN - BINLOG_MAGIC_SIZE;

      // Remove BINLOG_MAGIC from m_header
      memmove(m_header, m_header + BINLOG_MAGIC_SIZE, header_len);
      // Read the left BINLOG_MAGIC_SIZE bytes of the header
      return read_fixed_length<Binlog_read_error::TRUNC_EVENT>(
          m_header + header_len, BINLOG_MAGIC_SIZE);
    }
    return false;
  }
};

class Mysqlbinlog_ifile : public Basic_binlog_ifile {
 public:
  using Basic_binlog_ifile::Basic_binlog_ifile;

 private:
  std::unique_ptr<Basic_seekable_istream> open_file(
      const char *file_name) override {
    // if (file_name && strcmp(file_name, "-") != 0) {
      IO_CACHE_istream *iocache = new IO_CACHE_istream;
      if (iocache->open(
#ifdef HAVE_PSI_INTERFACE
              PSI_NOT_INSTRUMENTED, PSI_NOT_INSTRUMENTED,
#endif
              file_name, MYF(MY_WME | MY_NABP))) {
        delete iocache;
        return nullptr;
      }
      return std::unique_ptr<Basic_seekable_istream>(iocache);
#if 0
    } else {
      std::string errmsg;
      Stdin_binlog_istream *standard_in = new Stdin_binlog_istream;
      if (standard_in->open(&errmsg)) {
        error("%s", errmsg.c_str());
        delete standard_in;
        return nullptr;
      }
      return std::unique_ptr<Basic_seekable_istream>(standard_in);
    }
#endif
  }
};

typedef Basic_binlog_file_reader<
    Mysqlbinlog_ifile, Mysqlbinlog_event_data_istream,
    Binlog_event_object_istream, Default_binlog_event_allocator>
    Mysqlbinlog_file_reader;

/**
  Reads a local binlog and truncate it to given time or a given index.

  @param[in] logname Name of input binlog.

  @retval ERROR_STOP An error occurred - the program should terminate.
  @retval OK_CONTINUE No error, the program should continue.
  @retval OK_STOP No error, the binlog has been trucated to given time,
  and the program should terminate.
*/
static Exit_status truncate_binlog(const char* logname)
{
  File fd= -1; // open the binlog to truncate
  // IO_CACHE cache,*file= &cache;
  Exit_status retval= OK_CONTINUE;
  Log_event* ev= NULL;
  bool in_group= false;
  bool seen_begin= false;
  uint64_t consensus_start_index = UINT64_MAX, consensus_start_term = UINT64_MAX;
  uint64_t consensus_end_index = UINT64_MAX, consensus_end_term = UINT64_MAX;

  /*
    store the end position of last valid transaction, either a
    COMMIT/XID or DDL
  */
  my_off_t last_trx_end_pos= 0;
  my_off_t file_size= 0;

  ulong max_event_size = 0;
  mysql_get_option(NULL, MYSQL_OPT_MAX_ALLOWED_PACKET, &max_event_size);
  Mysqlbinlog_file_reader mysqlbinlog_file_reader(true,
                                                  max_event_size);

  Format_description_log_event *fdle = nullptr;
  if (mysqlbinlog_file_reader.open(logname, 0, &fdle)) {
    error("%s", mysqlbinlog_file_reader.get_error_str());
    return ERROR_STOP;
  }

  if ((fd= my_open(logname, O_RDWR, MYF(MY_WME))) < 0)
    return ERROR_STOP;
#if 0
  if (init_io_cache(file, fd, 0, READ_CACHE, BIN_LOG_HEADER_SIZE, 0,
                    MYF(MY_WME | MY_NABP)))
  {
    my_close(fd, MYF(MY_WME));
    return ERROR_STOP;
  }
#endif
  /* determine the binlog version, i.e. the glob_description_event */
  if ((retval= check_header(mysqlbinlog_file_reader.get_io_cache(), &file_size)) != OK_CONTINUE)
    goto end;

  if (!glob_description_event || !glob_description_event->is_valid())
  {
    error("Invalid Format_description log event; could be out of memory.");
    goto err;
  }

  for (;;)
  {
    char llbuff[21];
    // my_off_t old_off= mysqlbinlog_file_reader.position();
    my_off_t old_off= my_b_tell(mysqlbinlog_file_reader.get_io_cache());

    mysqlbinlog_file_reader.set_format_description_event(*glob_description_event);
    // ev= Log_event::read_log_event(file, glob_description_event, false, 0);
    ev = mysqlbinlog_file_reader.read_event_object();
    if (!ev)
    {
      /*
        if binlog wasn't closed properly ("in use" flag is set) don't complain
        about a corruption, but treat it as EOF and move to the next binlog.
      */
      /*
        Now RDS stream binlog in real time, so it is very likely in_use flag
        is not cleared when upload to OSS, i.e. this flag is almost useless.
      */
      // if (glob_description_event->flags & LOG_EVENT_BINLOG_IN_USE_F)
      //   file->error= 0;

      /* check whether this current corrupt event is the very last one */
      if (mysqlbinlog_file_reader.get_io_cache()->error)
      {
        if (trim_tail_incomplete_trx)
        {
          /* seek back to event beginning */
          my_b_seek(mysqlbinlog_file_reader.get_io_cache(), old_off);

          if (is_tail_invalid_event(mysqlbinlog_file_reader.get_io_cache(),
                                    glob_description_event,
                                    file_size))
            mysqlbinlog_file_reader.get_io_cache()->error= 0;
        }
      }

      if (mysqlbinlog_file_reader.get_io_cache()->error)
      {
        error("Could not read entry at offset %s: "
              "Error in log format or read error.",
              llstr(old_off,llbuff));
        goto err;
      }

      /* force appending a rotate event at end */
      if (force_append_rotate_event)
      {
        if (do_truncate_and_append(fd, logname, my_b_tell(mysqlbinlog_file_reader.get_io_cache())))
        {
          goto err;
        }
        else
        {
          /* we have done our job, end the program */
          retval= OK_CONTINUE;
          goto end;
        }
      }

      // file->error == 0 means EOF, that's OK.
      // if request to trim the last incomplete trx and
      // 1. inside a trx
      // 2. outside a trx and position of the very last event is same as
      //    last_trx_end_pos, which means there is no proper end event(such as
      //    ROTATE, STOP) at the end of binlog
      // we trim at the last_trx_end_pos position and append a rotate event
      if (trim_tail_incomplete_trx &&
          (in_group || last_trx_end_pos == old_off))
      {
        if (do_truncate_and_append(fd, logname, last_trx_end_pos))
        {
          goto err;
        }
        else
        {
          /* we have done our job, end the program */
          retval= OK_CONTINUE;
          goto end;
        }
      }

      if (show_index_info)
      {
        printf("[%lu:%lu, %lu:%lu]\n",
          consensus_start_index, consensus_start_term,
          consensus_end_index, consensus_end_term);
      }

      goto end;
    }
    if (ev->get_type_code() == binary_log::CONSENSUS_LOG_EVENT)
    {
      if (consensus_start_index == UINT64_MAX && consensus_start_term == UINT64_MAX)
      {
        consensus_start_index = ((Consensus_log_event *)ev)->get_index();
        consensus_start_term = ((Consensus_log_event *)ev)->get_term();
      }
      consensus_end_index = ((Consensus_log_event *)ev)->get_index();
      consensus_end_term = ((Consensus_log_event *)ev)->get_term();
      if (truncate_index_from != UINT64_MAX &&
          truncate_index_from == consensus_end_index)
      {
        if (do_truncate_and_append(fd, logname, old_off))
        {
          goto err;
        }
        else
        {
          /* we have done our job, end the program */
          retval= OK_CONTINUE;
          goto end;
        }
      }
    }

    /**
       this is the location we should truncate to, and the location should
       not inside a transaction.
    */
    if (!in_group && ((my_time_t) (ev->common_header->when.tv_sec) >= truncate_datetime))
    {
      if (do_truncate_and_append(fd, logname, old_off))
      {
        goto err;
      }
      else
      {
        /* we have done our job, end the program */
        retval= OK_CONTINUE;
        goto end;
      }
    }

    if (!in_group)
    {
      if (starts_group(ev))
        in_group= true;
    }
    else
    {
      if (ends_group(ev))
      {
        last_trx_end_pos= my_b_tell(mysqlbinlog_file_reader.get_io_cache());
        in_group= false;
        seen_begin= false;
      }
    }

    /*
      a fixup for DDL query
      A DDL is a QUERY event which is not profixed with a BEGIN QUERY
     */
    if (!seen_begin && ev->get_type_code() == binary_log::QUERY_EVENT)
    {
      if (starts_group(ev))
      {
        seen_begin= true;
      }
      /* DDL, a query neither BEGIN nor COMMIT */
      else if (!ends_group(ev))
      {
        in_group= false;
        last_trx_end_pos= my_b_tell(mysqlbinlog_file_reader.get_io_cache());
      }
    }

    /**
      Process with Format_description_log_event, if found one.
      Assign to glob_description_event, and transfer the responsibility
      for destroying the event to global_description_event.
    */
    if (ev->get_type_code() == binary_log::FORMAT_DESCRIPTION_EVENT)
    {
      delete glob_description_event;
      glob_description_event= (Format_description_log_event*) ev;
    }
    else
    {
      /* for other event, free it, cause we only interest in the time
         of a event, which has been used before. */
      delete ev;
    }
    ev= NULL; /* mark as deleted */
  }

err:
  retval= ERROR_STOP;

end:
  /* free event if any */
  if (ev)
    delete ev;

   mysqlbinlog_file_reader.close();
#if 0
  if (end_io_cache(file))
    retval= ERROR_STOP;
#endif
  if (fd >= 0)
    my_close(fd, MYF(MY_WME));

  return retval;
}


/**
   GTID cleanup destroys objects and reset their pointer.
   Function is reentrant.
*/
inline void gtid_client_cleanup()
{
  delete global_sid_lock;
  delete global_sid_map;
  delete gtid_set_excluded;
  delete gtid_set_included;
  global_sid_lock= NULL;
  global_sid_map= NULL;
  gtid_set_excluded= NULL;
  gtid_set_included= NULL;
}

/**
   GTID initialization.

   @return true if allocation does not succeed
           false if OK
*/
inline bool gtid_client_init()
{
  bool res=
    (!(global_sid_lock= new Checkable_rwlock) ||
     !(global_sid_map= new Sid_map(global_sid_lock)) ||
     !(gtid_set_excluded= new Gtid_set(global_sid_map)) ||
     !(gtid_set_included= new Gtid_set(global_sid_map)));
  if (res)
  {
    gtid_client_cleanup();
  }
  return res;
}


int main(int argc, char **argv)
{
  Exit_status retval= OK_CONTINUE;

  MY_INIT(argv[0]);
  DBUG_ENTER("main");
  DBUG_PROCESS(argv[0]);

  my_init_time(); // for time functions

  parse_args(&argc, &argv);

  if (!argc)
  {
    usage();
    my_end(0);
    exit(1);
  }

  if (gtid_client_init())
  {
    error("Could not initialize GTID structures.");
    exit(1);
  }

  /* force-append-rotate-event conflicts with other options */
  if (force_append_rotate_event &&
      (truncate_datetime_str != NULL || trim_tail_incomplete_trx ))
  {
    error("--force-append-rotate-event conflicts with --truncate-datetime and "
          "--trim-tail-incomplete-trx.");
    exit(1);
  }
  if (truncate_index_from != UINT64_MAX && truncate_datetime_str != NULL)
  {
    error("--truncate_index_from conflicts with --truncate-datetime.");
    exit(1);
  }
  if (show_index_info &&
      (truncate_datetime_str != NULL || truncate_index_from != UINT64_MAX || trim_tail_incomplete_trx))
  {
    error("--show-index-info conflicts with --truncate-datetime and --truncate-index-from and "
          "--trim-tail-incomplete-trx.");
    exit(1);
  }

  while (--argc >= 0)
  {
    if ((retval= truncate_binlog(*argv++)) != OK_CONTINUE)
      break;
  }
  delete glob_description_event;

  /* We cannot free DBUG, it is used in global destructors after exit(). */
  my_end(MY_DONT_FREE_DBUG);

  gtid_client_cleanup();

  exit(retval == ERROR_STOP ? 1 : 0);
  /* Keep compilers happy. */
  DBUG_RETURN(retval == ERROR_STOP ? 1 : 0);
}

#ifndef MYSQL_SERVER
/*
  Mysqlbinlogtailer should not use the function print.
  Following code will override the virtual function ::print and
  do some defense.
*/
void Transaction_payload_log_event::print(FILE *,
                                          PRINT_EVENT_INFO *info) const {
  DBUG_TRACE;

  IO_CACHE *const head = &info->head_cache;
  if (!info->short_form) {
    std::ostringstream oss;
    oss << "\tTransaction_Payload\t" << to_string() << std::endl;
    oss << "# Start of compressed events!" << std::endl;
    print_header(head, info, false);
    my_b_printf(head, "%s", oss.str().c_str());

    my_b_printf(head, "# End of compressed events!\n");
  }
}
#endif