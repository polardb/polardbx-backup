. inc/common.sh
. inc/binlog_common.sh
vlog "------- TEST 3 -------"
INDEX_FILE="binlog123.index"
FILES="binlog123.000001"
backup_restore --log-bin=binlog123
