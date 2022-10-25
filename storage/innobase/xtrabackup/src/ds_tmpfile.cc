/******************************************************
Copyright (c) 2012 Percona LLC and/or its affiliates.

tmpfile datasink for XtraBackup.

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

/* Do all writes to temporary files first, then pipe them to the specified
datasink in a serialized way in deinit(). */

#include <my_base.h>
#include <my_dir.h>
#include <my_io.h>
#include <my_list.h>
#include <my_thread_local.h>
#include <mysql/service_mysql_alloc.h>
#include "common.h"
#include "datasink.h"

typedef struct {
  pthread_mutex_t mutex;
  LIST *file_list;
  size_t in_size;
  size_t out_size;
} ds_tmpfile_ctxt_t;

typedef struct {
  LIST list;
  File fd;
  char *orig_path;
  MY_STAT mystat;
  ds_file_t *file;
  ds_tmpfile_ctxt_t *tmpfile_ctxt;
} ds_tmp_file_t;

static ds_ctxt_t *tmpfile_init(const char *root);
static ds_file_t *tmpfile_open(ds_ctxt_t *ctxt, const char *path,
                               MY_STAT *mystat);
static int tmpfile_write(ds_file_t *file, const void *buf, size_t len);
static int tmpfile_close(ds_file_t *file);
static int tmpfile_delete(ds_file_t *file);
static void tmpfile_cleanup(ds_ctxt_t *ctxt);
static void tmpfile_deinit(ds_ctxt_t *ctxt);
static void tmpfile_size(ds_ctxt_t *ctxt, size_t *in_size, size_t *out_size);

void *(* xb_tmp_prepare_info) (const char *filepath) = NULL;
void (* xb_tmp_finish_info) (void *info) = NULL;

datasink_t datasink_tmpfile = {&tmpfile_init, &tmpfile_open,  &tmpfile_write,
                               nullptr,       &tmpfile_close, &tmpfile_delete,
                               &tmpfile_cleanup, &tmpfile_deinit, &tmpfile_size};

extern MY_TMPDIR mysql_tmpdir_list;

static ds_ctxt_t *tmpfile_init(const char *root) {
  ds_ctxt_t *ctxt;
  ds_tmpfile_ctxt_t *tmpfile_ctxt;

  ctxt = static_cast<ds_ctxt_t *>(
      my_malloc(PSI_NOT_INSTRUMENTED,
                sizeof(ds_ctxt_t) + sizeof(ds_tmpfile_ctxt_t), MYF(MY_FAE)));
  tmpfile_ctxt = (ds_tmpfile_ctxt_t *)(ctxt + 1);
  tmpfile_ctxt->file_list = NULL;
  if (pthread_mutex_init(&tmpfile_ctxt->mutex, NULL)) {
    my_free(ctxt);
    return NULL;
  }

  tmpfile_ctxt->in_size = 0;
  tmpfile_ctxt->out_size = 0;

  ctxt->ptr = tmpfile_ctxt;
  ctxt->root = my_strdup(PSI_NOT_INSTRUMENTED, root, MYF(MY_FAE));

  return ctxt;
}

