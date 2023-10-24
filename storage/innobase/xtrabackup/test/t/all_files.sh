#
# Compare files in the datadir and the backup dir
#

start_server

load_sakila

mysql -e "CREATE TABLE t1 (a INT) ENGINE=MyISAM" test
mysql -e "CREATE TABLE t2 (a INT) ENGINE=InnoDB" test

function compare_files() {

dir1=$1
dir2=$2

ign_list="(innodb_temp|xtrabackup_binlog_pos_innodb|dblwr|innodb_redo|ib_logfile|__recycle_bin__)"

# files that present in the backup directory, but not present in the datadir
diff -u <( ( ( cd $dir1; find . | grep -Ev $ign_list )
             ( cd $dir2; find . | grep -Ev $ign_list )
             ( cd $dir2; find . | grep -Ev $ign_list ) ) | sort | uniq -u ) - <<EOF
./backup-my.cnf
./xtrabackup_binlog_info
./xtrabackup_checkpoints
./xtrabackup_info
./xtrabackup_logfile
./xtrabackup_tablespaces
EOF

XTRA_DOUBLEWRITE=""

# files that present in the datadir, but not present in the backup
diff -B -u <( ( ( cd $dir1; find . | grep -Ev $ign_list )
                ( cd $dir1; find . | grep -Ev $ign_list )
                ( cd $dir2; find . | grep -Ev $ign_list ) ) | sort | uniq -u ) - <<EOF
./auto.cnf
./ca-key.pem
./ca.pem
./client-cert.pem
./client-key.pem
./mysqld1.err
./private_key.pem
./public_key.pem
./server-cert.pem
./server-key.pem
${XTRA_DOUBLEWRITE}
EOF

}

function compare_files_inc() {

dir1=$1
dir2=$2

# files that present in the backup directory, but not present in the datadir
diff -u <( ( ( cd $dir1; find . | grep -Ev $ign_list )
             ( cd $dir2; find . | grep -Ev $ign_list )
             ( cd $dir2; find . | grep -Ev $ign_list ) ) | sort | uniq -u ) - <<EOF
./backup-my.cnf
./xtrabackup_binlog_info
./xtrabackup_checkpoints
./xtrabackup_info
./xtrabackup_logfile
./xtrabackup_tablespaces
EOF

# files that present in the datadir, but not present in the backup
diff -B -u <( ( ( cd $dir1; find . | grep -Ev $ign_list )
                ( cd $dir1; find . | grep -Ev $ign_list )
                ( cd $dir2; find . | grep -Ev "innodb_temp|dblwr|innodb_redo|ib_logfile|__recycle_bin__" ) ) | sort | uniq -u ) - <<EOF
./auto.cnf
./ca-key.pem
./ca.pem
./client-cert.pem
./client-key.pem
./mysqld1.err
./private_key.pem
./public_key.pem
./server-cert.pem
./server-key.pem
${XTRA_DOUBLEWRITE}
EOF

}

xtrabackup --backup --target-dir=$topdir/backup
cp -a $topdir/backup $topdir/full
xtrabackup --prepare --target-dir=$topdir/backup
compare_files $topdir/backup $mysql_datadir

# PXB-1696: Incremental prepare removes .sdi files from the data dir
mysql -e "CREATE TABLE t3 (a INT) ENGINE=MyISAM" test
mysql -e "CREATE TABLE t4 (a INT) ENGINE=InnoDB" test

xtrabackup --backup --target-dir=$topdir/inc --incremental-basedir=$topdir/full
xtrabackup --prepare --target-dir=$topdir/full --apply-log-only
xtrabackup --prepare --target-dir=$topdir/full --incremental-dir=$topdir/inc
compare_files_inc $topdir/full $mysql_datadir
