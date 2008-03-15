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
	unsigned long last;			/* Last bit allocated (Cleared) */
	unsigned long nints;		/* Number of ints in this map */
}
bitmap_header;

typedef struct zone_map_ptr_32_s
{
	uint32_t sector;		/* Sector of next table */
	uint32_t sbackup;		/* Sector of backup of next table */
	uint32_t length;		/* Length of next table in sectors */
	uint32_t size;			/* Size of partition of next table */
	uint32_t min;			/* Minimum allocation of next table */
}
zone_map_ptr_32;

typedef struct zone_map_ptr_64_s
{
	uint64_t sector;		/* Sector of next table */
	uint64_t sbackup;		/* Sector of backup of next table */
	uint64_t length;		/* Length of next table in sectors */
	uint64_t size;			/* Size of partition of next table */
	uint64_t min;			/* Minimum allocation of next table */
}
zone_map_ptr_64;

typedef struct zone_header_32_s
{
	uint32_t sector;		/* Sector of this table */
	uint32_t sbackup;		/* Sector of backup of this table */
	uint32_t length;		/* Length of this table in sectors */
	zone_map_ptr_32 next;	/* Next zone map */
	zone_type type;			/* Type of data in zone */
	uint32_t logstamp;		/* Last log stamp */
	uint32_t checksum;		/* Checksum of entire zone map and bitmaps */
	uint32_t first;			/* First sector in this zone */
	uint32_t last;			/* Last sector in this zone */
	uint32_t size;			/* Size of this zone (sectors) */
	uint32_t min;			/* Minimum allocation size (sectors) */
	uint32_t free;			/* Free space in this zone */
	uint32_t zero;			/* Always zero? */
	uint32_t num;			/* Num of bitmaps.  Followed by num */
	/* addresses, pointing to mmapped */
	/* memory from /tmp/fsmem for bitmaps */
}
zone_header_32;

typedef struct zone_header_64_s
{
	uint64_t sector;			/* Sector of this table */
	uint64_t sbackup;			/* Sector of backup of this table */
	uint64_t next_sector;		/* Sector of next table */
	uint64_t next_sbackup;		/* Sector of backup of next table */
	uint64_t next_size;			/* Size of next zone (sectors) */
	uint64_t first;				/* First sector in this zone */
	uint64_t last;				/* Last sector in this zone */
	uint64_t size;				/* Size of this zone (sectors) */
	uint64_t free;				/* Free space in this zone */
	uint32_t next_length;		/* Length of next table in sectors */
	uint32_t length;			/* Length of this table in sectors */
	uint32_t min;				/* Minimum allocation size (sectors) */
	uint32_t next_min;			/* Minimum allocation size of next zone (sectors) */
	uint32_t logstamp;			/* Last log stamp */
	zone_type type;				/* Type of data in zone */
	uint32_t checksum;			/* Checksum of entire zone map and bitmaps */
	uint32_t zero;				/* Always zero? */
	uint32_t num;				/* Num of bitmaps.  Followed by num */
	/* addresses, pointing to mmapped */
	/* memory from /tmp/fsmem for bitmaps */
}
zone_header_64;

typedef union zone_header_u
{
	zone_header_32 z32;
	zone_header_64 z64;
}
zone_header;

/* Size of each bitmap is (nints + (nbits < 8? 1: 2)) * 4 */
/* Don't ask why, thats just the way it is. */
/* In bitmap, MSB is first, LSB last */

#endif /*ZONEMAP_H */
