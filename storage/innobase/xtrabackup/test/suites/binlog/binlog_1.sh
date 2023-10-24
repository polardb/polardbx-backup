. inc/common.sh
. inc/binlog_common.sh

skip_test "Requires skip-log-bin, but xdb can not"

vlog "------- TEST 1 -------"
INDEX_FILE=""
FILES=""
backup_restore --skip-log-bin
