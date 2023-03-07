#
# PXB-1902: PXB is unable to connect to the database with transition-key when password is specified
#

skip_test "Requires Percona Server"

start_server

mysql -e "CREATE USER 'bkpuser'@'localhost' IDENTIFIED BY '111'"
mysql -e "GRANT BACKUP_ADMIN, RELOAD, LOCK TABLES, PROCESS, REPLICATION CLIENT ON *.* TO 'bkpuser'@'localhost';"
mysql -e "GRANT SELECT ON performance_schema.log_status TO 'bkpuser'@'localhost'"
mysql -e "GRANT ALL ON PERCONA_SCHEMA.* TO 'bkpuser'@'localhost';"

xtrabackup -ubkpuser -p111 --backup --target-dir=$topdir/backup \
	   --transition-key=abcd
