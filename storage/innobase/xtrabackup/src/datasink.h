/******************************************************
Copyright (c) 2011-2013 Percona LLC and/or its affiliates.

Data sink interface.

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

#ifndef XB_DATASINK_H
#define XB_DATASINK_H

#include <my_dir.h>

#ifdef __cplusplus
extern "C" {
#endif

struct datasink_struct;
typedef struct datasink_struct datasink_t;

/* Supported datasink types */
typedef enum {
  DS_TYPE_STDOUT,
  DS_TYPE_LOCAL,
  DS_TYPE_XBSTREAM,
  DS_TYPE_COMPRESS_QUICKLZ,
  DS_TYPE_COMPRESS_LZ4,
  DS_TYPE_DECOMPRESS_QUICKLZ,
  DS_TYPE_DECOMPRESS_LZ4,
  DS_TYPE_ENCRYPT,
  DS_TYPE_DECRYPT,
  DS_TYPE_TMPFILE,
  DS_TYPE_BUFFER
} ds_type_t;

typedef struct ds_ctxt {
  datasink_t *datasink;
  char *root;
  void *ptr;
  struct ds_ctxt *pipe_ctxt;
  ds_type_t type;
} ds_ctxt_t;

typedef struct {
  void *ptr;
  char *path;
  datasink_t *datasink;
} ds_file_t;

typedef struct {
  size_t skip;
  size_t len;
} ds_sparse_chunk_t;

struct datasink_struct {
  ds_ctxt_t *(*init)(const char *root);
  ds_file_t *(*open)(ds_ctxt_t *ctxt, const char *path, MY_STAT *stat);
  int (*write)(ds_file_t *file, const void *buf, size_t len);
  int (*write_sparse)(ds_file_t *file, const void *buf, size_t len,
                      size_t sparse_map_size,
                      const ds_sparse_chunk_t *sparse_map);
  int (*close)(ds_file_t *file);
  int (*delete_file)(ds_file_t *file);
  void (*cleanup)(ds_ctxt_t *ctxt);
  void (*deinit)(ds_ctxt_t *ctxt);
  void (*size)(ds_ctxt_t *ctxt, size_t *in_size, size_t *out_size);
};

/************************************************************************
Create a datasink of the specified type */
ds_ctxt_t *ds_create(const char *root, ds_type_t type);

/************************************************************************
Open a datasink file */
ds_file_t *ds_open(ds_ctxt_t *ctxt, const char *path, MY_STAT *stat);

/************************************************************************
Write to a datasink file.
@return 0 on success, 1 on error. */
int ds_write(ds_file_t *file, const void *buf, size_t len);

/************************************************************************
Check if sparse files are supported.
@return 1 if yes. */
int ds_is_sparse_write_supported(ds_file_t *file);

/************************************************************************
Write sparse chunk if supported.
@return 0 on success, 1 on error. */
int ds_write_sparse(ds_file_t *file, const void *buf, size_t len,
                    size_t sparse_map_size,
                    const ds_sparse_chunk_t *sparse_map);

/************************************************************************
Close a datasink file.
@return 0 on success, 1, on error. */
int ds_close(ds_file_t *file);

/************************************************************************
Unlink a datasink file, some datasinks don't support this operation such
as stream (tar, xbstream) and stdout.
@return 0 on success, 1, on error. */
int ds_delete(ds_file_t *file);

/************************************************************************
Cleanup a datasink handle */
void ds_cleanup(ds_ctxt_t *ctxt);

/************************************************************************
Deinit a datasink handle */
void ds_deinit(ds_ctxt_t *ctxt);

/************************************************************************
Destroy a datasink handle */
void ds_destroy(ds_ctxt_t *ctxt);

/************************************************************************
Set the destination pipe for a datasink (only makes sense for compress and
tmpfile). */
void ds_set_pipe(ds_ctxt_t *ctxt, ds_ctxt_t *pipe_ctxt);

/************************************************************************
Get type of datasink. */
ds_type_t ds_type(ds_ctxt_t *ctxt);

/************************************************************************
Get the accumulated input size and output size for a datasink, the values
are stored in in_size and out_size respectively.

Note that the in_size and out_size may be different for datasinks which
will reshape data flowed through, such as ds_compress, ds_archive,
ds_xbstream. */
void ds_size(ds_ctxt_t *ctxt, size_t *in_size, size_t *out_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* XB_DATASINK_H */
