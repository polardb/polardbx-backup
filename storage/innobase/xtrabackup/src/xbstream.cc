/******************************************************
Copyright (c) 2011-2020 Percona LLC and/or its affiliates.

The xbstream utility: serialize/deserialize files in the XBSTREAM format.

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

#include "xbstream.h"
#include <gcrypt.h>
#include <my_base.h>
#include <my_getopt.h>
#include <my_thread.h>
#include <mysql/service_mysql_alloc.h>
#include <mysql_version.h>
#include <pthread.h>
#include <typelib.h>
#include <unordered_map>
#include "common.h"
#include "crc_glue.h"
#include "datasink.h"
#include "ds_decompress.h"
#include "ds_decompress_lz4.h"
#include "ds_decrypt.h"
#include "template_utils.h"
#include "xbcrypt_common.h"
#include "xtrabackup_version.h"
#include <libgen.h>
#include <dirent.h>

#define XBSTREAM_VERSION XTRABACKUP_VERSION
#define XBSTREAM_BUFFER_SIZE (10 * 1024 * 1024UL)

typedef enum {
  RUN_MODE_NONE,
  RUN_MODE_CREATE,
  RUN_MODE_EXTRACT,
  RUN_MODE_PRINT
} run_mode_t;

const char *xbstream_encrypt_algo_names[] = {"NONE", "AES128", "AES192",
                                             "AES256", NullS};
TYPELIB xbstream_encrypt_algo_typelib = {
    array_elements(xbstream_encrypt_algo_names) - 1, "",
    xbstream_encrypt_algo_names, NULL};

/* Following definitions are to avoid linking with unused datasinks
   and their link dependencies */
datasink_t datasink_archive;
datasink_t datasink_xbstream;
datasink_t datasink_compress;
datasink_t datasink_compress_lz4;
datasink_t datasink_tmpfile;
datasink_t datasink_encrypt;

static run_mode_t opt_mode;
static char *opt_directory = NULL;
static bool opt_verbose = 0;
static int opt_parallel = 1;
static ulong opt_encrypt_algo;
static char *opt_encrypt_key_file = NULL;
static char *opt_encrypt_key = NULL;
static int opt_encrypt_threads = 1;
static bool opt_decompress = 0;
static uint opt_decompress_threads = 1;
static char *opt_input_file = NULL;
static int input_file_fd = -1;
static ulonglong opt_start_position = 0;
static ulonglong opt_stop_position = ULLONG_MAX;
static bool opt_enable_partial = 0;

enum {
  OPT_DECOMPRESS = 256,
  OPT_DECOMPRESS_THREADS,
  OPT_ENCRYPT_THREADS,
  OPT_INPUT_FILE,
  OPT_START_POSITION,
  OPT_STOP_POSITION,
  OPT_ENABLE_PARTIAL,
  OPT_PRINT_INFO
};

