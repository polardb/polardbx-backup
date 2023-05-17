
require_server_version_higher_than 8.0.0

MYSQLD_EXTRA_MY_CNF_OPTS="
innodb_buffer_pool_size=256M
innodb_log_file_size=500M
innodb_undo_retention=90000
innodb_txn_cached_list_keep_size=128
"
start_server

mysql -e "set global innodb_io_capacity=100"
mysql -e "set global innodb_io_capacity_max=100"
mysql -e "set global innodb_flush_sync=off"
mysql -e "set global innodb_max_dirty_pages_pct=99.999"
mysql -e "set global innodb_max_dirty_pages_pct_lwm=70"


run_cmd $MYSQL $MYSQL_ARGS test <<EOF
CREATE TABLE accounts (
  id int(11) NOT NULL,
  balance int(11) NOT NULL,
  version int(11) NOT NULL DEFAULT '0',
  gmt_modified timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id)
) ENGINE=InnoDB;
EOF

for i in {0..99} ; do
  echo "INSERT INTO test.accounts (id, balance, version) VALUES ($i, 1000, 0);"
done | mysql test

mysql -e "SELECT count(1) FROM test.accounts" test

current_gcn=`mysql -e "show status like 'Lizard_current_gcn'"| sed -n '2p' | awk '{print $2}'`

for i in {1..10000} ; do
  cat <<EOF

xa begin '$i';
update test.accounts set balance = balance + 1  where id = floor(rand()*100);
update test.accounts set balance = balance + -1 where id = floor(rand()*100);
xa end '$i';
xa prepare '$i';
set innodb_commit_seq=$(expr $current_gcn + $i);
xa commit '$i';

EOF
done | mysql test

mysql -e "SELECT * FROM test.accounts" test > $topdir/accounts_before
mysql -e "SHOW STATUS LIKE '%gcn'" test > $topdir/gcn_status_before
# for debug
mysql -e "SHOW ENGINE INNODB STATUS\G" > $topdir/engine_status

xtrabackup --lock-ddl --backup --target-dir=$topdir/backup --core-file

stop_server

xtrabackup --prepare --target-dir=$topdir/backup --core-file

rm -rf $mysql_datadir

xtrabackup --copy-back --target-dir=$topdir/backup --core-file

start_server

diff -u $topdir/accounts_before <(mysql -e "SELECT * FROM test.accounts" test)
diff -u $topdir/gcn_status_before <(mysql -e "SHOW STATUS LIKE '%gcn'" test)
