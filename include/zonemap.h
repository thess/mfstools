#ifndef ZONEMAP_H
#define ZONEMAP_H

typedef enum zone_type_e
{
	ztInode = 0,
	ztApplication = 1,
	ztMedia = 2,
	ztMax = 3,
	ztPad = 0xffffffff
}
zone_type;

typedef struct bitmap_header_s
{
	unsigned long nbits;		/* Number of bits in this map */
	unsigned long freeblocks;	/* Number of free blocks in this map */
	unsigned long last;			/* Last bit set ??? */
	unsigned long nints;		/* Number of ints in this map */
}
bitmap_header;

typedef struct zone_map_ptr_s
{
	unsigned long sector;		/* Sector of next table */
	unsigned long sbackup;		/* Sector of backup of next table */
	unsigned long length;		/* Length of next table in sectors */
	unsigned long size;			/* Size of partition of next table */
	unsigned long min;			/* Minimum allocation of next table */
}
zone_map_ptr;

typedef struct zone_header_s
{
	unsigned long sector;		/* Sector of this table */
	unsigned long sbackup;		/* Sector of backup of this table */
	unsigned long length;		/* Length of this table in sectors */
	zone_map_ptr next;			/* Next zone map */
	zone_type type;				/* Type of data in zone */
	unsigned long logstamp;		/* Last log stamp */
	unsigned long checksum;		/* Checksum of ??? */
	unsigned long first;		/* First sector in this partition */
	unsigned long last;			/* Last sector in this partition */
	unsigned long size;			/* Size of this partition (sectors) */
	unsigned long min;			/* Minimum allocation size (sectors) */
	unsigned long free;			/* Free space in this partition */
	unsigned long zero;			/* Always zero? */
	unsigned long num;			/* Num of bitmaps.  Followed by num */
	/* addresses, pointing to mmapped */
	/* memory from /tmp/fsmem for bitmaps */
}
zone_header;

/* Size of each bitmap is (nints + (nbits < 8? 1: 2)) * 4 */
/* Don't ask why, thats just the way it is. */
/* In bitmap, MSB is first, LSB last */

#endif /*ZONEMAP_H */
