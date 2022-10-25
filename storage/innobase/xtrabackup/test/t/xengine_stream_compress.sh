#
# basic xengine backup test with streaming and compression
#

require_xengine

start_server

FULL_BACKUP_CMD="xtrabackup
    --backup
    --extra-lsndir=$topdir/backuplsn
    --parallel=10
    --compress
    --compress-threads=10
    --target_dir=$topdir/backup_debug_sync
    --stream=xbstream >$topdir/backup.xbstream"

FULL_PREPARE_CMD="mkdir $topdir/backup &&
  xbstream -C $topdir/backup -x
     --parallel=10 --decompress < $topdir/backup.xbstream &&
  xtrabackup
    --prepare
    --apply-log-only
    --target-dir=$topdir/backup"

CLEANUP_CMD="rm -rf $mysql_datadir"

RESTORE_CMD="xtrabackup
    --move-back
    --target-dir=$topdir/backup
    --parallel=10"

pid_file=$topdir/backup_debug_sync/xtrabackup_debug_sync

. inc/xengine_common.sh
