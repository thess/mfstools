#define TIVO_BOOT_MAGIC         0x1492
#define TIVO_BOOT_AMIGC         0x9214
#define MAC_PARTITION_MAGIC     0x504d

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
			unsigned int sectors;
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
	unsigned int sectors;
	unsigned int start;
	unsigned int refs;
	char *name;
	char *type;
	struct tivo_partition_table *table;
};

/* TiVo partition map information */
struct tivo_partition_table
{
	unsigned char *device;
	int ro_fd;
	int rw_fd;
	int vol_flags;
	int count;
	int allocated;
	int refs;
	unsigned int devsize;
	struct tivo_partition *partitions;
	struct tivo_partition_table *next;
	struct tivo_partition_table *parent;
};

/* From macpart.c */
tpFILE *tivo_partition_open (char *device, int flags);
tpFILE *tivo_partition_open_direct (char *device, int partnum, int flags);
int tivo_partition_count (const char *device);
void tivo_partition_close (tpFILE * file);
unsigned int tivo_partition_size (tpFILE * file);
unsigned int tivo_partition_sizeof (const char *device, int partnum);
char *tivo_partition_name (const char *device, int partnum);
char *tivo_partition_type (const char *device, int partnum);
unsigned int tivo_partition_offset (tpFILE * file);
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
int tivo_partition_add (const char *device, unsigned int size, int before, const char *name, const char *type);
int tivo_partition_table_write (const char *device);

/* From readwrite.c */
int tivo_partition_read (tpFILE * file, void *buf, unsigned int sector, int count);
int tivo_partition_write (tpFILE * file, void *buf, unsigned int sector, int count);

#ifndef EXTERNINLINE
#if DEBUG
#define EXTERNINLINE static inline
#else
#define EXTERNINLINE extern inline
#endif
#endif

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
