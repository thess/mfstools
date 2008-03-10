#ifndef MFS_H
#define MFS_H

#include "volume.h"
#include "zonemap.h"
#include "fsid.h"

typedef struct volume_header_s
{
	unsigned int off00;
	unsigned int abbafeed;
	unsigned int checksum;
	unsigned int off0c;
	unsigned int root_fsid;		/* Maybe? */
	unsigned int off14;
	unsigned int off18;
	unsigned int off1c;
	unsigned int off20;
	unsigned char partitionlist[128];
	unsigned int total_sectors;
	unsigned int offa8;
	unsigned int logstart;
	unsigned int lognsectors;
	unsigned int logstamp;
	unsigned int unkstart;		/* Not sure what it's used for */
	unsigned int unksectors;	/* But definately an allocated area */
	unsigned int offc0;
	zone_map_ptr zonemap;
	unsigned int offd8;
								/* Following two used in transaction log */
								/* And inodes */
	unsigned int bootcycles;	/* Seems to be times booted on */
	unsigned int bootsecs;		/* Seems to be seconds since boot */
	unsigned int offe4;
}
volume_header;

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

struct mfs_handle
{
	struct volume_handle *vols;
	volume_header vol_hdr;
	struct zone_map_head zones[ztMax];
	struct zone_map *loaded_zones;

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
unsigned int mfs_inode_count (struct mfs_handle *mfshnd);
unsigned int mfs_inode_to_sector (struct mfs_handle *mfshnd, unsigned int inode);
mfs_inode *mfs_read_inode (struct mfs_handle *mfshnd, unsigned int inode);
mfs_inode *mfs_read_inode_by_fsid (struct mfs_handle *mfshnd, unsigned int fsid);
int mfs_write_inode (struct mfs_handle *mfshnd, mfs_inode *inode);
int mfs_read_inode_data_part (struct mfs_handle *mfshnd, mfs_inode * inode, unsigned char *data, unsigned int start, unsigned int count);
unsigned char *mfs_read_inode_data (struct mfs_handle *mfshnd, mfs_inode * inode, int *size);
int mfs_add_volume_pair (struct mfs_handle *mfshnd, char *app, char *media, unsigned int minalloc);
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

#define CRC32_RESIDUAL 0xdebb20e3

#define MFS_check_crc(data, size, crc) (mfs_check_crc ((unsigned char *)(data), (size), (unsigned int *)&(crc) - (unsigned int *)(data)))
#define MFS_update_crc(data, size, crc) (mfs_update_crc ((unsigned char *)(data), (size), (unsigned int *)&(crc) - (unsigned int *)(data)))

#define mfs_read_data(mfshnd,buf,sector,count) mfsvol_read_data ((mfshnd)->vols, buf, sector, count)
#define mfs_write_data(mfshnd,buf,sector,count) mfsvol_write_data ((mfshnd)->vols, buf, sector, count)
#define mfs_volume_size(mfshnd,sector) mfsvol_volume_size ((mfshnd)->vols, sector)
#define mfs_volume_set_size(mfshnd) mfsvol_volume_set_size ((mfshnd)->vols)

#endif	/* MFS_H */
