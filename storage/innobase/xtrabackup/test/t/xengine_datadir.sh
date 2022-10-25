#
# test xengien backup with non-default backup directory
#

require_xengine

start_server
shutdown_server

xengine_datadir=${TEST_VAR_ROOT}/xengine

mkdir -p ${TEST_VAR_ROOT}/xengine
mv $mysql_datadir/.xengine $xengine_datadir

MYSQLD_EXTRA_MY_CNF_OPTS="
xengine_datadir=$xengine_datadir
"
start_server

mysql -e "CREATE TABLE t (a INT PRIMARY KEY) ENGINE=XENGINE" test
mysql -e "INSERT INTO t (a) VALUES (1), (2), (3)" test

xtrabackup --backup --target-dir=$topdir/backup

if ! [ -d $topdir/backup/xengine ] ; then
    die "XENGINE haven't been backed up"
fi

mysql -e "INSERT INTO t (a) VALUES (4), (5), (6)" test

old_checksum=$(checksum_table_columns test t a)

xtrabackup --prepare --apply-log-only --target-dir=$topdir/backup

rm -rf $mysql_datadir $xengine_datadir

xtrabackup --copy-back --target-dir=$topdir/backup

new_checksum=$(checksum_table_columns test t a)

vlog "Checksums:  $old_checksum $new_checksum"
if ! [ "$old_checksum" == "$new_checksum" ] ; then
    die "Checksums aren't equal"
fi
