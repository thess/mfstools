
/* Represents the lowest level bitmap for a zone */
/* This is the opposite order from TiVo bitmaps - bit 0 is LSB */
typedef struct zone_bitmap_s
{
	uint64_t first;
	uint64_t last;
	uint32_t blocksize;
	uint32_t *bits;
	uint32_t *fsids;
	int type;
	struct zone_bitmap_s *next;
} zone_bitmap;
