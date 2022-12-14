/******************************************************
Copyright (c) 2013 Percona LLC and/or its affiliates.

Local datasink implementation for XtraBackup.

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

#include <my_base.h>
#include <mysql/service_mysql_alloc.h>
#include <mysys_err.h>
#include "common.h"
#include "datasink.h"

typedef struct {
  pthread_mutex_t mutex;
  size_t in_size;
  size_t out_size;
} ds_stdout_ctxt_t;

typedef struct {
  File fd;
  ds_stdout_ctxt_t *stdout_ctxt;
} ds_stdout_file_t;

static ds_ctxt_t *stdout_init(const char *root);
static ds_file_t *stdout_open(ds_ctxt_t *ctxt, const char *path,
                              MY_STAT *mystat);
static int stdout_write(ds_file_t *file, const void *buf, size_t len);
static int stdout_close(ds_file_t *file);
static int stdout_delete(ds_file_t *file);
static void stdout_cleanup(ds_ctxt_t *ctxt);
static void stdout_deinit(ds_ctxt_t *ctxt);
static void stdout_size(ds_ctxt_t *ctxt, size_t *in_size, size_t *out_size);

datasink_t datasink_stdout = {&stdout_init, &stdout_open,  &stdout_write,
                              nullptr,      &stdout_close, &stdout_delete,
                              &stdout_cleanup, &stdout_deinit, &stdout_size};

static ds_ctxt_t *stdout_init(const char *root) {
  ds_ctxt_t *ctxt;
  ds_stdout_ctxt_t *stdout_ctxt;

  ctxt = static_cast<ds_ctxt_t *>(
    my_malloc(PSI_NOT_INSTRUMENTED,
              sizeof(ds_ctxt_t) + sizeof(ds_stdout_ctxt_t), MYF(MY_FAE)));

  stdout_ctxt = (ds_stdout_ctxt_t*) (ctxt + 1);

  if (pthread_mutex_init(&stdout_ctxt->mutex, NULL)) {
    msg("stdout_init: pthread_mutex_init() failed.\n");
    goto err;
  }
  stdout_ctxt->in_size = 0;
  stdout_ctxt->out_size = 0;

  ctxt->root = my_strdup(PSI_NOT_INSTRUMENTED, root, MYF(MY_FAE));
  ctxt->ptr = stdout_ctxt;

  return ctxt;
err:
  my_free(ctxt);
  return NULL;
}

static ds_file_t *stdout_open(ds_ctxt_t *ctxt __attribute__((unused)),
                              const char *path __attribute__((unused)),
                              MY_STAT *mystat __attribute__((unused))) {
  ds_stdout_file_t *stdout_file;
  ds_file_t *file;
  size_t pathlen;
  const char *fullpath = "<STDOUT>";

  pathlen = strlen(fullpath) + 1;

  file = (ds_file_t *)my_malloc(
      PSI_NOT_INSTRUMENTED,
      sizeof(ds_file_t) + sizeof(ds_stdout_file_t) + pathlen, MYF(MY_FAE));
  stdout_file = (ds_stdout_file_t *)(file + 1);

#ifdef __WIN__
  setmode(fileno(stdout), _O_BINARY);
#endif

  stdout_file->fd = fileno(stdout);
  stdout_file->stdout_ctxt = (ds_stdout_ctxt_t*) ctxt->ptr;

  file->path = (char *)stdout_file + sizeof(ds_stdout_file_t);
  memcpy(file->path, fullpath, pathlen);

  file->ptr = stdout_file;

  return file;
}

static int stdout_write(ds_file_t *file, const void *buf, size_t len) {
  File fd = ((ds_stdout_file_t *)file->ptr)->fd;
  ds_stdout_ctxt_t *stdout_ctxt =
      ((ds_stdout_file_t *) file->ptr)->stdout_ctxt;

  if (!my_write(fd, static_cast<const uchar *>(buf), len,
                MYF(MY_WME | MY_NABP))) {
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    /* increment datasink size counter */
    pthread_mutex_lock(&stdout_ctxt->mutex);
    stdout_ctxt->in_size += len;
    stdout_ctxt->out_size += len;
    pthread_mutex_unlock(&stdout_ctxt->mutex);

    return 0;
  }

  return 1;
}

static int stdout_close(ds_file_t *file) {
  my_free(file);

  return 0;
}

static int stdout_delete(ds_file_t *file __attribute__((unused))) {
  msg("Error: stdout datasink don't support delete operation.\n");
  return 1;
}

static void stdout_cleanup(ds_ctxt_t *ctxt __attribute__((unused))) {
  return;
}

static void stdout_deinit(ds_ctxt_t *ctxt) {
  ds_stdout_ctxt_t *stdout_ctxt = (ds_stdout_ctxt_t *) ctxt->ptr;

  pthread_mutex_destroy(&stdout_ctxt->mutex);

  my_free(ctxt->root);
  my_free(ctxt);
}

static void stdout_size(ds_ctxt_t *ctxt, size_t *in_size, size_t *out_size) {
  ds_stdout_ctxt_t *stdout_ctxt = (ds_stdout_ctxt_t *) ctxt->ptr;

  pthread_mutex_lock(&stdout_ctxt->mutex);
  if (in_size) {
    *in_size = stdout_ctxt->in_size;
  }
  if (out_size) {
    *out_size = stdout_ctxt->out_size;
  }
  pthread_mutex_unlock(&stdout_ctxt->mutex);
}