static struct my_option my_long_options[] = {
    {"help", '?', "Display this help and exit.", 0, 0, 0, GET_NO_ARG, NO_ARG, 0,
     0, 0, 0, 0, 0},
    {"create", 'c', "Stream the specified files to the standard output.", 0, 0,
     0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
    {"decompress", OPT_DECOMPRESS, "Decompress individual backup files.",
     &opt_decompress, &opt_decompress, 0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
    {"decompress-threads", OPT_DECOMPRESS_THREADS,
     "Number of threads for parallel data decompression. The default value is "
     "1.",
     &opt_decompress_threads, &opt_decompress_threads, 0, GET_UINT,
     REQUIRED_ARG, 1, 1, UINT_MAX, 0, 0, 0},
    {"extract", 'x',
     "Extract to disk files from the stream on the "
     "standard input.",
     0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
    {"directory", 'C',
     "Change the current directory to the specified one "
     "before streaming or extracting.",
     &opt_directory, &opt_directory, 0, GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0,
     0, 0},
    {"verbose", 'v', "Print verbose output.", &opt_verbose, &opt_verbose, 0,
     GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
    {"parallel", 'p', "Number of worker threads for reading / writing.",
     &opt_parallel, &opt_parallel, 0, GET_INT, REQUIRED_ARG, 1, 1, INT_MAX, 0,
     0, 0},
    {"decrypt", 'd', "Decrypt files ending with .xbcrypt.", &opt_encrypt_algo,
     &opt_encrypt_algo, &xbstream_encrypt_algo_typelib, GET_ENUM, REQUIRED_ARG,
     0, 0, 0, 0, 0, 0},
    {"encrypt-key", 'k', "Encryption key", 0, 0, 0, GET_STR_ALLOC, REQUIRED_ARG,
     0, 0, 0, 0, 0, 0},
    {"encrypt-key-file", 'f', "File which contains encryption key.",
     &opt_encrypt_key_file, &opt_encrypt_key_file, 0, GET_STR_ALLOC,
     REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
    {"encrypt-threads", OPT_ENCRYPT_THREADS,
     "Number of threads for parallel data encryption. "
     "The default value is 1.",
     &opt_encrypt_threads, &opt_encrypt_threads, 0, GET_INT, REQUIRED_ARG, 1, 1,
     INT_MAX, 0, 0, 0},
    {"input-file", OPT_INPUT_FILE,
     "The input xbstream file to be extracted.",
     &opt_input_file, &opt_input_file, 0,
     GET_STR_ALLOC, REQUIRED_ARG, 0, 0, 0, 0, 0, 0},
    {"start-position", OPT_START_POSITION,
     "Start reading input xbstream file at this position.",
     &opt_start_position, &opt_start_position, 0,
     GET_ULL, REQUIRED_ARG, 0, 0, ULLONG_MAX, 0, 0, 0},
    {"stop-position", OPT_STOP_POSITION,
     "Stop reading input xbstream file at this position.",
     &opt_stop_position, &opt_stop_position, 0,
     GET_ULL, REQUIRED_ARG, LLONG_MAX, 0, ULLONG_MAX, 0, 0, 0},
    {"partial", OPT_ENABLE_PARTIAL,
     "Enable partial extracting, which will skip incomplete enctry file.",
     &opt_enable_partial, &opt_enable_partial,
     0, GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
    {"print-info", OPT_PRINT_INFO, "Print chunk information.",
     0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},

    {0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}};

typedef struct {
  char *path;
  uint pathlen;
  my_off_t offset;
  ds_file_t *file;
  pthread_mutex_t mutex;
  bool seen_begin;
  bool seen_end;
} file_entry_t;

typedef std::unordered_map<std::string, file_entry_t *> map_file;

typedef struct {
  map_file filehash;
  xb_rstream_t *stream;
  ds_ctxt_t *ds_ctxt;
  ds_ctxt_t *ds_decompress_quicklz_ctxt;
  ds_ctxt_t *ds_decompress_lz4_ctxt;
  ds_ctxt_t *ds_decrypt_quicklz_ctxt;
  ds_ctxt_t *ds_decrypt_lz4_ctxt;
  ds_ctxt_t *ds_decrypt_uncompressed_ctxt;
  pthread_mutex_t *mutex;
} extract_ctxt_t;

static int get_options(int *argc, char ***argv);
static int mode_create(int argc, char **argv);
static int mode_extract(int n_threads, int argc, char **argv);
static int mode_print(int argc, char **argv);
static bool get_one_option(int optid, const struct my_option *opt,
                           char *argument);

int main(int argc, char **argv) {
  MY_INIT(argv[0]);

  if (get_options(&argc, &argv)) {
    goto err;
  }

  crc_init();

  if (opt_mode == RUN_MODE_NONE) {
    msg("%s: either -c or -x must be specified.\n", my_progname);
    goto err;
  }

  if (opt_input_file) {

    input_file_fd = my_open(opt_input_file, O_RDONLY, MYF(MY_WME));
    if (input_file_fd < 0) {
      msg("%s: failed to open input file '%s'.\n", my_progname, opt_input_file);
      goto err;
    }
  }

  if (opt_start_position > opt_stop_position) {

    msg("%s: start-position (%llu) must be less than stop-position (%llu).\n",
        my_progname, opt_start_position, opt_stop_position);
    goto err;
  }

  /* Change the current directory if -C is specified */
  if (opt_directory && my_setwd(opt_directory, MYF(MY_WME))) {
    goto err;
  }

  if (opt_encrypt_algo || opt_encrypt_key) {
    xb_libgcrypt_init();
  }

  if (opt_mode == RUN_MODE_CREATE && mode_create(argc, argv)) {
    goto err;
  } else if (opt_mode == RUN_MODE_EXTRACT &&
             mode_extract(opt_parallel, argc, argv)) {
    goto err;
  } else if (opt_mode == RUN_MODE_PRINT && mode_print(argc, argv)) {
    goto err;
  }

  my_cleanup_options(my_long_options);

  if (input_file_fd >= 0) {
    my_close(input_file_fd, MYF(0));
  }

  my_end(0);

  return EXIT_SUCCESS;
err:
  my_cleanup_options(my_long_options);

  my_end(0);

  exit(EXIT_FAILURE);
}

static int get_options(int *argc, char ***argv) {
  int ho_error;

  if ((ho_error =
           handle_options(argc, argv, my_long_options, get_one_option))) {
    exit(EXIT_FAILURE);
  }

  return 0;
}

static void print_version(void) {
  printf("%s  Ver %s for %s (%s)\n", my_progname, XBSTREAM_VERSION, SYSTEM_TYPE,
         MACHINE_TYPE);
}

static void usage(void) {
  print_version();
  puts("Copyright (C) 2011-2018 Percona LLC and/or its affiliates.");
  puts(
      "This software comes with ABSOLUTELY NO WARRANTY. "
      "This is free software,\nand you are welcome to modify and "
      "redistribute it under the GPL license.\n");

  puts("Serialize/deserialize files in the XBSTREAM format.\n");

  puts("Usage: ");
  printf(
      "  %s -c [OPTIONS...] FILES...	# stream specified files to "
      "standard output.\n",
      my_progname);
  printf(
      "  %s -x [OPTIONS...]		# extract files from the stream"
      "on the standard input.\n",
      my_progname);

  puts("\nOptions:");
  my_print_help(my_long_options);
}

static int set_run_mode(run_mode_t mode) {
  if (opt_mode != RUN_MODE_NONE) {
    msg("%s: can't set specify both -c and -x.\n", my_progname);
    return 1;
  }

  opt_mode = mode;

  return 0;
}

static bool get_one_option(int optid,
                           const struct my_option *opt __attribute__((unused)),
                           char *argument __attribute__((unused))) {
  switch (optid) {
    case 'c':
      if (set_run_mode(RUN_MODE_CREATE)) {
        return true;
      }
      break;
    case 'x':
      if (set_run_mode(RUN_MODE_EXTRACT)) {
        return true;
      }
      break;
    case OPT_PRINT_INFO:
      if (set_run_mode(RUN_MODE_PRINT)) {
        return true;
      }
	  break;
    case 'k':
      hide_option(argument, &opt_encrypt_key);
      break;
    case '?':
      usage();
      exit(0);
  }

  return false;
}

static int stream_one_file(File file, xb_wstream_file_t *xbfile) {
  uchar *buf;
  size_t bytes;
  size_t offset;

  posix_fadvise(file, 0, 0, POSIX_FADV_SEQUENTIAL);
  offset = my_tell(file, MYF(MY_WME));

  buf = (uchar *)(my_malloc(PSI_NOT_INSTRUMENTED, XBSTREAM_BUFFER_SIZE,
                            MYF(MY_FAE)));

  while ((bytes = my_read(file, buf, XBSTREAM_BUFFER_SIZE, MYF(MY_WME))) > 0) {
    if (xb_stream_write_data(xbfile, buf, bytes)) {
      msg("%s: xb_stream_write_data() failed.\n", my_progname);
      my_free(buf);
      return 1;
    }
    posix_fadvise(file, offset, XBSTREAM_BUFFER_SIZE, POSIX_FADV_DONTNEED);
    offset += XBSTREAM_BUFFER_SIZE;
  }

  my_free(buf);

  if (bytes == (size_t)-1) {
    return 1;
  }

  return 0;
}

static int mode_create(int argc, char **argv) {
  int i;
  MY_STAT mystat;
  xb_wstream_t *stream;

  if (argc < 1) {
    msg("%s: no files are specified.\n", my_progname);
    return 1;
  }

  stream = xb_stream_write_new();
  if (stream == NULL) {
    msg("%s: xb_stream_write_new() failed.\n", my_progname);
    return 1;
  }

  for (i = 0; i < argc; i++) {
    char *filepath = argv[i];
    File src_file;
    xb_wstream_file_t *file;

    if (my_stat(filepath, &mystat, MYF(MY_WME)) == NULL) {
      goto err;
    }
    if (!MY_S_ISREG(mystat.st_mode)) {
      msg("%s: %s is not a regular file, exiting.\n", my_progname, filepath);
      goto err;
    }

    if ((src_file = my_open(filepath, O_RDONLY, MYF(MY_WME))) < 0) {
      msg("%s: failed to open %s.\n", my_progname, filepath);
      goto err;
    }

    file = xb_stream_write_open(stream, filepath, &mystat, NULL, NULL);
    if (file == NULL) {
      goto err;
    }

    if (opt_verbose) {
      msg("%s\n", filepath);
    }

    if (stream_one_file(src_file, file) || xb_stream_write_close(file) ||
        my_close(src_file, MYF(MY_WME))) {
      goto err;
    }
  }

  xb_stream_write_done(stream);

  return 0;
err:
  xb_stream_write_done(stream);

  return 1;
}

/************************************************************************
Check if string ends with given suffix.
@return true if string ends with given suffix. */
static bool ends_with(const char *str, const char *suffix) {
  size_t suffix_len = strlen(suffix);
  size_t str_len = strlen(str);
  return (str_len >= suffix_len &&
          strcmp(str + str_len - suffix_len, suffix) == 0);
}

static file_entry_t *file_entry_new(extract_ctxt_t *ctxt, const char *path,
                                    uint pathlen) {
  file_entry_t *entry;
  ds_file_t *file;

  entry = (file_entry_t *)my_malloc(PSI_NOT_INSTRUMENTED, sizeof(file_entry_t),
                                    MYF(MY_WME | MY_ZEROFILL));
  if (entry == NULL) {
    return NULL;
  }

  entry->path = my_strndup(PSI_NOT_INSTRUMENTED, path, pathlen, MYF(MY_WME));
  if (entry->path == NULL) {
    goto err;
  }
  entry->pathlen = pathlen;

  if (ctxt->ds_decrypt_quicklz_ctxt && ends_with(path, ".qp.xbcrypt")) {
    file = ds_open(ctxt->ds_decrypt_quicklz_ctxt, path, NULL);
  } else if (ctxt->ds_decrypt_lz4_ctxt && ends_with(path, ".lz4.xbcrypt")) {
    file = ds_open(ctxt->ds_decrypt_lz4_ctxt, path, NULL);
  } else if (ctxt->ds_decrypt_uncompressed_ctxt &&
             ends_with(path, ".xbcrypt")) {
    file = ds_open(ctxt->ds_decrypt_uncompressed_ctxt, path, NULL);
  } else if (ctxt->ds_decompress_quicklz_ctxt && ends_with(path, ".qp")) {
    file = ds_open(ctxt->ds_decompress_quicklz_ctxt, path, NULL);
  } else if (ctxt->ds_decompress_lz4_ctxt && ends_with(path, ".lz4")) {
    file = ds_open(ctxt->ds_decompress_lz4_ctxt, path, NULL);
  } else {
    file = ds_open(ctxt->ds_ctxt, path, NULL);
  }
  if (file == NULL) {
    msg("%s: failed to create file.\n", my_progname);
    goto err;
  }

  if (opt_verbose) {
    msg("%s\n", entry->path);
  }

  entry->file = file;

  pthread_mutex_init(&entry->mutex, NULL);

  return entry;

err:
  if (entry->path != NULL) {
    my_free(entry->path);
  }
  my_free(entry);

  return NULL;
}

static void remove_dir_if_empty(const char* dir_name) {
  DIR *dir;
  long file_count = 0;

  dir = opendir(dir_name);

  while ((readdir(dir)) != NULL)  file_count++;
  closedir(dir);

  /* This is empty dir */
  if (file_count <= 2) {

    if (opt_verbose) {
      msg("Removing empty directory '%s'\n", dir_name);
    }
    rmdir(dir_name);
  }
}

/**
   Remove incomplete file entry.

   Also remove directory if it's empty after file entry deleted.
   FIXME It's is a bit tricky to remove directly, it's better to cleanup
   empty directory inside datasink deinitialization.
 */
static void remove_incomplete_entry(file_entry_t *entry) {
  char dir_name[FN_REFLEN];

  if (opt_verbose) {
    msg("Removing incomplete file '%s'\n", entry->path);
  }
  ds_delete(entry->file);

  strcpy(dir_name, dirname(entry->path));

  /* Do not remove current dir */
  if (strcmp(dir_name, ".")) {
    remove_dir_if_empty(dir_name);
  }
}

static void file_entry_free(file_entry_t *entry) {
  pthread_mutex_destroy(&entry->mutex);
  /* This file entry is incomplete */
  if (entry->file != NULL) {

    if (opt_enable_partial) {

      assert(!entry->seen_begin || !entry->seen_end);
      remove_incomplete_entry(entry);
    } else {

      ds_close(entry->file);
    }
  }

  my_free(entry->path);
  my_free(entry);
}

static void hash_free(map_file *filehash) {
  for (auto element : *filehash) {
    file_entry_free(element.second);
  }
  filehash->clear();
}

static void *extract_worker_thread_func(void *arg) {
  xb_rstream_chunk_t chunk;
  file_entry_t *entry;
  xb_rstream_result_t res;

  extract_ctxt_t *ctxt = (extract_ctxt_t *)arg;

  my_thread_init();

  memset(&chunk, 0, sizeof(chunk));

  while (1) {
    pthread_mutex_lock(ctxt->mutex);
    res = xb_stream_read_chunk(ctxt->stream, &chunk);

    if (res != XB_STREAM_READ_CHUNK) {
      pthread_mutex_unlock(ctxt->mutex);
      break;
    }

    /* If unknown type and ignorable flag is set, skip this chunk */
    if (chunk.type == XB_CHUNK_TYPE_UNKNOWN &&
        !(chunk.flags & XB_STREAM_FLAG_IGNORABLE)) {
      pthread_mutex_unlock(ctxt->mutex);
      continue;
    }

    /* See if we already have this file open */
    entry = ctxt->filehash[chunk.path];

    if (entry == NULL) {
      entry = file_entry_new(ctxt, chunk.path, chunk.pathlen);
      if (entry == NULL) {
        res = XB_STREAM_READ_ERROR;
        pthread_mutex_unlock(ctxt->mutex);
        break;
      }
      ctxt->filehash[chunk.path] = entry;
    }

    pthread_mutex_lock(&entry->mutex);

    pthread_mutex_unlock(ctxt->mutex);

    if (chunk.type == XB_CHUNK_TYPE_PAYLOAD ||
        chunk.type == XB_CHUNK_TYPE_SPARSE) {
      res = xb_stream_validate_checksum(&chunk);
    }

    if (res != XB_STREAM_READ_CHUNK) {
      pthread_mutex_unlock(&entry->mutex);
      break;
    }

    /* Check whether this is the first chunk of one file */
    if (!entry->seen_begin && chunk.offset == 0) {
      entry->seen_begin = true;
    }

    if (chunk.type == XB_CHUNK_TYPE_EOF) {

      /* This is the end chunk of one file entry */
      entry->seen_end = true;

      /* But we have not seen the begin chunk, so this file
      entry is incomplete. We keep it in the HASH, and it wiil
      be cleaned properly in hash_free */
      if (!entry->seen_begin) {

        pthread_mutex_unlock(&entry->mutex);
        continue;
      }

      /* Close complete entry and remove it from HASH */
      ds_close(entry->file);
      entry->file = NULL;
      pthread_mutex_lock(ctxt->mutex);
      pthread_mutex_unlock(&entry->mutex);
      ctxt->filehash.erase(entry->path);
      file_entry_free(entry);
      pthread_mutex_unlock(ctxt->mutex);

      continue;
    }

    if (entry->offset != chunk.offset) {
      /* skip file entry without begin chunk in partial mode */
      if (opt_enable_partial && !entry->seen_begin) {
        entry->offset += chunk.length;
        pthread_mutex_unlock(&entry->mutex);
        continue;
      }

      msg("%s: out-of-order chunk: real offset = 0x%llx, "
          "expected offset = 0x%llx\n",
          my_progname, chunk.offset, entry->offset);
      pthread_mutex_unlock(&entry->mutex);
      res = XB_STREAM_READ_ERROR;
      break;
    }

    if (chunk.type == XB_CHUNK_TYPE_PAYLOAD) {
      if (ds_write(entry->file, chunk.data, chunk.length)) {
        msg("%s: my_write() failed.\n", my_progname);
        pthread_mutex_unlock(&entry->mutex);
        res = XB_STREAM_READ_ERROR;
        break;
      }

      entry->offset += chunk.length;
    } else if (chunk.type == XB_CHUNK_TYPE_SPARSE) {
      if (ds_write_sparse(entry->file, chunk.data, chunk.length,
                          chunk.sparse_map_size, chunk.sparse_map)) {
        msg("%s: my_write() failed.\n", my_progname);
        pthread_mutex_unlock(&entry->mutex);
        res = XB_STREAM_READ_ERROR;
        break;
      }

      for (size_t i = 0; i < chunk.sparse_map_size; ++i)
        entry->offset += chunk.sparse_map[i].skip;
      entry->offset += chunk.length;
    }

    pthread_mutex_unlock(&entry->mutex);
  }

  my_free(chunk.raw_data);
  my_free(chunk.sparse_map);

  my_thread_end();

  return (void *)(res);
}

static int mode_extract(int n_threads, int argc __attribute__((unused)),
                        char **argv __attribute__((unused))) {
  xb_rstream_t *stream = NULL;
  ds_ctxt_t *ds_ctxt = NULL;
  ds_ctxt_t *ds_decrypt_lz4_ctxt = NULL;
  ds_ctxt_t *ds_decrypt_quicklz_ctxt = NULL;
  ds_ctxt_t *ds_decrypt_uncompressed_ctxt = NULL;
  ds_ctxt_t *ds_decompress_quicklz_ctxt = NULL;
  ds_ctxt_t *ds_decompress_lz4_ctxt = NULL;
  extract_ctxt_t ctxt;
  int i;
  pthread_t *tids = NULL;
  void **retvals = NULL;
  pthread_mutex_t mutex;
  int ret = 0;

  if (pthread_mutex_init(&mutex, NULL)) {
    msg("%s: failed to initialize mutex.\n", my_progname);
    return 1;
  }

  /* If --directory is specified, it is already set as CWD by now. */
  ds_ctxt = ds_create(".", DS_TYPE_LOCAL);
  if (ds_ctxt == NULL) {
    ret = 1;
    goto exit;
  }

  if (opt_decompress) {
    ds_decompress_quicklz_threads = opt_decompress_threads;
    ds_decompress_quicklz_ctxt = ds_create(".", DS_TYPE_DECOMPRESS_QUICKLZ);
    if (ds_decompress_quicklz_ctxt == NULL) {
      ret = 1;
      goto exit;
    }
    ds_set_pipe(ds_decompress_quicklz_ctxt, ds_ctxt);

    ds_decompress_lz4_threads = opt_decompress_threads;
    ds_decompress_lz4_ctxt = ds_create(".", DS_TYPE_DECOMPRESS_LZ4);
    if (ds_decompress_lz4_ctxt == NULL) {
      ret = 1;
      goto exit;
    }
    ds_set_pipe(ds_decompress_lz4_ctxt, ds_ctxt);
  }

  if (opt_encrypt_algo) {
    ds_encrypt_algo = opt_encrypt_algo;
    ds_encrypt_key = opt_encrypt_key;
    ds_encrypt_key_file = opt_encrypt_key_file;
    ds_decrypt_encrypt_threads = opt_encrypt_threads;
    ds_decrypt_uncompressed_ctxt = ds_create(".", DS_TYPE_DECRYPT);
    ds_set_pipe(ds_decrypt_uncompressed_ctxt, ds_ctxt);
    if (ds_decrypt_uncompressed_ctxt == NULL) {
      ret = 1;
      goto exit;
    }
    if (ds_decompress_quicklz_ctxt) {
      ds_decrypt_quicklz_ctxt = ds_create(".", DS_TYPE_DECRYPT);
      ds_set_pipe(ds_decrypt_quicklz_ctxt, ds_decompress_quicklz_ctxt);
    }
    if (ds_decompress_lz4_ctxt) {
      ds_decrypt_lz4_ctxt = ds_create(".", DS_TYPE_DECRYPT);
      ds_set_pipe(ds_decrypt_lz4_ctxt, ds_decompress_lz4_ctxt);
    }
  }

  if (input_file_fd > 0) {
    stream = xb_stream_read_fd_new(input_file_fd, opt_start_position,
                                   opt_stop_position);
  } else {
    stream = xb_stream_read_new();
  }

  if (stream == NULL) {
    msg("%s: xb_stream_read_new() failed.\n", my_progname);
    pthread_mutex_destroy(&mutex);
    ret = 1;
    goto exit;
  }

  ctxt.stream = stream;
  ctxt.ds_ctxt = ds_ctxt;
  ctxt.ds_decompress_quicklz_ctxt = ds_decompress_quicklz_ctxt;
  ctxt.ds_decompress_lz4_ctxt = ds_decompress_lz4_ctxt;
  ctxt.ds_decrypt_uncompressed_ctxt = ds_decrypt_uncompressed_ctxt;
  ctxt.ds_decrypt_quicklz_ctxt = ds_decrypt_quicklz_ctxt;
  ctxt.ds_decrypt_lz4_ctxt = ds_decrypt_lz4_ctxt;
  ctxt.mutex = &mutex;

  tids = new pthread_t[n_threads];
  retvals = new void *[n_threads];

  for (i = 0; i < n_threads; i++)
    pthread_create(tids + i, NULL, extract_worker_thread_func, &ctxt);

  for (i = 0; i < n_threads; i++) pthread_join(tids[i], retvals + i);

  for (i = 0; i < n_threads; i++) {
    if ((ulong)retvals[i] == XB_STREAM_READ_ERROR) {
      ret = 1;
      goto exit;
    }
  }

exit:
  pthread_mutex_destroy(&mutex);

  delete[] tids;
  delete[] retvals;

  hash_free(&ctxt.filehash);

  if (ds_ctxt != NULL) {
    ds_destroy(ds_ctxt);
  }
  if (ds_decrypt_uncompressed_ctxt != NULL) {
    ds_destroy(ds_decrypt_uncompressed_ctxt);
  }
  if (ds_decrypt_lz4_ctxt != NULL) {
    ds_destroy(ds_decrypt_lz4_ctxt);
  }
  if (ds_decrypt_quicklz_ctxt != NULL) {
    ds_destroy(ds_decrypt_quicklz_ctxt);
  }
  if (ds_decompress_lz4_ctxt != NULL) {
    ds_destroy(ds_decompress_lz4_ctxt);
  }
  if (ds_decompress_quicklz_ctxt != NULL) {
    ds_destroy(ds_decompress_quicklz_ctxt);
  }
  xb_stream_read_done(stream);

  if (ret) {
    msg("exit code: %d\n", ret);
  }

  return ret;
}

static const char *get_chunk_type_name(xb_chunk_type_t type) {
  if (type == XB_CHUNK_TYPE_UNKNOWN) {

    return "UNKNOWN";
  } else if (type == XB_CHUNK_TYPE_PAYLOAD) {

    return "PAYLOAD";
  } else if (type == XB_CHUNK_TYPE_EOF) {

    return "EOF    ";
  } else {
    /* Make gcc happy */
    return "IMPOSSIBLE";
  }
}

/**
   Chunk format

   Name      Len      Desc
   ================================
   Magic     8        "XBSTCK01"
   Flag      1        Currently only this XB_STREAM_FLAG_IGNORABLE
   Type      1        Payload or EOF
   Path Len  4        Length of following path string
   Path    <Path Len> Path string
   ===== EOF Chunk end here  =====
   Length    8        Payload length
   Offset    8        Payload offset in file
   Checksum  4        Payload checksum
   Payload <Legnth>   Payload data
*/
static void print_chunk_info(my_off_t chunk_begin,  my_off_t chunk_end,
                             xb_rstream_chunk_t *chunk)
{
  char buff[1024];
  char *ptr = buff;
  char *end = buff + sizeof(buff);

  /* flag */
  ptr += snprintf(ptr, end - ptr, "%02X ", chunk->flags);
  /* type */
  ptr += snprintf(ptr, end - ptr, "%s ", get_chunk_type_name(chunk->type));
  /* path */
  ptr += snprintf(ptr, end - ptr, "%s ", chunk->path);

  /* offset in stream */
  ptr += snprintf(ptr, end - ptr, "%llu %llu ", (ulonglong)chunk_begin,
                 (ulonglong)chunk_end);

  /* Extra data length caused by header */
  ptr += snprintf(ptr, end - ptr, "%u ",
         8 + 1 + 1 + 4 + chunk->pathlen + 8 + 8 + 4);

  /* offset and length */
  if (chunk->type != XB_CHUNK_TYPE_EOF) {
    ptr += snprintf(ptr, end - ptr, "%llu %lu ",
                    (ulonglong)chunk->offset, (ulong)chunk->length);

    /* checksum is not intersting to use */
    /* ptr += snprintf(ptr, end - ptr, "%llu",
       chunk->checksum); */
  }

  /* flag */

  *ptr = '\0';

  msg("%s\n", buff);
}

static int mode_print(int argc __attribute__((unused)),
                      char **argv __attribute__((unused))) {
  xb_rstream_result_t res = XB_STREAM_READ_CHUNK;
  xb_rstream_chunk_t chunk;
  xb_rstream_t *stream = NULL;
  int ret = 0;
  my_off_t chunk_begin_pos;
  my_off_t chunk_end_pos;

  memset(&chunk, 0, sizeof(chunk));

  if (input_file_fd > 0) {
    stream = xb_stream_read_fd_new(input_file_fd,
                 opt_start_position,
                 opt_stop_position);
  } else {
    stream = xb_stream_read_new();
  }

  chunk_begin_pos = chunk_end_pos = xb_stream_read_offset(stream);

  while (1) {

    chunk_begin_pos = xb_stream_read_offset(stream);
    assert(chunk_begin_pos == chunk_end_pos);

    res = xb_stream_read_chunk(stream, &chunk);

    chunk_end_pos = xb_stream_read_offset(stream);

    if (res != XB_STREAM_READ_CHUNK) {
      break;
    }

    print_chunk_info(chunk_begin_pos, chunk_end_pos, &chunk);

  }

  xb_stream_read_done(stream);
  return ret;
}
