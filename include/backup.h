struct backup_block
{
	unsigned int firstsector;
	unsigned int sectors;
};

struct backup_partition
{
	unsigned int sectors;
	char partno;
	char devno;
	char reserved[2];
};

struct device_info
{
	struct tivo_partition_file **files;
	int nparts;
	char *devname;
#ifdef RESTORE
	int swab;
	unsigned int sectors;
#endif
};

struct backup_info
{
	char *lasterr;
	struct mfs_handle *mfs;
	int cursector;
	int presector;
	int ndevs;
	struct device_info *devs;
	int nparts;
	struct backup_partition *parts;
	int nblocks;
	struct backup_block *blocks;
	int nmfs;
	struct backup_partition *mfsparts;
	int nsectors;
	int back_flags;
	struct z_stream_s *comp;
	char *comp_buf;
#ifdef RESTORE
	struct volume_handle *vols;
	int nnewparts;
	struct backup_partition *newparts;
	int varsize;
	int swapsize;
	int bswap;
#else
	unsigned int thresh;
#endif
};

struct block_info
{
	unsigned int size;
	struct block_info *next;
};

struct backup_head
{
	unsigned int magic;	/* TBAK */
	unsigned int flags;
	unsigned int nsectors;
	unsigned int nparts;
	unsigned int nblocks;
	unsigned int mfspairs;
	char reserved[488];
};

#define TB_MAGIC (('T' << 24) + ('B' << 16) + ('A' << 8) + ('K' << 0))
#define TB_ENDIAN (('T' << 0) + ('B' << 8) + ('A' << 16) + ('K' << 24))
#define BF_COMPRESSED	0x00000001	/* Backup is compressed. */
#define BF_MFSONLY	0x00000002	/* Backup is MFS only. */
#define BF_BACKUPVAR	0x00000004	/* /var in backup. */
#define BF_SHRINK	0x00000008	/* Divorced backup. */
#define BF_THRESHSIZE	0x00000010
#define BF_THRESHTOT	0x00000020
#define BF_STREAMTOT	0x00000040
#define BF_NOBSWAP	0x00000080	/* Source isn't byte swapped. */
#define BF_COMPLVL(f)	(((f) >> 12) & 0xf)
#define BF_SETCOMP(l)	((((l) & 0xf) << 12) | BF_COMPRESSED)
#define BF_FLAGS	0x0000ffff
#define RF_INITIALIZED	0x00010000	/* Restore initialized. */
#define RF_ENDIAN	0x00020000	/* Restore from different endian. */
#define RF_NOMORECOMP	0x00040000	/* No more compressed data. */
#define RF_ZEROPART	0x00080000	/* Zero out non restored partitions. */
#define RF_BALANCE	0x00100000	/* Balance partition layout. */
#define RF_NOFILL	0x00200000	/* Leave room for more partitions. */
#define RF_FLAGS	0xffff0000

#ifndef EXTERNINLINE
#if DEBUG
#define EXTERNINLINE static inline
#else
#define EXTERNINLINE extern inline
#endif
#endif

EXTERNINLINE char *
last_err (struct backup_info *info)
{
	return info->lasterr;
}

struct backup_info *init_backup (char *device, char *device2, int flags);
void backup_set_thresh (struct backup_info *info, unsigned int thresh);
int backup_start (struct backup_info *info);
unsigned int backup_read (struct backup_info *info, char *buf, unsigned int size);
int backup_finish (struct backup_info *info);

struct backup_info *init_restore (unsigned int flags);
void restore_set_varsize (struct backup_info *info, int size);
void restore_set_swapsize (struct backup_info *info, int size);
unsigned int restore_write (struct backup_info *info, char *buf, unsigned int size);
int restore_trydev (struct backup_info *info, char *dev1, char *dev2);
int restore_start (struct backup_info *info);
int restore_finish(struct backup_info *info);
