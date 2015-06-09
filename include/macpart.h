#ifndef MACPART_H
#define MACPART_H

#include "util.h"

#define TIVO_BOOT_MAGIC         0x1492
#define TIVO_BOOT_AMIGC         0x9214
#define MAC_PARTITION_MAGIC     0x504d
#define MAC_PARTITION_AMIGC     0x4d50
#define TIVO_BIGPARTITION_MAGIC 0x504E
#define TIVO_BIGPARTITION_AMIGC 0x4E50

/* Format of mac partition table. */
struct mac_partition
{
	uint16_t signature;
	uint16_t res1;
	uint32_t map_count;
	uint32_t start_block;
	uint32_t block_count;
	char name[32];
	char type[32];
	uint32_t data_start;
	uint32_t data_count;
	uint32_t status;
};

/*
 * The TiVo partition structure is (quite obviously) derived from
 * the PowerMac structure, but expands the LBA-related fields from
 * 32 bits to 64 in order to allow addressing of disks which
 * fall in the multi-terabyte storage class.
 */

struct tivo_bigpartition {
	uint16_t signature;	/* expected to be TIVO_BIGPARTITION_MAGIC */
	uint16_t res1;
	uint32_t map_count;	/* # blocks in partition map */
	uint64_t start_block;	/* absolute starting block # of partition */
	uint64_t block_count;	/* number of blocks in partition */
	char name[32];	/* partition name */
	char type[32];	/* string type description */
	uint64_t data_start;	/* rel block # of first data block */
	uint64_t data_count;	/* number of data blocks */
	uint64_t boot_start;
	uint64_t boot_size;
	uint64_t boot_load;
	uint64_t boot_load2;
	uint64_t boot_entry;
	uint64_t boot_entry2;
	uint32_t boot_cksum;
	uint32_t status;		/* partition status bits */
	char	processor[16];	/* identifies ISA of boot */
	/* there is more stuff after this that we don't need */
};

typedef struct tivo_partition_file
{
	enum
	{ pUNKNOWN = 0, pFILE, pDEVICE, pDIRECTFILE, pDIRECT }
	tptype;
	int fd;
/* Only for pDIRECT and friend. */
	union
	{
		struct
		{
			struct tivo_partition_table *pt;
			struct tivo_partition *part;
		}
		direct;
		struct
		{
			uint64_t sectors;
		}
		kernel;
	}
	extra;
}
tpFILE;

#define VOL_FILE	0x00000001
#define VOL_SWAB	0x00000004
#define VOL_DIRTY	0x00000008
#define VOL_NONINIT	0x00000010
#define VOL_VALID	0x00000020

/* TiVo partition map partition */
struct tivo_partition
{
	uint64_t sectors;
	uint64_t start;
	unsigned int refs;
	char *name;
	char *type;
	struct tivo_partition_table *table;
};

/* TiVo partition map information */
struct tivo_partition_table
{
	char *device;
	int ro_fd;
	int rw_fd;
	int vol_flags;
	int count;
	int refs;
	uint64_t devsize;
	int allocated;
	struct tivo_partition *partitions;
	struct tivo_partition_table *next;
	struct tivo_partition_table *parent;
};

/* From macpart.c */
tpFILE *tivo_partition_open (char *device, int flags);
tpFILE *tivo_partition_open_direct (char *device, int partnum, int flags);
int tivo_partition_count (const char *device);
void tivo_partition_close (tpFILE * file);
uint64_t tivo_partition_size (tpFILE * file);
uint64_t tivo_partition_sizeof (const char *device, int partnum);
uint64_t tivo_partition_total_free (const char *device);
char *tivo_partition_name (const char *device, int partnum);
char *tivo_partition_type (const char *device, int partnum);
uint64_t tivo_partition_offset (tpFILE * file);
const char *tivo_partition_device_name (tpFILE * file);
int tivo_partition_rrpart (const char *device);
void tivo_partition_direct ();
void tivo_partition_file ();
void tivo_partition_auto ();

char *
tivo_partition_type (const char *device, int partnum);

int tivo_partition_swabbed (const char *device);
int tivo_partition_devswabbed (const char *device);

int tivo_partition_read_bootsector (const char *device, void *buf);
int tivo_partition_write_bootsector (const char *device, void *buf);

int tivo_partition_table_init (const char *device, int swab);
int tivo_partition_add (const char *device, uint64_t size, int before, const char *name, const char *type);
int tivo_partition_table_write (const char *device);

uint64_t tivo_partition_largest_free (const char *device);

/* From readwrite.c */
int tivo_partition_read (tpFILE * file, void *buf, uint64_t sector, int count);
int tivo_partition_write (tpFILE * file, void *buf, uint64_t sector, int count);
int tivo_partition_rename (const char *device, int partition, const char *name);

/* Some quick routines, mainly intended for internal macpart use. */
EXTERNINLINE int
_tivo_partition_fd (tpFILE * file)
{
	return file->fd;
}
EXTERNINLINE int
_tivo_partition_isdevice (tpFILE * file)
{
	return (file->tptype == pDIRECT || file->tptype == pDEVICE);
}
EXTERNINLINE int
_tivo_partition_swab (tpFILE * file)
{
	return ((file->tptype == pDIRECT || file->tptype == pDIRECTFILE) && file->extra.direct.pt->vol_flags & VOL_SWAB);
}
#endif
