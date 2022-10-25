#
# incremental extents test
#

. inc/common.sh

require_xengine

start_server

# create some more tables
mysql -e "CREATE TABLE t (a INT PRIMARY KEY AUTO_INCREMENT, b INT, KEY(b), c VARCHAR(200)) ENGINE=XENGINE;" test

# insert some data
for i in {1..1000} ; do
  echo "INSERT INTO t (b, c) VALUES (FLOOR(RAND() * 1000000), UUID());"
done | mysql test

mysql -e "set global xengine_force_flush_memtable_now=true"

# create some tables and insert some data
load_dbase_schema sakilax
load_dbase_data sakilax

# keep insering data for the rest of the test
( while true ; do
  echo "INSERT INTO t (b, c) VALUES (FLOOR(RAND() * 1000000), UUID());"
done | mysql test ) &

mysql_pid=$!

record_db_state sakilax

xtrabackup --backup --target-dir=$topdir/backup --parallel=10 \
           --debug-sync="before_xengine_acquire_snapshots" &

backup_job_pid=$!

pid_file=$topdir/backup/xtrabackup_debug_sync

wait_for_xb_to_suspend $pid_file

xb_pid=`cat $pid_file`

mysql -e "set global xengine_force_flush_memtable_now=true"

record_db_state test

vlog "Resuming xtrabackup"
kill -SIGCONT $xb_pid

wait $backup_job_pid

kill $mysql_pid
wait $mysql_pid || true

xtrabackup --prepare --apply-log-only --target-dir=$topdir/backup

stop_server

rm -rf $mysql_datadir

xtrabackup --copy-back --target-dir=$topdir/backup --parallel=10

start_server

verify_db_state sakilax
verify_db_state test

