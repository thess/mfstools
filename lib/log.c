#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <linux/fs.h>
#include <linux/unistd.h>

/* For htonl() */
#include <netinet/in.h>

#include "mfs.h"
#include "log.h"

unsigned int
mfs_log_last_sync ()
{
	return htonl (vol_hdr.logstamp);
}

int
mfs_log_read (void *buf, unsigned int logstamp)
{
	log_hdr *tmp = buf;

	if (mfs_read_data (buf, (logstamp % htonl (vol_hdr.lognsectors)) + htonl (vol_hdr.logstart), 1) != 512)
	{
		return -1;
	}

	if (logstamp != htonl (tmp->logstamp))
	{
		return 0;
	}

	if (!MFS_check_crc (buf, 512, tmp->crc))
	{
		fprintf (stderr, "MFS transaction logstamp %ud has invalid checksum\n", logstamp);
		return 0;
	}

	return 512;
}

int
mfs_log_write (void *buf)
{
	log_hdr *tmp = buf;
	unsigned int logstamp = htonl (tmp->logstamp);

	MFS_update_crc (buf, 512, tmp->crc);

	if (mfs_write_data (buf, (logstamp % htonl (vol_hdr.lognsectors)) + htonl (vol_hdr.logstart), 1) != 512)
	{
		return -1;
	}

	return 512;
}
