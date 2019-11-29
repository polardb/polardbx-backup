#
# basic test for xengine backup
#

# create some tables and insert some data
load_dbase_schema sakilax
load_dbase_data sakilax

mysql -e "set global xengine_force_flush_memtable_now=true"

# create some more tables
mysql -e "CREATE TABLE t (a INT PRIMARY KEY AUTO_INCREMENT, b INT, KEY(b), c VARCHAR(200)) ENGINE=XENGINE;" test

# insert some data
for i in {1..1000} ; do
  echo "INSERT INTO t (b, c) VALUES (FLOOR(RAND() * 1000000), UUID());"
done | mysql test

# keep insering data for the rest of the test
( while true ; do
  echo "INSERT INTO t (b, c) VALUES (FLOOR(RAND() * 1000000), UUID());"
done | mysql test ) &

mysql_pid=$!

record_db_state sakilax

eval ${FULL_BACKUP_CMD} --debug-sync='before_xengine_acquire_snapshots' &
backup_job_pid=$!

[ -z "${pid_file:-}" ] && pid_file=$topdir/backup/xtrabackup_debug_sync
wait_for_xb_to_suspend $pid_file
xb_pid=`cat $pid_file`

mysql -e "set global xengine_force_flush_memtable_now=true"

kill $mysql_pid
wait $mysql_pid || true

record_db_state test

# Resume xtrabackup
vlog "Resuming xtrabackup"
kill -SIGCONT $xb_pid

run_cmd wait $backup_job_pid

# insert some more data
for i in {1..1000} ; do
  echo "INSERT INTO t (b, c) VALUES (FLOOR(RAND() * 1000000), UUID());"
done | mysql test

stop_server

eval ${CLEANUP_CMD}

eval ${FULL_PREPARE_CMD}

eval ${RESTORE_CMD}

start_server

verify_db_state sakilax
verify_db_state test
