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
#define BF_COMPRESSED	1
#define BF_MFSONLY	2
#define BF_BACKUPVAR	4
