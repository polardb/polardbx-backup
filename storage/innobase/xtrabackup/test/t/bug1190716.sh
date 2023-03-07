##########################################################################
# Bug #1190716: innobackupex --safe-slave-backup hangs when used on master
##########################################################################

. inc/common.sh

skip_test "Not surpport --safe-slave-backup for XDB now"

start_server

xtrabackup --backup --safe-slave-backup --target-dir=$topdir/backup

# mysql_variable status[] = {{"Read_Master_Log_Pos", &read_master_log_pos},
#                            {"Slave_SQL_Running", &slave_sql_running},
#                            {NULL, NULL}};

#  read_mysql_variables(connection, "SHOW SLAVE STATUS", status, false);

#  // For XDB, this can not recognize if it is a follower
#  if (!(read_master_log_pos && slave_sql_running)) {
#    msg("Not checking slave open temp tables for "
#        "--safe-slave-backup because host is not a slave\n");
#    goto cleanup;
#  }
grep -q "Not checking slave open temp tables for --safe-slave-backup because host is not a slave" $OUTFILE
