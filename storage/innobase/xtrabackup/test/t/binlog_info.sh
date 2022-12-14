########################################################################
# binlog-info tests
########################################################################

require_server_version_higher_than 8.0.16

function test_binlog_info() {

    start_server $@

    has_backup_locks && bl_avail=1 || bl_avail=0
    has_backup_safe_binlog_info && bsbi_avail=1 || bsbi_avail=0
    is_gtid_mode && gtid=1 || gtid=0

    # Generate some InnoDB entries in the binary log
    run_cmd $MYSQL $MYSQL_ARGS test <<EOF
    CREATE TABLE p (a INT) ENGINE=InnoDB;
    INSERT INTO p VALUES (1), (2), (3);
EOF

    # Generate some non-InnoDB entries in the binary log
    run_cmd $MYSQL $MYSQL_ARGS test <<EOF
    CREATE TABLE t (a INT) ENGINE=MyISAM;
    INSERT INTO t VALUES (1), (2), (3);
EOF

    if [ $gtid = 1 ] ; then
        gtid_executed=`get_gtid_executed`
    fi

    # In rds 8.0, MyISAM tables's binlog pos is upated into innodb.
    binlog_file_innodb=`get_binlog_file`
    binlog_pos_innodb=`get_binlog_pos`
    binlog_info_innodb="$binlog_file_innodb	$binlog_pos_innodb"

    xb_binlog_info=$topdir/backup/xtrabackup_binlog_info
    xb_binlog_info_innodb=$topdir/backup/xtrabackup_binlog_pos_innodb

    xtrabackup --backup --target-dir=$topdir/backup

    binlog_file=`get_binlog_file`
    binlog_pos=`get_binlog_pos`

    verify_binlog_info_on

    rm -rf $topdir/backup

    stop_server

    rm -rf $mysql_datadir

}

function normalize_path()
{
    sed -i -e 's|^\./||' $1
}

function verify_binlog_info_on()
{
    normalize_path $xb_binlog_info

    if [ $gtid = 1 ]
    then
		run_cmd diff -u $xb_binlog_info - <<EOF
$binlog_file	$binlog_pos	$gtid_executed
EOF
	else
		run_cmd diff -u $xb_binlog_info - <<EOF
$binlog_file	$binlog_pos
EOF
	fi
    xtrabackup --prepare --target-dir=$topdir/backup

    if ! [ -f $xb_binlog_info_innodb ] ; then
        return
    fi

    normalize_path $xb_binlog_info_innodb

    if [ $bsbi_avail = 1 ]
    then
        # Real coordinates in xtrabackup_binlog_pos_innodb
        run_cmd diff -u $xb_binlog_info_innodb - <<EOF
$binlog_file	$binlog_pos
EOF
    else
        # Stale coordinates in xtrabackup_binlog_pos_innodb
        run_cmd diff -u $xb_binlog_info_innodb - <<EOF
$binlog_file_innodb	$binlog_pos_innodb
EOF
    fi
}

test_binlog_info

test_binlog_info --gtid-mode=ON --enforce-gtid-consistency=ON \
                 --log-bin --log-slave-updates
