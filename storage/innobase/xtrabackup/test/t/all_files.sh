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

ign_list="(innodb_temp|xtrabackup_binlog_pos_innodb|*.dblwr|__recycle_bin__\/db.opt|ALISQL_PERFORMANCE_AGENT)"

echo "compare_files# files that present in the backup directory, but not present in the datadir"
diff -u <( ( ( cd $dir1; find . | grep -Ev $ign_list )
             ( cd $dir2; find . | grep -Ev $ign_list )
             ( cd $dir2; find . | grep -Ev $ign_list ) ) | sort | uniq -u ) - <<EOF
./backup-my.cnf
./xtrabackup_binlog_info
./xtrabackup_checkpoints
./xtrabackup_info
./xtrabackup_logfile
./xtrabackup_master_key_id
./xtrabackup_tablespaces
EOF

if is_server_version_higher_than 8.0.19; then
    XTRA_DOUBLEWRITE=""
elif is_xtradb ; then
    XTRA_DOUBLEWRITE="./xb_doublewrite"
else
    XTRA_DOUBLEWRITE=""
fi

echo "compare_files# files that present in the datadir, but not present in the backup"
diff -B -u <( ( ( cd $dir1; find . | grep -Ev $ign_list )
                ( cd $dir1; find . | grep -Ev $ign_list )
                ( cd $dir2; find . | grep -Ev $ign_list ) ) | sort | uniq -u ) - <<EOF
./auto.cnf
./mysql-bin.000001
./mysql-bin.000002
./mysqld1.err
./private_key.pem
./public_key.pem
${XTRA_DOUBLEWRITE}
EOF

}

function compare_files_inc() {

dir1=$1
dir2=$2

echo "compare_files_inc# files that present in the backup directory, but not present in the datadir"
diff -u <( ( ( cd $dir1; find . | grep -Ev $ign_list )
             ( cd $dir2; find . | grep -Ev $ign_list )
             ( cd $dir2; find . | grep -Ev $ign_list ) ) | sort | uniq -u ) - <<EOF
./backup-my.cnf
./xtrabackup_binlog_info
./xtrabackup_checkpoints
./xtrabackup_info
./xtrabackup_logfile
./xtrabackup_master_key_id
./xtrabackup_tablespaces
EOF

echo "compare_files_inc# files that present in the datadir, but not present in the backup"
diff -B -u <( ( ( cd $dir1; find . | grep -Ev $ign_list )
                ( cd $dir1; find . | grep -Ev $ign_list )
                ( cd $dir2; find . | grep -Ev "innodb_temp|*.dblwr" ) ) | sort | uniq -u ) - <<EOF
./ALISQL_PERFORMANCE_AGENT_GENERAL.log
./ALISQL_PERFORMANCE_AGENT_IO.log
./ALISQL_PERFORMANCE_AGENT_PERF.csv
./auto.cnf
./mysql-bin.000001
./mysql-bin.000002
./mysql-bin.000003
./mysqld1.err
./private_key.pem
./public_key.pem
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
