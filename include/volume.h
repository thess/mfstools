#ifndef VOLUME_H
#define VOLUME_H

#include "zonemap.h"

/* Size that TiVo rounds the partitions down to whole increments of. */
#define MFS_PARTITION_ROUND 1024

/* Flags for vol_flags below */
/* #define VOL_FILE        1        This volume is really a file */
#define VOL_RDONLY      2		/* This volume is read-only */
/* #define VOL_SWAB        4        This volume is byte-swapped */

enum volume_write_mode_e
{
	vwNormal = 0,		// Writes go to the volume (If RW mode)
	vwFake = 1,			// Writes pretend to go to the volume, but are hex dumped instead
	vwLocal = 2			// Writes are cached in memory and returned on subsequent reads, but not written to the volume
};

/* Block written to memory */
struct volume_mem_data
{
	unsigned int start;
	unsigned int sectors;
	struct volume_mem_data *next;
	unsigned char data[0];
};

/* Information about the list of volumes needed for reads */
struct volume_info
{
	struct tivo_partition_file *file;
	unsigned int start;
	unsigned int sectors;
	unsigned int offset;
	int vol_flags;
	struct volume_mem_data *mem_blocks;
	struct volume_info *next;
};

struct volume_handle
{
	struct volume_info *volumes;
	enum volume_write_mode_e write_mode;
	char *hda;
	char *hdb;

	char *err_msg;
	void *err_arg1;
	void *err_arg2;
	void *err_arg3;
};

char *mfsvol_device_translate (struct volume_handle *hnd, char *dev);
int mfsvol_add_volume (struct volume_handle *hnd, char *path, int flags);
struct volume_info *mfsvol_get_volume (struct volume_handle *hnd, unsigned int sector);
int mfsvol_is_writable (struct volume_handle *hnd, unsigned int sector);
unsigned int mfsvol_volume_size (struct volume_handle *hnd, unsigned int sector);
unsigned int mfsvol_volume_set_size (struct volume_handle *hnd);
int mfsvol_read_data (struct volume_handle *hnd, void *buf, unsigned int sector, int count);
int mfsvol_write_data (struct volume_handle *hnd, void *buf, unsigned int sector, int count);
void mfsvol_enable_memwrite (struct volume_handle *hnd);
void mfsvol_discard_memwrite (struct volume_handle *hnd);
void mfsvol_cleanup (struct volume_handle *hnd);
struct volume_handle *mfsvol_init (const char *hda, const char *hdb);

void mfsvol_perror (struct volume_handle *hnd, char *str);
int mfsvol_strerror (struct volume_handle *hnd, char *str);
int mfsvol_has_error (struct volume_handle *hnd);
void mfsvol_clearerror (struct volume_handle *hnd);

#endif /*VOLUME_H */
