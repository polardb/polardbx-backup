#
# basic xengine backup test
#

require_xengine

start_server

FULL_BACKUP_CMD="xtrabackup
    --backup
    --target-dir=$topdir/backup
    --parallel=10"

FULL_PREPARE_CMD="xtrabackup
    --prepare
    --apply-log-only
    --target-dir=$topdir/backup"

CLEANUP_CMD="rm -rf $mysql_datadir"

RESTORE_CMD="xtrabackup
    --copy-back
    --target-dir=$topdir/backup
    --parallel=10"

. inc/xengine_common.sh
