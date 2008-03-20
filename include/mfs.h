#ifndef MFS_H
#define MFS_H

#include "util.h"
#include "volume.h"
#include "zonemap.h"
#include "fsid.h"

#define MFS32_MAGIC	0xABBAFEED
#define MFS64_MAGIC	0xEBBAFEED

typedef struct volume_header_32_s
{
	uint32_t state;
	uint32_t magic;
	uint32_t checksum;
	uint32_t off0c;
	uint32_t root_fsid;		/* Maybe? */
	uint32_t off14;
	uint32_t firstpartsize;	/* Size of first partition / 1024 sectors */
	uint32_t off1c;
	uint32_t off20;
	unsigned char partitionlist[128];
	uint32_t total_sectors;
	uint32_t offa8;
	uint32_t logstart;
	uint32_t lognsectors;
	uint32_t logstamp;
	uint32_t unkstart;		/* Not sure what it's used for */
	uint32_t unksectors;	/* But definately an allocated area */
	uint32_t offc0;
	zone_map_ptr_32 zonemap;
	uint32_t next_fsid;		/* Incrementing value for the next fsid to create */
								/* Following two used in transaction log */
								/* And inodes */
	uint32_t bootcycles;	/* Seems to be times booted on */
	uint32_t bootsecs;		/* Seems to be seconds since boot */
	uint32_t offe4;
}
volume_header_32;

typedef struct volume_header_64_s
{
	uint32_t state;
	uint32_t magic;
	uint32_t checksum;
	uint32_t off0c;
	uint32_t root_fsid;		/* Maybe? */
	uint32_t off14;
	uint32_t firstpartsize;	/* Size of first partition / 1024 sectors */
	uint32_t off1c;
	uint32_t off20;
	unsigned char partitionlist[132];
	uint64_t total_sectors;
	uint64_t logstart;
	uint32_t offb8;
	uint32_t logstamp;
	uint64_t unkstart;
	uint32_t offc8;
	uint32_t unkstamp;
	zone_map_ptr_64 zonemap;
	uint32_t unknsectors;
	uint32_t lognsectors;
	uint32_t off100;
	uint32_t next_fsid;		/* Incrementing value for the next fsid to create */
								/* Following two used in transaction log */
								/* And inodes */
	uint32_t bootcycles;	/* Seems to be times booted on */
	uint32_t bootsecs;		/* Seems to be seconds since boot */
	uint32_t off110;
	uint32_t off114;
}
volume_header_64;

typedef union volume_header_u
{
	volume_header_32 v32;
	volume_header_64 v64;
}
volume_header;

/* Linked lists of zone maps for a certain type of map */
struct zone_map
{
	zone_header *map;
	bitmap_header **bitmaps;
	int dirty;
	struct zone_map *next;
	struct zone_map *next_loaded;
};

/* Head of zone maps linked list, contains totals as well */
struct zone_map_head
{
	uint64_t size;
	uint64_t free;
	struct zone_map *next;
};

struct mfs_handle
{
	struct volume_handle *vols;
	volume_header vol_hdr;
	struct zone_map_head zones[ztMax];
	struct zone_map *loaded_zones;
	struct log_hdr_s *current_log;

	int inode_log_type;
	int is_64;

	uint32_t bootcycle;
	uint32_t bootsecs;
	uint32_t lastlogsync;
	uint32_t lastlogcommit;

	char *err_msg;
	void *err_arg1;
	void *err_arg2;
	void *err_arg3;
};

#define SABLOCKSEC 1630000

// Flags to pass to mfs_init along with the accmode
#define MFS_ERROROK		0x04000000	// Open despite errors

unsigned int compute_crc (unsigned char *data, unsigned int size, unsigned int crc);
unsigned int mfs_compute_crc (unsigned char *data, unsigned int size, unsigned int off);
unsigned int mfs_check_crc (unsigned char *data, unsigned int size, unsigned int off);
void mfs_update_crc (unsigned char *data, unsigned int size, unsigned int off);
void data_swab (void *data, int size);
zone_header *mfs_next_zone (struct mfs_handle *mfshdn, zone_header *cur);
int mfs_zone_map_commit (struct mfs_handle *mfshnd, unsigned int logstamp);
int mfs_zone_map_update (struct mfs_handle *mfshnd, uint64_t sector, uint64_t size, uint32_t state, uint32_t logstamp);
int mfs_zone_map_block_state (struct mfs_handle *mfshnd, uint64_t sector, uint64_t size);
uint32_t mfs_inode_count (struct mfs_handle *mfshnd);
uint64_t mfs_inode_to_sector (struct mfs_handle *mfshnd, uint32_t inode);
mfs_inode *mfs_read_inode (struct mfs_handle *mfshnd, uint32_t inode);
mfs_inode *mfs_read_inode_by_fsid (struct mfs_handle *mfshnd, uint32_t fsid);
mfs_inode *mfs_find_inode_for_fsid (struct mfs_handle *mfshnd, uint32_t fsid);
int mfs_write_inode (struct mfs_handle *mfshnd, mfs_inode *inode);
int mfs_read_inode_data_part (struct mfs_handle *mfshnd, mfs_inode * inode, unsigned char *data, uint64_t start, unsigned int count);
unsigned char *mfs_read_inode_data (struct mfs_handle *mfshnd, mfs_inode * inode, int *size);
int mfs_add_volume_pair (struct mfs_handle *mfshnd, char *app, char *media, uint32_t minalloc);
int mfs_load_volume_header (struct mfs_handle *mfshnd, int flags);
void mfs_cleanup_zone_maps (struct mfs_handle *mfshnd);
int mfs_load_zone_maps (struct mfs_handle *hnd);
struct mfs_handle *mfs_init (char *hda, char *hdb, int flags);
int mfs_reinit (struct mfs_handle *mfshnd, int flags);
void mfs_cleanup (struct mfs_handle *mfshnd);
char *mfs_partition_list (struct mfs_handle *mfshnd);

void mfs_perror (struct mfs_handle *mfshnd, char *str);
int mfs_strerror (struct mfs_handle *mfshnd, char *str);
int mfs_has_error (struct mfs_handle *mfshnd);
void mfs_clearerror (struct mfs_handle *mfshnd);

#define mfs_read_data(mfshnd,buf,sector,count) mfsvol_read_data ((mfshnd)->vols, buf, sector, count)
#define mfs_write_data(mfshnd,buf,sector,count) mfsvol_write_data ((mfshnd)->vols, buf, sector, count)
#define mfs_volume_size(mfshnd,sector) mfsvol_volume_size ((mfshnd)->vols, sector)
#define mfs_volume_set_size(mfshnd) mfsvol_volume_set_size ((mfshnd)->vols)
#define mfs_enable_memwrite(mfshnd) mfsvol_enable_memwrite ((mfshnd)->vols)
#define mfs_discard_memwrite(mfshnd) mfsvol_discard_memwrite ((mfshnd)->vols)
#define mfs_is_64bit(mfshnd) ((mfshnd)->is_64)
#define mfs_volume_header(mfshnd) (&(mfshnd)->vol_hdr)

#endif	/* MFS_H */
