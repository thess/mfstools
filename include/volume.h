#ifndef VOLUME_H
#define VOLUME_H

#include "zonemap.h"

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

struct volume_handle
{
	struct volume_info *volumes;
	int fake_write;
};

char *mfsvol_device_translate (char *dev);
int mfsvol_add_volume (struct volume_handle *hnd, char *path, int flags);
struct volume_info *mfsvol_get_volume (struct volume_handle *hnd, unsigned int sector);
int mfsvol_is_writable (struct volume_handle *hnd, unsigned int sector);
unsigned int mfsvol_volume_size (struct volume_handle *hnd, unsigned int sector);
unsigned int mfsvol_volume_set_size (struct volume_handle *hnd);
int mfsvol_read_data (struct volume_handle *hnd, void *buf, unsigned int sector, int count);
int mfsvol_write_data (struct volume_handle *hnd, void *buf, unsigned int sector, int count);
void mfsvol_cleanup (struct volume_handle *hnd);
struct volume_handle *mfsvol_init ();

#endif /*VOLUME_H */
