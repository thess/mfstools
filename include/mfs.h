#ifndef MFS_H
#define MFS_H

#include "volume.h"
#include "zonemap.h"
#include "fsid.h"

/* Size that TiVo rounds the partitions down to whole increments of. */
#define MFS_PARTITION_ROUND 1024

/* Flags for vol_flags below */
/* #define VOL_FILE        1        This volume is really a file */
#define VOL_RDONLY      2		/* This volume is read-only */
/* #define VOL_SWAB        4        This volume is byte-swapped */

/* Information about the list of volumes needed for reads */
struct volume_info
{
	struct tivo_partition_file *file;
	unsigned int start;
	unsigned int sectors;
	unsigned int offset;
	int vol_flags;
	struct volume_info *next;
};

/* Linked lists of zone maps for a certain type of map */
struct zone_map
{
	zone_header *map;
	struct zone_map *next;
	struct zone_map *next_loaded;
};

/* Head of zone maps linked list, contains totals as well */
struct zone_map_head
{
	unsigned int size;
	unsigned int free;
	struct zone_map *next;
};

extern volume_header vol_hdr;

int mfs_compute_crc (unsigned char *data, unsigned int size, unsigned int off);
int mfs_check_crc (unsigned char *data, unsigned int size, unsigned int off);
void mfs_update_crc (unsigned char *data, unsigned int size, unsigned int off);
char *mfs_device_translate (char *dev);
void data_swab (void *data, int size);
int mfs_add_volume (char *path, int flags);
struct volume_info *mfs_get_volume (unsigned int sector);
int mfs_is_writable (unsigned int sector);
int mfs_volume_size (unsigned int sector);
int mfs_read_data (void *buf, unsigned int sector, int count);
int mfs_write_data (void *buf, unsigned int sector, int count);
unsigned int mfs_inode_count ();
unsigned int mfs_inode_to_sector (unsigned int inode);
mfs_inode *mfs_read_inode (unsigned int inode);
mfs_inode *mfs_read_inode_by_fsid (unsigned int fsid);
int mfs_read_inode_data_part (mfs_inode * inode, unsigned char *data, unsigned int start, unsigned int count);
unsigned char *mfs_read_inode_data (mfs_inode * inode, int *size);
int mfs_add_volume_pair (char *app, char *media, unsigned int minalloc);
void mfs_cleanup_volumes ();
int mfs_load_volume_header (int flags);
void mfs_cleanup_zone_maps ();
int mfs_load_zone_maps ();
int mfs_init (int flags);
int mfs_readwrite_init ();
int mfs_reinit (int flags);
void mfs_cleanup ();

#define MFS_check_crc(data, size, crc) (mfs_check_crc ((unsigned char *)(data), (size), (unsigned int *)&(crc) - (unsigned int *)(data)))
#define MFS_update_crc(data, size, crc) (mfs_update_crc ((unsigned char *)(data), (size), (unsigned int *)&(crc) - (unsigned int *)(data)))

#endif	/* MFS_H */
