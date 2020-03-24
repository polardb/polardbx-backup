################################################################################
# Persisting binlog into redo feature implements a new redo record type
# MLOG_SERVER_DATA
# In prepare and apply phase, xtrabackup should ignore the record type with
# no interrupt.
################################################################################

function require_persist_binlog_to_redo() {
    if ! ${MYSQLD} --help --verbose 2>&1 | grep -q persist-binlog-to-redo; then
        skip_test "Requires --persist-binlog-to-redo support"
    fi
}

require_persist_binlog_to_redo

MYSQLD_EXTRA_MY_CNF_OPTS="
gtid_mode=on
log_slave_updates=on
enforce_gtid_consistency=on
persist_binlog_to_redo=on
"

start_server

# Generate some InnoDB entries in the binary log
run_cmd $MYSQL $MYSQL_ARGS test <<EOF
CREATE TABLE t1 (a INT) ENGINE=InnoDB;
INSERT INTO t1 VALUES (1), (2), (3);
FLUSH BINARY LOGS;
INSERT INTO t1 VALUES (4), (5), (6);
EOF

gtid_executed=`get_gtid_executed`
xb_binlog_info=$topdir/backup/xtrabackup_binlog_info

record_db_state test

xtrabackup --backup --target-dir=$topdir/backup

binlog_file=`get_binlog_file`
binlog_pos=`get_binlog_pos`

xtrabackup --prepare --target-dir=$topdir/backup

stop_server
rm -r $mysql_datadir
xtrabackup --copy-back --target-dir=$topdir/backup

start_server

verify_db_state test

run_cmd diff -u $xb_binlog_info - <<EOF
$binlog_file	$binlog_pos	$gtid_executed
EOF

