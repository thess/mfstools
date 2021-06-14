#ifndef MFS_H
#define MFS_H

struct mfs_handle;

#include "util.h"
#include "volume.h"
#include "zonemap.h"
#include "fsid.h"

#define MFS_MAGIC_OK      0xABBAFEED // mfs filesystem database consistent 
#define MFS_MAGIC_FS_CHK  0x37353033 //(GSOD) Filesystem is inconsistent - cannot mount!  -  Filesystem is inconsistent, will attempt repair!          - Triggered by kickstart 5 7, and others
#define MFS_MAGIC_LOG_CHK 0x37353134 //(GSOD) Filesystem is inconsistent - cannot mount!  -  Filesystem logs are bad - log roll-forward inhibited!     - Triggered by ???
#define MFS_MAGIC_DB_CHK  0x37353135 //(GSOD) Database is inconsistent - cannot mount!    -  fsfix:  mounted MFS volume, starting consistency checks.  - Triggered when a THD beackup with eSata restored to a single drive, without fixing off0c/off14 and trying to remove eSata from UI
#define MFS_MAGIC_CLEAN   0x37353136 // Clean up objects with missing tystreams                                                                        - Triggered after a GSOD encounters bad refcounts or missing media tystreams (eg, truncated restore)
#define MFS_MAGIC_64BIT   0x40000000 // bit is set when mfs is 64-bit

typedef struct volume_header_32_s
{
	// magic and state appear to be 32-bit values in both 32 and 64 bit volume headers.  When the MFS is MSB (premiere and earlier?) the order is state then magic.  On MSB platforms (roamio and later?), the order is reversed.  State is expected to be 0.
	uint32_t magicLSB; // this value is state when the MFS is MSB
	uint32_t magicMSB; // this value is state when the MFS is LSB
	uint32_t checksum;
	uint32_t off0c;
	uint32_t root_fsid;		/* Maybe? */
	uint32_t off14;
	uint32_t firstpartsize;	/* Size of first partition / 1024 sectors */
	uint32_t off1c;
	uint32_t off20;
	char partitionlist[128];
	uint32_t total_sectors;
	uint32_t offa8;
	uint32_t logstart;
	uint32_t lognsectors;
	uint32_t volhdrlogstamp;
	uint32_t unkstart;		/* Not sure what it's used for */
	uint32_t unksectors;	/* But definately an allocated area */
	uint32_t unkstamp;
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
	// magic and state appear to be 32-bit values in both 32 and 64 bit volume headers.  When the MFS is MSB (premiere and earlier?) the order is state then magic.  On MSB platforms (roamio and later?), the order is reversed.  State is expected to be 0.
	uint32_t magicLSB; // this value is state when the MFS is MSB
	uint32_t magicMSB; // this value is state when the MFS is LSB
	uint32_t checksum;
	uint32_t off0c;
	uint32_t root_fsid;		/* Maybe? */
	uint32_t off14;
	uint32_t firstpartsize;	/* Size of first partition / 1024 sectors */
	uint32_t off1c;
	uint32_t off20;
	char partitionlist[132];
	uint64_t total_sectors;
	uint64_t logstart;
	// Roamio inspection also indicate that logstamp is also a 64 bit value.
	//uint32_t offb8;  
	//uint32_t logstamp;
	uint64_t volhdrlogstamp;
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

/* Linked list of runs allocated or freed since the last commit */
/* This only includes frees created by splitting an existing */
/* run.  Including frees created by actually freeing a run wuld not */
/* be transactionally safe to do, since it would result in allocating */
/* (and overwriting) a run with currently live data in it. */
struct zone_changed_run
{
	int bitno;
	int newstate;
	struct zone_changed_run *next;
};

/* Summary of changes to a zone bitmap since last commit */
/* As above, this only includes frees from split runs. */
struct zone_changes
{
	int allocated;
	int freed;
};

/* Linked lists of zone maps for a certain type of map */
struct zone_map
{
	zone_header *map;
	bitmap_header **bitmaps;
	struct zone_changed_run **changed_runs;
	struct zone_changes *changes;
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
	int64_t err_arg1;
	int64_t err_arg2;
	int64_t err_arg3;
};

#define SABLOCKSEC 1630000

// Flags to pass to mfs_init along with the accmode
#define MFS_ERROROK		0x04000000	// Open despite mfs magic being marked inconsistent

void data_swab (void *data, int size);

int mfs_add_volume_pair (struct mfs_handle *mfshnd, char *app, char *media, uint32_t minalloc);
uint64_t mfs_volume_pair_app_size (struct mfs_handle *mfshnd, uint64_t blocks, unsigned int minalloc);
int64_t  mfs_load_volume_header (struct mfs_handle *mfshnd, int flags);
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

int mfs_write_volume_header (struct mfs_handle *mfshnd);

#endif	/* MFS_H */
