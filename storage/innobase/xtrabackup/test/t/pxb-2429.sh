# PXB-2349 crash in PXB during tablespace encryption
if ! is_debug_server  ; then
	skip_test "Require debug server version"
fi

. inc/keyring_file.sh
start_server

mysql -e "create tablespace ts add datafile 'ts.ibd' encryption='y'" test 2>/dev/null >/dev/null
innodb_wait_for_flush_all

mysql -e "set global innodb_page_cleaner_disabled_debug=on" test 2>/dev/null >/dev/null
mysql -e "set global innodb_checkpoint_disabled=on" test 2>/dev/null >/dev/null
mysql -e "alter tablespace ts encryption='n'" test 2>/dev/null >/dev/null

mkdir $topdir/backup

vlog "Case#1 backup just after normal encryption"
xtrabackup --backup --target-dir=$topdir/backup

stop_server

xtrabackup --prepare --target-dir=$topdir/backup

rm -rf $mysql_datadir

xtrabackup --copy-back --target-dir=$topdir/backup

start_server

if mysql -e "select encryption from information_schema.INNODB_TABLESPACES where name='ts'" test | grep Y ; then
  die "Tablespace encryption is wrong set"
fi