static ds_file_t *tmpfile_open(ds_ctxt_t *ctxt, const char *path,
                               MY_STAT *mystat) {
  ds_tmpfile_ctxt_t *tmpfile_ctxt;
  char tmp_path[FN_REFLEN];
  ds_tmp_file_t *tmp_file;
  ds_file_t *file;
  size_t path_len;
  File fd;

  /* Create a temporary file in tmpdir. The file will be automatically
  removed on close. Code copied from mysql_tmpfile(). */
  fd = create_temp_file(tmp_path, my_tmpdir(&mysql_tmpdir_list), "xbtemp",
#ifdef __WIN__
                        O_BINARY | O_TRUNC | O_SEQUENTIAL | O_TEMPORARY |
                            O_SHORT_LIVED |
#endif /* __WIN__ */
                            O_CREAT | O_EXCL | O_RDWR,
                        UNLINK_FILE, MYF(MY_WME));

#ifndef __WIN__
  if (fd >= 0) {
    /* On Windows, open files cannot be removed, but files can be
    created with the O_TEMPORARY flag to the same effect
    ("delete on close"). */
    unlink(tmp_path);
  }
#endif /* !__WIN__ */

  if (fd < 0) {
    return NULL;
  }

  path_len = strlen(path) + 1; /* terminating '\0' */

  file = (ds_file_t *)my_malloc(
      PSI_NOT_INSTRUMENTED,
      sizeof(ds_file_t) + sizeof(ds_tmp_file_t) + path_len, MYF(MY_FAE));

  tmp_file = (ds_tmp_file_t *)(file + 1);
  tmp_file->file = file;
  memcpy(&tmp_file->mystat, mystat, sizeof(MY_STAT));
  /* Save a copy of 'path', since it may not be accessible later */
  tmp_file->orig_path = (char *)tmp_file + sizeof(ds_tmp_file_t);

  tmp_file->fd = fd;
  memcpy(tmp_file->orig_path, path, path_len);
  tmp_file->tmpfile_ctxt = (ds_tmpfile_ctxt_t*) ctxt->ptr;

  /* Store the real temporary file name in file->path */
  file->path = my_strdup(PSI_NOT_INSTRUMENTED, tmp_path, MYF(MY_FAE));
  file->ptr = tmp_file;

  /* Store the file object in the list to be piped later */
  tmpfile_ctxt = (ds_tmpfile_ctxt_t *)ctxt->ptr;
  tmp_file->list.data = tmp_file;

  pthread_mutex_lock(&tmpfile_ctxt->mutex);
  tmpfile_ctxt->file_list = list_add(tmpfile_ctxt->file_list, &tmp_file->list);
  pthread_mutex_unlock(&tmpfile_ctxt->mutex);

  return file;
}

static int tmpfile_write(ds_file_t *file, const void *buf, size_t len) {
  File fd = ((ds_tmp_file_t *)file->ptr)->fd;
  ds_tmpfile_ctxt_t *tmpfile_ctxt =
      ((ds_tmp_file_t *) file->ptr)->tmpfile_ctxt;

  if (!my_write(fd, static_cast<const uchar *>(buf), len,
                MYF(MY_WME | MY_NABP))) {
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    /* increment datasink size counter */
    pthread_mutex_lock(&tmpfile_ctxt->mutex);
    tmpfile_ctxt->in_size += len;
    pthread_mutex_unlock(&tmpfile_ctxt->mutex);
    return 0;
  }

  return 1;
}

static int tmpfile_close(ds_file_t *file) {
  /* Do nothing -- we will close (and thus remove) the file after piping
  it to the destination datasink in tmpfile_deinit(). */

  my_free(file->path);

  return 0;
}

static int tmpfile_delete(ds_file_t *file) {
  ds_tmp_file_t *tmp_file;
  ds_tmpfile_ctxt_t *tmpfile_ctxt;
  MY_STAT mystat;

  tmp_file = (ds_tmp_file_t *) file->ptr;
  tmpfile_ctxt = tmp_file->tmpfile_ctxt;

  pthread_mutex_lock(&tmpfile_ctxt->mutex);
  tmpfile_ctxt->file_list = list_delete(tmpfile_ctxt->file_list,
             &tmp_file->list);
  pthread_mutex_unlock(&tmpfile_ctxt->mutex);


  if (my_fstat(tmp_file->fd, &mystat)) {
    msg("error: my_fstat() failed.\n");
    exit(EXIT_FAILURE);
  }

  my_close(tmp_file->fd, MYF(MY_WME));

  my_free(file->path);
  my_free(file);
  return 0;
}

