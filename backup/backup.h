struct backup_block {
	unsigned int firstsector;
	unsigned int sectors;
};

struct backup_info {
	int cursector;
	int presector;
	int nblocks;
	struct backup_block *blocks;
};

struct block_info {
	unsigned int size;
	struct block_info *next;
};

struct partition_info {
	unsigned int sectors;
	
};
