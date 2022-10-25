########################################################################
# Tests for xtrabackup --print-param
########################################################################

my_cnf="[mysqld]
datadir=/some/data/dir
tmpdir=/some/tmp/dir1:/some/tmp/dir2
innodb_data_home_dir=/some/innodb/dir
innodb_data_file_path=ibdata1:10M;ibdata2:5M:autoextend
innodb_log_group_home_dir=/some/log/dir
innodb_log_files_in_group=3
innodb_log_file_size=5M
innodb_flush_method=O_DIRECT
innodb_page_size=4K
innodb_log_block_size=4K
innodb_doublewrite_file=/some/doublewrite/file
innodb_undo_directory=/some/undo/directory
innodb_undo_tablespaces=8
innodb_checksum_algorithm=strict_crc32
innodb_buffer_pool_filename=/some/buffer/pool/file"

echo "$my_cnf" >$topdir/my.cnf

diff -u <($XB_BIN --defaults-file=$topdir/my.cnf --print-param) - <<EOF
# This MySQL options file was generated by XtraBackup.
[mysqld]
datadir=/some/data/dir
tmpdir=/some/tmp/dir1:/some/tmp/dir2
innodb_data_home_dir=/some/innodb/dir
innodb_data_file_path=ibdata1:10M;ibdata2:5M:autoextend
innodb_log_group_home_dir=/some/log/dir
innodb_log_files_in_group=3
innodb_log_file_size=5242880
innodb_flush_method=O_DIRECT
innodb_page_size=4096
innodb_log_block_size=4096
innodb_doublewrite_file=/some/doublewrite/file
innodb_undo_directory=/some/undo/directory
innodb_undo_tablespaces=8
innodb_checksum_algorithm=strict_crc32
innodb_buffer_pool_filename=/some/buffer/pool/file
EOF

diff -u <($XB_BIN --no-defautls 2>/dev/null | grep -E log.buffer.size) - <<EOF
  --innodb-log-buffer-size=# 
innodb-log-buffer-size            16777216
EOF
