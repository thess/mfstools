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
	int fd;
	int swab;
	unsigned int sectors;
#endif
};

struct backup_info
{
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
	int nnewparts;
	struct backup_partition *newparts;
	int varsize;
	int swapsize;
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
#define BF_COMPRESSED	0x00000001
#define BF_MFSONLY	0x00000002
#define BF_BACKUPVAR	0x00000004
#define BF_SHRINK	0x00000008
#define RF_INITIALIZED	0x00010000
#define RF_ENDIAN	0x00020000
#define RF_NOMORECOMP	0x00040000
