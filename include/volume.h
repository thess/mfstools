#ifndef VOLUME_H
#define VOLUME_H

#include "zonemap.h"

typedef struct volume_header_s
{
	unsigned int off00;
	unsigned int abbafeed;
	unsigned int checksum;
	unsigned int off08;
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
	unsigned int offb8;
	unsigned int offbc;
	unsigned int offc0;
	zone_map_ptr zonemap;
	unsigned int offd8;
	unsigned int offdc;
	unsigned int offe0;
	unsigned int offe4;
}
volume_header;

#endif /*VOLUME_H */
