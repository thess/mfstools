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
