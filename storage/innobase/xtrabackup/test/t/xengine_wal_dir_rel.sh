#
# test xengine backup with non-default backup directory
#

require_xengine

#start_server
#shutdown_server

xengine_wal_dir=xengine/wal

mkdir -p ${mysql_datadir}/xengine/wal

MYSQLD_EXTRA_MY_CNF_OPTS="
xengine_wal_dir=$xengine_wal_dir
"
start_server

mysql -e "CREATE TABLE t (a INT AUTO_INCREMENT PRIMARY KEY) ENGINE=XENGINE" test
mysql -e "INSERT INTO t (a) VALUES (1), (2), (3)" test

while true ; do
    echo "INSERT INTO t () VALUES ();"
done | mysql test &

xtrabackup --backup --target-dir=$topdir/backup

if ! [ -d $topdir/backup/.xengine ] ; then
    die "XENGINE haven't been backed up"
fi

xtrabackup --prepare --target-dir=$topdir/backup

stop_server

rm -rf $mysql_datadir

xtrabackup --copy-back --target-dir=$topdir/backup

if ! ls $mysql_datadir/$xengine_wal_dir/*.wal 1> /dev/null 2>&1; then
    die "Logs haven't been copied to wal dir"
fi

start_server

mysql -e "SELECT COUNT(*) FROM t" test
