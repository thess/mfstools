#define TIVO_BOOT_MAGIC         0x1492
#define TIVO_BOOT_AMIGC         0x9214
#define MAC_PARTITION_MAGIC     0x504d

/* Format of mac partition table. */
struct mac_partition
{
	__u16 signature;
	__u16 res1;
	__u32 map_count;
	__u32 start_block;
	__u32 block_count;
	char name[32];
	char type[32];
	__u32 data_start;
	__u32 data_count;
	__u32 status;
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

#define VOL_FILE 1
#define VOL_SWAB 4

/* TiVo partition map partition */
struct tivo_partition
{
	unsigned int sectors;
	unsigned int start;
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
	struct tivo_partition *partitions;
	struct tivo_partition_table *next;
};

/* From macpart.c */
tpFILE *tivo_partition_open (char *path, int flags);
tpFILE *tivo_partition_open_direct (char *path, int partnum, int flags);
int tivo_partition_count (char *path);
void tivo_partition_close (tpFILE * file);
unsigned int tivo_partition_size (tpFILE * file);
unsigned int tivo_partition_sizeof (char *device, int partnum);
unsigned int tivo_partition_offset (tpFILE * file);
const char *tivo_partition_device_name (tpFILE * file);
void tivo_partition_direct ();
void tivo_partition_file ();
void tivo_partition_auto ();

/* There is no write bootsector on purpose. */
int tivo_partition_read_bootsector (char *device, void *buf);

/* From readwrite.c */
int tivo_partition_read (tpFILE * file, void *buf, unsigned int sector, int count);
int tivo_partition_write (tpFILE * file, void *buf, unsigned int sector, int count);

/* Some quick routines, mainly intended for internal macpart use. */
extern inline int
_tivo_partition_fd (tpFILE * file)
{
	return file->fd;
}
extern inline int
_tivo_partition_isdevice (tpFILE * file)
{
	return (file->tptype == pDIRECT || file->tptype == pDEVICE);
}
extern inline int
_tivo_partition_swab (tpFILE * file)
{
	return ((file->tptype == pDIRECT || file->tptype == pDIRECTFILE) && file->extra.direct.pt->vol_flags & VOL_SWAB);
}
