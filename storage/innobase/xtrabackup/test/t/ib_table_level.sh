########################################################################
# Test support for table level backup and recover
########################################################################

. inc/common.sh

MYSQLD_EXTRA_MY_CNF_OPTS="innodb_file_per_table"

start_server

load_sakila

# Make sure table level mode only works with xbstream
run_cmd_expect_failure  $XB_BIN $XB_ARGS --no-timestamp --rds-table-level $topdir/backup

run_cmd mkdir -p $topdir/tmp_backup
run_cmd mkdir -p $topdir/backup
vlog "Starting backup"
run_cmd xtrabackup --backup --no-timestamp --stream=xbstream --rds-table-level \
--target-dir=$topdir/tmp_backup \
| xbstream -x -C $topdir/backup

if ! ls $topdir/backup/rds_table_info_json*.log > /dev/null
then
    die "No JSON meta file!"
fi

vlog "Prepare backup, only apply redo log"
run_cmd xtrabackup --prepare --apply-log-only --rds-table-level --target-dir=$topdir/backup

## The ibd files may be recreated while applying redo log (depends on the starting
## checkpoint position), so we do the removing after redo log applied.
vlog "Remove some tablespaces"
run_cmd rm -f $topdir/backup/sakila/film*

vlog "Prepare backup again"
run_cmd xtrabackup --prepare --rds-table-levelm$topdir/backup

stop_server
rm -rf $mysql_datadir
vlog "Data destroyed"

vlog "Copying files to their original locations"
xtrabackup --copy-back --target-dir=$topdir/backup
vlog "Data restored"

# if ! egrep -q "file for 'sakila/film.* will be removed from the data dictionary" $OUTFILE; then
#     die "sakila/film.* should be removed, but actually not."
# fi

start_server

num=$(${MYSQL} ${MYSQL_ARGS} -BNe "SELECT count(*) FROM INFORMATION_SCHEMA.INNODB_TABLES WHERE NAME LIKE 'sakila/film%'")

# if [ $num != "0" ]
# then
#     die "There should be no tables prefix with 'film' in InnoDB"
# fi
