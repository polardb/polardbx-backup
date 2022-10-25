start_server

require_server_version_higher_than 8.0.0

mysql -e "set global innodb_io_capacity=100"
mysql -e "set global innodb_io_capacity_max=100"
mysql -e "set global innodb_flush_sync=off"
mysql -e "set global innodb_max_dirty_pages_pct=80"
mysql -e "set global innodb_max_dirty_pages_pct_lwm=70"
mysql -e "set global innodb_max_dirty_pages_pct_lwm=70"
mysql -e "set global old_alter_table=OFF"

for i in {1..10} ; do
    cat <<EOF
CREATE TABLE sbtest$i (
  id int(10) unsigned NOT NULL AUTO_INCREMENT,
  k int(10) unsigned NOT NULL DEFAULT '0',
  c char(120) NOT NULL DEFAULT '',
  pad char(60) NOT NULL DEFAULT '',
  PRIMARY KEY (id),
  KEY k_1 (k)
) ENGINE=InnoDB;
EOF
done | mysql test

for i in {1..10000} ; do
    echo "INSERT INTO sbtest1 (id, k, c, pad) VALUES (0, FLOOR(RAND() * 100000), REPEAT(UUID(), 3), UUID());"
done | mysql test

for i in {2..10} ; do
    mysql -e "INSERT INTO sbtest$i (k, c, pad) SELECT k, c, pad FROM sbtest1" test
done

mysql -e "ALTER TABLE test.sbtest2 engine = innodb"
mysql -e "ALTER TABLE test.sbtest2 engine = innodb"

mysql -e "INSERT INTO sbtest2 (k, c, pad) SELECT k, c, pad FROM sbtest1" test


mysql -e "SELECT count(1) FROM test.sbtest2" test > $topdir/count_before

run_cmd_expect_failure $XB_BIN $XB_ARGS --backup --target-dir=$topdir/backup

# Let the backup fail directly.
if ! grep -q "DDL operation has been performed. All modified pages may not have been flushed to the disk yet" $OUTFILE
then
	die "Error message not found!"
fi
rm -fr $topdir/backup/

# With lock-ddl can also pass by the bug. 
xtrabackup --lock-ddl --backup --target-dir=$topdir/backup

stop_server

xtrabackup --prepare --target-dir=$topdir/backup

rm -rf $mysql_datadir

xtrabackup --copy-back --target-dir=$topdir/backup

start_server

diff -u $topdir/count_before <(mysql -e "SELECT count(1) FROM test.sbtest2" test)

