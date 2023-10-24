. inc/common.sh

skip_test "Requires group replica mode, but xdb can not"

start_group_replication_cluster 3

mysql -e "SELECT * FROM performance_schema.replication_group_members;"

xtrabackup --backup --target-dir=$topdir/backup
xtrabackup --prepare --target-dir=$topdir/backup