static void tmpfile_cleanup(ds_ctxt_t *ctxt) {
  LIST *list;
  ds_tmpfile_ctxt_t *tmpfile_ctxt;
  MY_STAT mystat;
  ds_tmp_file_t *tmp_file;
  ds_file_t *dst_file;
  ds_ctxt_t *pipe_ctxt;
  void *buf = NULL;
  const size_t buf_size = 10 * 1024 * 1024;
  size_t bytes;
  size_t offset;
  void *info;

  pipe_ctxt = ctxt->pipe_ctxt;
  xb_a(pipe_ctxt != NULL);

  buf = my_malloc(PSI_NOT_INSTRUMENTED, buf_size, MYF(MY_FAE));

  tmpfile_ctxt = (ds_tmpfile_ctxt_t *)ctxt->ptr;
  list = tmpfile_ctxt->file_list;

  /* Walk the files in the order they have been added */
  list = list_reverse(list);
  while (list != NULL) {
    tmp_file = static_cast<ds_tmp_file_t *>(list->data);
    /* Stat the file to replace size and mtime on the original
     * mystat struct */
    if (my_fstat(tmp_file->fd, &mystat)) {
      msg("error: my_fstat() failed.\n");
      exit(EXIT_FAILURE);
    }
    tmp_file->mystat.st_size = mystat.st_size;
    tmp_file->mystat.st_mtime = mystat.st_mtime;

    if (xb_tmp_prepare_info) {
      info = xb_tmp_prepare_info(tmp_file->orig_path);
    }

    dst_file = ds_open(pipe_ctxt, tmp_file->orig_path, &tmp_file->mystat);
    if (dst_file == NULL) {
      msg("error: could not stream a temporary file to "
          "'%s'\n",
          tmp_file->orig_path);
      exit(EXIT_FAILURE);
    }

    /* copy to the destination datasink */
    posix_fadvise(tmp_file->fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    if (my_seek(tmp_file->fd, 0, SEEK_SET, MYF(0)) == MY_FILEPOS_ERROR) {
      msg("error: my_seek() failed for '%s', errno = %d.\n",
          tmp_file->file->path, my_errno());
      exit(EXIT_FAILURE);
    }
    offset = 0;
    while ((bytes = my_read(tmp_file->fd, static_cast<uchar *>(buf), buf_size,
                            MYF(MY_WME))) > 0) {
      posix_fadvise(tmp_file->fd, offset, buf_size, POSIX_FADV_DONTNEED);
      offset += buf_size;
      if (ds_write(dst_file, buf, bytes)) {
        msg("error: cannot write to stream for '%s'.\n", tmp_file->orig_path);
        exit(EXIT_FAILURE);
      }
      /* increment datasink size counter */
      pthread_mutex_lock(&tmpfile_ctxt->mutex);
      tmpfile_ctxt->out_size += bytes;
      pthread_mutex_unlock(&tmpfile_ctxt->mutex);
    }
    if (bytes == (size_t)-1) {
      exit(EXIT_FAILURE);
    }

    my_close(tmp_file->fd, MYF(MY_WME));
    ds_close(dst_file);

    if (xb_tmp_finish_info) {
      xb_tmp_finish_info(info);
    }

    list = list_rest(list);
    my_free(tmp_file->file);
  }

  my_free(buf);
}

static void tmpfile_deinit(ds_ctxt_t *ctxt) {
  ds_tmpfile_ctxt_t *tmpfile_ctxt;
  ds_ctxt_t *pipe_ctxt;

  pipe_ctxt = ctxt->pipe_ctxt;
  xb_a(pipe_ctxt != NULL);

  tmpfile_ctxt = (ds_tmpfile_ctxt_t *) ctxt->ptr;

  pthread_mutex_destroy(&tmpfile_ctxt->mutex);

  my_free(ctxt->root);
  my_free(ctxt);
}

static void tmpfile_size(ds_ctxt_t *ctxt, size_t *in_size, size_t *out_size) {
  ds_tmpfile_ctxt_t *tmpfile_ctxt = (ds_tmpfile_ctxt_t *) ctxt->ptr;

  pthread_mutex_lock(&tmpfile_ctxt->mutex);
  if (in_size) {
    *in_size = tmpfile_ctxt->in_size;
  }
  if (out_size) {
    *out_size = tmpfile_ctxt->out_size;
  }
  pthread_mutex_unlock(&tmpfile_ctxt->mutex);
}
