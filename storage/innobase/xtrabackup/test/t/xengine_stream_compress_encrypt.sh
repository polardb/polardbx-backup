#
# basic xengine backup test with streaming, compression and encryption
#

require_xengine

start_server

pass=005929dbbc5602e8404fe7840d7a70c5

FULL_BACKUP_CMD="xtrabackup
    --backup
    --extra-lsndir=$topdir/backuplsn
    --compress
    --compress-threads=4
    --encrypt=AES256
    --encrypt-key=$pass
    --encrypt-threads=4
    --target_dir=$topdir/backup_debug_sync
    --stream=xbstream >$topdir/backup.xbstream"

FULL_PREPARE_CMD="mkdir $topdir/backup &&
  xbstream -C $topdir/backup -x
     --parallel=10 --decompress --decrypt=AES256 --encrypt-threads=10
     --encrypt-key=$pass < $topdir/backup.xbstream &&
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
