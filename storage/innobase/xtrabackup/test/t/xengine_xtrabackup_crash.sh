#
# test xtrabackup crash while backuping
#

. inc/common.sh

require_xengine

start_server

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

##
xtrabackup --backup --target-dir=$topdir/backup --parallel=10 \
           --debug-sync="after_xengine_copy_sst_files" &

backup_job_pid=$!

pid_file=$topdir/backup/xtrabackup_debug_sync

wait_for_xb_to_suspend $pid_file

mysql -e "show variables like '%xengine_hotbackup%';"
run_cmd ls $mysql_datadir/.xengine/hotbackup_tmp

kill $backup_job_pid
run_cmd_expect_failure wait $backup_job_pid

mysql -e "show variables like '%xengine_hotbackup%';"
run_cmd_expect_failure ls $mysql_datadir/.xengine/hotbackup_tmp

rm -rf $topdir/backup

##
xtrabackup --backup --target-dir=$topdir/backup --parallel=10 \
           --debug-sync="after_xengine_acquire_snapshots" &

backup_job_pid=$!

pid_file=$topdir/backup/xtrabackup_debug_sync

wait_for_xb_to_suspend $pid_file

mysql -e "show variables like '%xengine_hotbackup%';"
run_cmd ls $mysql_datadir/.xengine/hotbackup_tmp

kill $backup_job_pid
run_cmd_expect_failure wait $backup_job_pid

mysql -e "show variables like '%xengine_hotbackup%';"
run_cmd_expect_failure ls $mysql_datadir/.xengine/hotbackup_tmp

rm -rf $topdir/backup

##
xtrabackup --backup --target-dir=$topdir/backup --parallel=10 \
           --debug-sync="after_xengine_record_incemental_extent_ids" &

backup_job_pid=$!

pid_file=$topdir/backup/xtrabackup_debug_sync

wait_for_xb_to_suspend $pid_file

run_cmd ls $mysql_datadir/.xengine/hotbackup_tmp

kill $backup_job_pid
run_cmd_expect_failure wait $backup_job_pid

mysql -e "show variables like '%xengine_hotbackup%';"
run_cmd_expect_failure $mysql_datadir/.xengine/hotbackup_tmp

rm -rf $topdir/backup

##
xtrabackup --backup --target-dir=$topdir/backup --parallel=10 \
           --debug-sync="after_xengine_record_incemental_extents" &

backup_job_pid=$!

pid_file=$topdir/backup/xtrabackup_debug_sync

wait_for_xb_to_suspend $pid_file

run_cmd ls $mysql_datadir/.xengine/hotbackup_tmp

kill $backup_job_pid
run_cmd_expect_failure wait $backup_job_pid

mysql -e "show variables like '%xengine_hotbackup%';"
run_cmd_expect_failure ls $mysql_datadir/.xengine/hotbackup_tmp
