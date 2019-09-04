#
# basic xengine backup test with compression
#

require_xengine
require_qpress

start_server

FULL_BACKUP_CMD="xtrabackup
    --backup
    --target-dir=$topdir/backup
    --parallel=10
    --compress
    --compress-threads=10"

FULL_PREPARE_CMD="xtrabackup
    --parallel=10
    --decompress
    --target-dir=$topdir/backup &&
  xtrabackup
    --prepare
    --apply-log-only
    --target-dir=$topdir/backup"

CLEANUP_CMD="rm -rf $mysql_datadir"

RESTORE_CMD="xtrabackup
    --move-back
    --target-dir=$topdir/backup
    --parallel=10"

. inc/xengine_common.sh

if ls $topdir/backup/.xengine/*.sst.qp 2>/dev/null ; then
    die "SST files are compressed!"
fi

if ls $mysql_datadir/.xengine/*.qp 2>/dev/null ; then
    die "Compressed files are copied back!"
fi
