#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <malloc.h>
#include <sys/types.h>
#include <asm/types.h>
#include <fcntl.h>
#include <zlib.h>
#include <linux/fs.h>
#include <sys/param.h>
#include <string.h>

#include "mfs.h"
#include "macpart.h"

#define RESTORE
#include "backup.h"

static inline void
convendian32 (unsigned int *var)
{
	register int tmp = *var;

	tmp = (tmp << 16) | (tmp >> 16);
	tmp = ((tmp && 0xff00ff00) >> 8) | ((tmp << 8) & 0xff00ff00);
	*var = tmp;
}

/*************************************************/
/* Initializes the backup structure for restore. */
struct backup_info *
init_restore (unsigned int flags)
{
	struct backup_info *info;

	flags &= RF_FLAGS;

	info = calloc (sizeof (*info), 1);

	if (!info)
	{
		return 0;
	}

	info->presector = 1;
	info->nsectors = 1;
	info->back_flags = flags;
	return info;
}

void
restore_set_varsize (struct backup_info *info, int size)
{
	info->varsize = size;
}

void
restore_set_swapsize (struct backup_info *info, int size)
{
	info->swapsize = size;
}

/*****************************************************************************/
/* Return the next sectors in the backup.  This is where all the data in the */
/* backup originates.  If it's backed up, it came from here.  This only */
/* reads the data from the info structure.  Compression is handled */
/* elsewhere. */
static unsigned int
restore_next_sectors (struct backup_info *info, char *buf, int sectors)
{
	unsigned int retval = 0;

/* If there is nothing to do, do nothing.  How zen. */
	if (sectors <= 0)
	{
		return 0;
	}

/* If there is something to do, keep doing it until there is nothing.  How */
/* materialistic. */
	while (sectors > 0)
	{
/* If the sector number is 0 or greater, the it is the actual data.  If it */
/* is below zero, it is the header. */
		if (info->cursector - info->presector >= 0)
		{
			int cursector = info->cursector - info->presector;
			int loop = 0;

/* If the restore has not been initialized, don't do anything. */
			if (!(info->back_flags & RF_INITIALIZED))
			{
				return retval;
			}

			if (cursector == 0)
			{
/* Handle boot sector restore */
				int loop;

				for (loop = 0; loop < info->ndevs; loop++)
				{
					if (loop > 0 && info->devs[loop].swab != info->devs[loop - 1].swab || loop == 0 && info->devs[loop].swab)
						data_swab (buf, 512);
					lseek (info->devs[loop].fd, 0, SEEK_SET);
					write (info->devs[loop].fd, buf, 512);
				}
				if (info->devs[loop - 1].swab)
					data_swab (buf, 512);
				sectors -= 1;
				buf += 512;
				info->cursector += 1;
				retval += 1;
			}
			else
				cursector--;

/* Step through the partitions. */
			for (loop = 0; loop < info->nparts && sectors > 0; loop++)
			{
/* If the size of this partition doesn't account for the current sector, */
/* just track it and move on. */
				if (info->parts[loop].sectors <= cursector)
				{
					cursector -= info->parts[loop].sectors;
				}
				else
				{
					tpFILE *file;
					int tocopy = info->parts[loop].sectors - cursector;

/* The partition is larger than the buffer, read as much as possible. */
					if (tocopy > sectors)
					{
						tocopy = sectors;
					}

/* Get the file for this partition from the info structure. */
					file = info->devs[info->parts[loop].devno].files[info->parts[loop].partno - 1];

/* If the file isn't opened, open it. */
					if (!file)
					{
						file = tivo_partition_open_direct (info->devs[info->parts[loop].devno].devname, info->parts[loop].partno, O_RDWR);
/* The sick part, is most of this line is an lvalue. */
						info->devs[info->parts[loop].devno].files[info->parts[loop].partno - 1] = file;

/* If the file still isn't open, there is an error. */
						if (!file)
						{
							info->lasterr = "Error restoring partitions.";
							return -1;
						}
					}

/* Read the data. */
					if (tivo_partition_write (file, buf, cursector, tocopy) < 0)
					{
						info->lasterr = "Error restoring partitions.";
						return -1;
					}

					buf += tocopy * 512;
/* At this point, it is either the end of the buffer, at which point */
/* cursector doesn't matter, or it is the end of this partition, at which */
/* point, cursector should be 0. */
					cursector = 0;
					sectors -= tocopy;
					retval += tocopy;
					info->cursector += tocopy;
				}
			}

			if (sectors > 0 && cursector == 0)
			{
/* Bootstrap MFS by hand.  The macpart should be initialized by now. */
				int loop;

				for (loop = 0; loop < info->nmfs; loop++)
				{
					char devname[MAXPATHLEN];
					int devno = info->mfsparts[loop].devno;
					int partno = info->mfsparts[loop].partno;

					sprintf (devname, "%s%d", info->devs[devno].devname, partno);
					mfs_add_volume (devname, O_RDWR);
				}
			}

/* There are no partitions left.  Check the block list. */
			for (loop = 0; loop < info->nblocks && sectors > 0; loop++)
			{
/* If the current sector being backed up is beyond this block, just chip */
/* away at it and keep going. */
				if (info->blocks[loop].sectors <= cursector)
				{
					cursector -= info->blocks[loop].sectors;
				}
				else
				{
					int tocopy = info->blocks[loop].sectors - cursector;

/* Back up as much as possible.  If that exceeds the buffer size, get to the */
/* end of the buffer. */
					if (tocopy > sectors)
					{
						tocopy = sectors;
					}

/* Read the data. */
					if (mfs_write_data (buf, info->blocks[loop].firstsector + cursector, tocopy) < 0)
					{
						info->lasterr = "Error restoring MFS data.";
						return -1;
					}

					buf += tocopy * 512;
/* At this point, it is either the end of the buffer, at which point */
/* cursector doesn't matter, or it is the end of this block, at which */
/* point, cursector should be 0. */
					cursector = 0;
					sectors -= tocopy;
					retval += tocopy;
					info->cursector += tocopy;
				}
			}
			return retval;
/* In theory at this point the data is all written. */
		}
		else
		{
/* The cursector starts 1 further away from 0 than there are presectors. */
/* This is for the header.  Not too interesting here, but other places, the */
/* header is not compressed, while the rest of the backup is. */
			if (info->cursector > 0)
			{
/* This is part of the block list in the header. */
				int presector = info->cursector - 1;
				int curoff = presector * 512;
				int headerused = 0;
				int cursize;

/* First, the list of partitions.  The array in memory will be copied */
/* directly to the array on disk. */
				cursize = info->nparts * sizeof (struct backup_partition);
				if (curoff < cursize + headerused)
				{
					int needed_space = headerused + cursize - curoff;
					int have_space = sectors * 512 - curoff;

					if (needed_space > have_space)
					{
						needed_space = have_space;
					}

					memcpy ((char *)info->parts + curoff - headerused, buf, needed_space);

					buf += needed_space;

					curoff += needed_space;
					needed_space = curoff;

					while (presector < curoff / 512)
					{
						sectors--;
						retval++;
						presector++;
						needed_space -= 512;
						info->cursector++;
					}
				}
				headerused += cursize;

				cursize = info->nblocks * sizeof (struct backup_block);
				if (sectors > 0 && curoff < cursize * headerused)
				{
					int needed_space = headerused + cursize - curoff;
					int have_space = sectors * 512 - curoff;

					if (needed_space > have_space)
					{
						needed_space = have_space;
					}

					memcpy ((char *)info->blocks + curoff - headerused, buf, needed_space);

					buf += needed_space;

					curoff += needed_space;
					needed_space = curoff;

					while (presector < curoff / 512)
					{
						sectors--;
						retval++;
						presector++;
						needed_space -= 512;
						info->cursector++;
					}
				}
				headerused += cursize;

				cursize = info->nmfs * sizeof (struct backup_partition);
				if (sectors > 0 && curoff < cursize * headerused)
				{
					int needed_space = headerused + cursize - curoff;
					int have_space = sectors * 512 - curoff;

					if (needed_space > have_space)
					{
						needed_space = have_space;
					}

					memcpy ((char *)info->mfsparts + curoff - headerused, buf, needed_space);

					buf += needed_space;

					curoff += needed_space;
					needed_space = curoff;

					while (presector < curoff / 512)
					{
						sectors--;
						retval++;
						presector++;
						needed_space -= 512;
						info->cursector++;
					}
				}
				headerused += cursize;

				if (headerused == curoff)
				{
					int loop;

					if (info->back_flags & RF_ENDIAN)
					{
						for (loop = 0; loop < info->nparts; loop++)
						{
							convendian32 (&info->parts[loop].sectors);
						}

						for (loop = 0; loop < info->nblocks; loop++)
						{
							convendian32 (&info->blocks[loop].firstsector);
							convendian32 (&info->blocks[loop].sectors);
						}

						for (loop = 0; loop < info->nmfs; loop++)
						{
							convendian32 (&info->mfsparts[loop].sectors);
						}
					}

					if (headerused & 511)
					{
						buf += 512 - (headerused & 511);
						sectors--;
						retval++;
						presector++;
						info->cursector++;
					}
				}
			}
			else
			{
				struct backup_head *head = (struct backup_head *)buf;
	
				switch (head->magic)
				{
				case TB_MAGIC:
					break;
				case TB_ENDIAN:
					info->back_flags |= RF_ENDIAN;
					break;
				default:
					info->lasterr = "Unknown backup format.";
					return -1;
				}

				if (info->back_flags & RF_ENDIAN)
					convendian32 (&info->back_flags);
				info->back_flags |= head->flags;
				info->nsectors = head->nsectors;
				info->nparts = head->nparts;
				info->nblocks = head->nblocks;
				info->nmfs = head->mfspairs;

				if (head->magic == TB_ENDIAN)
				{
					convendian32 (&info->back_flags);
					convendian32 (&info->nsectors);
					convendian32 (&info->nparts);
					convendian32 (&info->nblocks);
					convendian32 (&info->nmfs);
				}

				info->presector = (info->nblocks * sizeof (struct backup_block) + info->nparts * sizeof (struct backup_partition) + info->nmfs * sizeof (struct backup_partition) + 511) / 512 + 1;

				info->parts = calloc (sizeof (struct backup_partition), info->nparts);
				info->blocks = calloc (sizeof (struct backup_block), info->nblocks);
				info->mfsparts = calloc (sizeof (struct backup_partition), info->nmfs);

				if (!info->parts || !info->blocks || !info->mfsparts)
				{
					if (info->parts)
						free (info->parts);
					info->parts = 0;
					info->nparts = 0;
					if (info->blocks)
						free (info->blocks);
					info->blocks = 0;
					info->nblocks = 0;
					if (info->mfsparts)
						free (info->mfsparts);
					info->mfsparts = 0;
					info->nmfs = 0;

					info->lasterr = "Memory exhausted.";

					return -1;
				}

				retval += 1;
				sectors -= 1;
				info->cursector += 1;
				buf += 512;
			}
		}
	}

	return retval;
}

/*************************************************************************/
/* Pass the data to the front-end program.  This handles compression and */
/* all that fun stuff. */
unsigned int
restore_write (struct backup_info *info, char *buf, unsigned int size)
{
	unsigned int retval = 0;

	if (info->back_flags & BF_COMPRESSED)
	{
/* The first sector is never compressed.  But thats okay, because the backup */
/* flags will have not been read yet. */
		if (!info->comp)
		{
			return retval;
		}

		info->comp->avail_in = size;
		info->comp->next_in = buf;
		while (info->comp && info->comp->avail_in > 0 || (info->back_flags & RF_NOMORECOMP) && (unsigned int)info->comp->next_out - (unsigned int)info->comp_buf > 512)
		{
			if ((unsigned int)info->comp->next_out - (unsigned int)info->comp_buf > 512)
			{
				int nread = restore_next_sectors (info, info->comp_buf, ((unsigned int)info->comp->next_out - (unsigned int)info->comp_buf) / 512);
				if (nread < 0)
				{
					return -1;
				}
				if (nread == 0)
				{
					break;
				}

				nread *= 512;
				if ((unsigned int)info->comp->next_out - (unsigned int)info->comp_buf > nread)
				{
					int nleft = (unsigned int)info->comp->next_out - (unsigned int)info->comp_buf - nread;

					memmove (info->comp_buf, info->comp->next_out - nleft, nleft);
				}
				info->comp->avail_out += nread;
				info->comp->next_out -= nread;
			}
			else if (!(info->back_flags & RF_NOMORECOMP))
			{
				int zres = inflate (info->comp, 0);

				if (zres == Z_STREAM_END)
				{
					info->back_flags |= RF_NOMORECOMP;
					continue;
				}

				if (zres != Z_OK)
				{
					info->lasterr = "Decompression error.";
					return -1;
				}
			}
			else
			{
				break;
			}
		}
		if (info->comp)
		{
			retval += size - info->comp->avail_in;
		}
	}
	else
	{
		if (size < 512)
		{
			info->lasterr = "Internal error 3.";
			return -1;
		}

		if (info->cursector == 0)
		{
			int nwrit = restore_next_sectors (info, buf, 1);
			if (nwrit != 1)
				return -1;

			size -= 512;
			buf += 512;
			retval += 512;

			if (info->back_flags & BF_COMPRESSED)
			{
				info->comp_buf = calloc (2048, 512);
				if (!info->comp_buf)
				{
					info->lasterr = "Memory exhausted.";
					return -1;
				}
				info->comp = calloc (sizeof (*info->comp), 1);
				if (!info->comp)
				{
					free (info->comp_buf);
					info->lasterr = "Memory exhausted.";
					return -1;
				}

				info->comp->zalloc = Z_NULL;
				info->comp->zfree = Z_NULL;
				info->comp->opaque = Z_NULL;
				info->comp->next_in = Z_NULL;
				info->comp->avail_in = 0;
				info->comp->next_out = info->comp_buf;
				info->comp->avail_out = 512 * 2048;

				if (inflateInit (info->comp) != Z_OK)
				{
					free (info->comp_buf);
					free (info->comp);
					info->lasterr = "Deompression error.";
					return -1;
				}

				if (size > 0)
				{
					return retval + restore_write (info, buf, size);
				}
			}
			else
			{
				if (size < 512)
				{
					return retval;
				}
			}
		}
		return retval + restore_next_sectors (info, buf, size / 512) * 512;
	}

	return retval;
}

static int
find_optimal_partitions (struct backup_info *info, unsigned int min1, unsigned int secs1, unsigned int secs2)
{
/* a12a13 a14a15 b2b3 b4b5 b6b7 b8b9 b10b11 b12b13 b14b15 */
	int bestorder = -1;
	unsigned int bestleft = secs1 - min1;
	int loop, loop2, loop3;
	int count;
	char *err = 0;

	for (loop = 0; loop < (1 << (info->nmfs / 2 - 1)); loop++)
	{
		int max1 = 11;
		int max2 = 1;
		int free1 = secs1 - min1;
		int free2 = secs2 - 64;

		for (loop2 = 1 << (info->nmfs / 2 - 1), loop3 = 2; loop2; loop2 >>= 1, loop3 += 2)
		{
			if (loop & loop2)
			{
				max1 += 2;
				free1 -= info->mfsparts[loop3].sectors + info->mfsparts[loop3 + 1].sectors;
			}
			else
			{
				max2 += 2;
				free2 -= info->mfsparts[loop3].sectors + info->mfsparts[loop3 + 1].sectors;
			}
		}

		if (max1 < 16 && max2 < 16 && free1 >= 0 && free2 >= 0 && free1 <= bestleft)
		{
			if ((max1 - 9) * 10 + max2 * 10 + (max2 & 7) <= 128)
			{
				bestorder = loop;
				bestleft = free1;
			}
			else
				err = "Too many MFS partitions.";
		}
	}

	if (bestorder < 0)
	{
		info->lasterr = err? err: "Unable to fit backup onto drives.";
		return -1;
	}

	count = 11;
	for (loop = 1 << (info->nmfs / 2 - 1), loop2 = 2, loop3 = 12; loop; loop >>= 1, loop2 += 2)
	{
		if (bestorder & loop2)
		{
			info->newparts[count].devno = 0;
			info->mfsparts[loop2].devno = 0;
			info->newparts[count].partno = loop3;
			info->mfsparts[loop2].partno = loop3;
			info->newparts[count].sectors = info->mfsparts[loop2].sectors;
			info->newparts[count + 1].devno = 0;
			info->mfsparts[loop2 + 1].devno = 0;
			info->newparts[count + 1].partno = loop3 + 1;
			info->mfsparts[loop2 + 1].partno = loop3 + 1;
			info->newparts[count + 1].sectors = info->mfsparts[loop2 + 1].sectors;
			count += 2;
			loop3 += 2;
			info->devs[0].nparts += 2;
		}
	}

	for (loop = 1 << (info->nmfs / 2 - 1), loop2 = 2, loop3 = 2; loop; loop >>= 1, loop2 += 2)
	{
		if (!(bestorder & loop2))
		{
			info->newparts[count].devno = 1;
			info->mfsparts[loop2].devno = 1;
			info->newparts[count].partno = loop3;
			info->mfsparts[loop2].partno = loop3;
			info->newparts[count].sectors = info->mfsparts[loop2].sectors;
			info->newparts[count + 1].devno = 1;
			info->mfsparts[loop2 + 1].devno = 1;
			info->newparts[count + 1].partno = loop3 + 1;
			info->mfsparts[loop2 + 1].partno = loop3 + 1;
			info->newparts[count + 1].sectors = info->mfsparts[loop2 + 1].sectors;
			count += 2;
			loop3 += 2;
			info->devs[1].nparts += 2;
		}
	}

	return 0;
}

int
restore_trydev (struct backup_info *info, char *dev1, char *dev2)
{
	int fd1 = -1;
	int fd2 = -1;
	unsigned int secs1 = 0;
	unsigned int secs2 = 0;
	unsigned int min1 = 0;
	unsigned int count;
	int loop;

	if (info->back_flags & RF_INITIALIZED || info->cursector < info->presector)
	{
		info->lasterr = "Internal error 4.";
		return -1;
	}
	if (!dev1)
	{
		info->lasterr = "No backup device.";
		return -1;
	}
	if (info->nmfs & 1 == 1)
	{
		info->lasterr = "Internal error 5.";
		return -1;
	}

	fd1 = open (dev1, O_RDWR);
	if (fd1 < 0)
	{
		info->lasterr = "Unable to open destination device for writing.";
		return -1;
	}

	if (ioctl (fd1, BLKGETSIZE, &secs1) != 0)
	{
		info->lasterr = "Destination is not a device.";
		close (fd1);
		return -1;
	}

	if (dev2)
	{
		fd2 = open (dev2, O_RDWR);
		if (fd2 < 0)
		{
			info->lasterr = "Unable to open second device.";
			close (fd1);
			return -1;
		}

		if (ioctl (fd2, BLKGETSIZE, &secs2) != 0)
		{
			info->lasterr = "Second restore target is not a device.";
			close (fd1);
			close (fd2);
			return -1;
		}
	}

	for (loop = 0, count=0; loop < info->nparts; loop++)
	{
		if (info->parts[loop].devno != 0 || info->parts[loop].partno < 2)
		{
			close (fd1);
			if (fd2 >= 0)
				close (fd2);
			info->lasterr = "Format error in backup file.";
			return (-1);
		}

		if (info->parts[loop].partno > 7 && info->parts[loop].partno == 9)
		{
			if (info->varsize && info->varsize != info->parts[loop].sectors)
			{
				info->lasterr = "Varsize in backup mis-matches requested varsize.";
				close (fd1);
				if (fd2 >= 0)
					close (fd2);
				return -1;
			}

			info->varsize = info->parts[loop].sectors;
		} else {
			count++;
			min1 += info->parts[loop].sectors;
		}

		if (count == 3)
		{
			min1 *= 2;
		}
	}

	if (info->swapsize == 0)
	{
		info->swapsize = 64 * 1024 * 2;
	}
	if (info->varsize == 0)
	{
		info->varsize = 128 * 1024 * 2;
	}

/* Boot sector, partition table, swap, var, mfs set 1 */
	min1 += 1 + 63 + info->swapsize * info->varsize + info->mfsparts[0].sectors + info->mfsparts[1].sectors;

	if (min1 > secs1)
	{
		info->lasterr = "First target drive too small.";
		close (fd1);
		if (fd2 >= 0)
			close (fd2);
		return -1;
	}

	if (info->newparts == NULL)
	{
		info->nnewparts = 9 + info->nmfs;
		info->newparts = calloc (info->nnewparts, sizeof (struct backup_partition));
		if (!info->newparts)
		{
			info->lasterr = "Memory exhausted.";
			close (fd1);
			if (fd2 >= 0)
				close (fd2);
			return -1;
		}
	}

	for (count = min1, loop = 2; loop < info->nmfs; loop++)
	{
		count += info->mfsparts[loop].sectors;
	}

	bzero (info->newparts, info->nnewparts * sizeof (struct backup_partition));
	info->newparts[0].partno = 1;
	info->newparts[0].sectors = 63;
	info->newparts[1].partno = 2;
	info->newparts[1].sectors = 4096;
	info->newparts[2].partno = 3;
	info->newparts[2].sectors = 4096;
	info->newparts[3].partno = 4;
	info->newparts[3].sectors = 128 * 1024 * 2;
	info->newparts[4].partno = 5;
	info->newparts[4].sectors = 4096;
	info->newparts[5].partno = 6;
	info->newparts[5].sectors = 4096;
	info->newparts[6].partno = 7;
	info->newparts[6].sectors = 128 * 1024 * 2;
	info->newparts[7].partno = 8;
	info->newparts[7].sectors = info->swapsize;
	info->newparts[8].partno = 9;
	info->newparts[8].sectors = info->varsize;
	for (loop = 0; loop < info->nparts; loop++)
	{
		info->newparts[info->parts[loop].partno - 1] = info->parts[loop];
	}
	info->newparts[9].partno = 10;
	info->newparts[9].sectors = info->mfsparts[0].sectors;
	info->newparts[10].partno = 11;
	info->newparts[10].sectors = info->mfsparts[1].sectors;

	if (count <= secs1)
	{
		if (fd2 >= 0)
			close (fd2);

		info->ndevs = 1;
		info->devs = calloc (1, sizeof (struct device_info));

		if (!info->devs)
		{
			info->lasterr = "Memory exhausted.";
			close (fd1);
			return -1;
		}

		info->devs->fd = fd1;
		info->devs->nparts = info->nnewparts;
		info->devs->devname = dev1;
		info->devs[0].sectors = secs1;

		for (loop = 0; loop < info->nmfs; loop++)
		{
			info->mfsparts[loop].devno = 0;
			info->mfsparts[loop].partno = 10 + loop;
			info->newparts[loop + 9].sectors = info->mfsparts[loop].sectors;
			info->newparts[loop + 9].partno = loop + 10;
		}

		return 1;
	}

	if (fd2 < 0)
	{
		info->lasterr = "Backup target not large enough for entire backup by itself.";
		close (fd1);
		return -1;
	}

	info->ndevs = 2;
	info->devs = calloc (2, sizeof (struct device_info));

	if (!info->devs)
	{
		close (fd1);
		close (fd2);
		info->lasterr = "Memory exhausted.";
		return -1;
	}

	info->devs[0].fd = fd1;
	info->devs[0].nparts = 11;
	info->devs[0].devname = dev1;
	info->devs[0].sectors = secs1;
	info->devs[1].fd = fd2;
	info->devs[1].nparts = 1;
	info->devs[1].devname = dev2;
	info->devs[1].sectors = secs2;

	if (find_optimal_partitions (info, min1, secs1, secs2) < 0)
	{
		free (info->devs);
		info->devs = 0;
		close (fd1);
		close (fd2);
		return -1;
	}

	return 1;
}

int
scan_swab (char *devname)
{
	int fd, tmp;
	char tempfile[MAXPATHLEN];
	char *tmp2;
	char buf[4096];
	int retval = 0;

	devname = strrchr (devname, '/');

	if (!devname)
		return 0;

	sprintf (tempfile, "/proc/ide/%s/settings", devname + 1);

	fd = open (tempfile, O_RDONLY);
	if (fd < 0)
		return 0;

	buf[0] = 0;
	read (fd, buf, 4096);
	buf[4096] = 0;

	tmp2 = strstr (buf, "bswap");

	if (tmp2)
	{
		tmp = strcspn (tmp2, "01");

		if (tmp)
			retval = (tmp2[tmp] - '0') ^ 1;
	}
	close (fd);
	return retval;
}

static const char *partition_strings [2][16][2] =
{
	{
		{"", ""},
		{"Apple", "Apple_partition_map"},
		{"Bootstrap 1", "Image"},
		{"Kernel 1", "Image"},
		{"Root 1", "Ext2"},
		{"Bootstrap 2", "Image"},
		{"Kernel 2", "Image"},
		{"Root 2", "Ext2"},
		{"Linux swap", "Swap"},
		{"/var", "Ext2"},
		{"MFS application a10", "MFS"},
		{"MFS media a11", "MFS"},
		{"MFS application a12", "MFS"},
		{"MFS media a13", "MFS"},
		{"MFS application a14", "MFS"},
		{"MFS media a15", "MFS"},
	},
	{
		{"", ""},
		{"Apple", "Apple_partition_map"},
		{"MFS application b2", "MFS"},
		{"MFS media b3", "MFS"},
		{"MFS application b4", "MFS"},
		{"MFS media b5", "MFS"},
		{"MFS application b6", "MFS"},
		{"MFS media b7", "MFS"},
		{"MFS application b8", "MFS"},
		{"MFS media b9", "MFS"},
		{"MFS application b10", "MFS"},
		{"MFS media b11", "MFS"},
		{"MFS application b12", "MFS"},
		{"MFS media b13", "MFS"},
		{"MFS application b14", "MFS"},
		{"MFS media b15", "MFS"},
	}
};

int
build_partition_table (struct backup_info *info, int devno)
{
	char buf[65536];
	union {
		struct mac_partition p;
		char c[512];
	} *part = (void *)buf;
	int loop;
	unsigned int curstart = 1;

	bzero (buf, sizeof (buf));

	for (loop = 0; loop < info->nnewparts; loop++)
	{
		if (info->newparts[loop].devno == devno)
		{
			int partno = info->newparts[loop].partno;
			part[partno].p.signature = 0x4d50;
			part[partno].p.map_count = htonl (info->devs[devno].nparts);
			part[partno].p.start_block = htonl (curstart);
			part[partno].p.block_count = htonl (info->newparts[loop].sectors);
			strcpy (part[partno].p.name, partition_strings [devno][partno][0]);
			strcpy (part[partno].p.type, partition_strings [devno][partno][1]);
			part[partno].p.data_count = part[partno].p.block_count;
/* I have no clue what this means, but it's what TiVo (pdisk?) uses. */
			part[partno].p.status = htonl (0x33);
			curstart += info->newparts[loop].sectors;
		}
	}

	if (curstart < info->devs[devno].sectors)
	{
		for (loop = 1; loop <= info->devs[devno].nparts; loop++)
		{
			part[loop].p.map_count = htonl (htonl (part[loop].p.map_count) + 1);
		}
		part[loop].p.signature = 0x4d50;
		part[loop].p.map_count = htonl (info->devs[devno].nparts + 1);
		part[loop].p.start_block = htonl (curstart);
		part[loop].p.block_count = htonl (info->devs[devno].sectors - curstart);
		strcpy (part[loop].p.name, "Extra");
		strcpy (part[loop].p.type, "Apple_Free");
		part[loop].p.data_count = part[loop].p.block_count;
		part[loop].p.status = htonl (0x33);
	}

	if (info->devs[devno].swab)
	{
		unsigned int *ints = (unsigned int *)buf;
		for (loop = 0; loop < 65536 / 4; loop++)
		{
			register int tmp = ints[loop];
			ints[loop] = ((tmp & 0xff00ff00) >> 8) | ((tmp << 8) & 0xff00ff00);
		}
	}

	if (lseek (info->devs[devno].fd, 0, SEEK_SET) < 0 || write (info->devs[devno].fd, buf, sizeof (buf)) < 0) {
		info->lasterr = "Unable to create partition table.";
		return -1;
	}

	ioctl (info->devs[devno].fd, BLKRRPART, 0);

	return 0;
}

int
restore_start (struct backup_info *info)
{
	int loop;

	if (info->back_flags & RF_INITIALIZED || info->cursector < info->presector || !info->devs)
	{
		info->lasterr = "Internal error 6.";
		return -1;
	}

	for (loop = 0; loop < info->ndevs; loop++) {
		info->devs[loop].swab = scan_swab (info->devs[loop].devname);
		if (build_partition_table (info, loop) < 0)
			return -1;

		if (!info->devs[loop].files)
		{
			info->devs[loop].files = calloc (sizeof (struct tivo_partition_file *), info->devs[loop].nparts);
			if (!info->devs[loop].files)
			{
				info->lasterr = "Memory exhausted.";
				return -1;
			}
		}
	}

	info->back_flags |= RF_INITIALIZED;
	return 0;
}

static const char swapspace[] = "SWAP-SPACE";
#define SWAP_PAGESZ 0x1000

int
restore_make_swap (struct backup_info *info)
{
	tpFILE *file;
	int size;
	unsigned int swaphdr[SWAP_PAGESZ / 4];
	int loop = 0;
	unsigned int loop2 = 0;

	bzero (swaphdr, sizeof (swaphdr));
	memcpy ((char *)swaphdr + sizeof (swaphdr) - strlen (swapspace), swapspace, strlen (swapspace));

	file = tivo_partition_open_direct (info->devs[0].devname, 8, O_RDWR);

	if (!file)
	{
		info->lasterr = "Error making swap partition.";
		return -1;
	}

	size = tivo_partition_size (file);

	while (loop < 0xff4 / 4 && size > 32 * SWAP_PAGESZ / 512)
	{
		swaphdr[loop++] = 0xffffffff;
		size -= 32 * SWAP_PAGESZ / 512;
	}

	for (loop2 = 0x1; loop2 > 0 || size > SWAP_PAGESZ / 512; size -= SWAP_PAGESZ / 512, loop2 <<= 1)
	{
		swaphdr[loop] |= htonl (loop2);
	}

	swaphdr[0] &= ~htonl (1);

	loop = tivo_partition_write (file, swaphdr, 0, sizeof (swaphdr) / 512);
	tivo_partition_close (file);

	if (loop < 0)
	{
		info->lasterr = "Error making swap partition.";
		return -1;
	}

	return 0;
}

int
restore_fudge_inodes (struct backup_info *info)
{
	unsigned int loop, count, total;

	if (!(info->back_flags & BF_SHRINK))
		return 0;

	count = mfs_inode_count ();
	total = mfs_volume_set_size ();

	for (loop = 0; loop < count; loop++)
	{
		mfs_inode *inode = mfs_read_inode (loop);

		if (inode)
		{
			if (inode->type == tyStream)
			{
				int loop2;
				int changed;

				for (loop2 = 0; loop2 < htonl (inode->numblocks); loop2++)
				{
					if (htonl (inode->datablocks[loop2].sector) >= total)
					{
						inode->blockused = 0;
						changed = 1;
						inode->numblocks = htonl (htonl (inode->numblocks) - 1);
						if (loop2 < htonl (inode->numblocks))
							memmove (&inode->datablocks[loop2], &inode->datablocks[loop2 + 1], sizeof (*inode->datablocks) * (htonl (inode->numblocks) - loop2));
						loop2--;
					}
				}

				if (changed)
					if (mfs_write_inode (inode) < 0)
					{
						info->lasterr = "Error fixing up inodes.";
						return -1;
					}

			}
			free (inode);
		}
	}

	return 0;
}

int
restore_fixup_vol_list (struct backup_info *info)
{
	int loop;
	union {
		volume_header hdr;
		char pad[512];
	} vol;

	if (mfs_read_data ((void *)&vol, 0, 1) != 512)
	{
		info->lasterr = "Error fixing volume list.";
		return -1;
	}

	bzero (vol.hdr.partitionlist, sizeof (vol.hdr.partitionlist));

	for (loop = 0; loop < info->nmfs; loop++)
	{
		sprintf (vol.hdr.partitionlist + strlen (vol.hdr.partitionlist), "%s/dev/hd%c%d", loop > 0? " ": "", 'a' + info->mfsparts[loop].devno, info->mfsparts[loop].partno);
	}

	if (strlen (vol.hdr.partitionlist) + 1 > sizeof (vol.hdr.partitionlist))
	{
		info->lasterr = "Partition list too long.";
		return -1;
	}

	vol.hdr.total_sectors = htonl (mfs_volume_set_size ());

	MFS_update_crc (&vol.hdr, sizeof (vol.hdr), vol.hdr.checksum);

	if (mfs_write_data ((void *)&vol, 0, 1) != 512 || mfs_write_data ((void *)&vol, mfs_volume_size (0) - 1, 1) != 512)
	{
		info->lasterr = "Error writing changes to volume header.";
		return -1;
	}

	return 0;
}

int
restore_fixup_zone_maps(struct backup_info *info)
{
	unsigned int tot;
	union {
		volume_header hdr;
		char pad[512];
	} vol;
	zone_header *cur;

	if (!(info->back_flags & BF_SHRINK))
		return 0;

	tot = mfs_volume_set_size ();

	if (mfs_read_data ((void *)&vol, 0, 1) < 0)
	{
		info->lasterr = "Error truncating MFS volume.";
		return -1;
	}

	cur = malloc (htonl (vol.hdr.zonemap.length) * 512);
	if (!cur)
	{
		info->lasterr = "Memory exhausted.";
		return -1;
	}

	if (mfs_read_data ((void *)cur, htonl (vol.hdr.zonemap.sector), htonl (vol.hdr.zonemap.length)) != htonl (vol.hdr.zonemap.length) * 512)
	{
		free (cur);
		info->lasterr = "Error truncating MFS volume.";
		return -1;
	}

	while (cur->next.sector && htonl (cur->next.sector) < tot)
	{
		unsigned int sector = htonl (cur->next.sector);
		unsigned int length = htonl (cur->next.length);

		cur = realloc (cur, length * 512);

		if (!cur)
		{
			info->lasterr = "Memory exhausted.";
			return -1;
		}

		if (mfs_read_data ((void *)cur, sector, length) != length * 512)
		{
			info->lasterr = "Error truncating MFS volume.";
			free (cur);
			return -1;
		}
	}

	if (cur->next.sector)
	{
		cur->next.sector = 0;
		cur->next.length = 0;
		cur->next.size = 0;
		cur->next.min = 0;

		MFS_update_crc (cur, htonl (cur->length) * 512, cur->checksum);

		if (mfs_write_data ((void *)cur, htonl (cur->sector), htonl (cur->length)) != htonl (cur->length) * 512 || mfs_write_data ((void *)cur, htonl (cur->sbackup), htonl (cur->length)) != htonl (cur->length) * 512)
		{
			info->lasterr = "Error truncating MFS volume.";
			free (cur);
			return -1;
		}
	}

	free (cur);
	return 0;
}

int
restore_cleanup_parts(struct backup_info *info)
{
	unsigned int loop, loop2;
	char buf[1024 * 1024];

	bzero (buf, sizeof (buf));

	for (loop = 2 - 1; loop < 10 - 1; loop++)
	{
		for (loop2 = 0; loop2 < info->nparts; loop2++)
		{
			if (info->parts[loop2].devno == 0 && info->parts[loop2].partno == loop + 1)
				break;
		}

		if (loop2 >= info->nparts)
		{
			tpFILE *file = info->devs[0].files[loop];
			int tot;

			if (!file)
				file = tivo_partition_open_direct (info->devs[0].devname, loop + 1, O_RDWR);
			if (!file)
			{
				info->lasterr = "Error cleaning up partitions.";
				return -1;
			}

			if (info->back_flags & RF_ZEROPART)
				tot = tivo_partition_size (file);
			else
				tot = 2048 / 512;

			for (loop2 = 0; loop2 < tot; loop2 += sizeof (buf))
			{
				int towrite;

				towrite = loop2 + sizeof (buf) / 512 > tot? tot - loop2: sizeof (buf) / 512;
				tivo_partition_write (file, buf, loop2, towrite);
			}
		}
	}

	return 0;
}

int
restore_finish(struct backup_info *info)
{
	if (info->cursector != info->nsectors)
	{
		info->lasterr = "Premature end of backup data.";
		return -1;
	}
	if (restore_cleanup_parts (info) < 0)
		return -1;
	if (restore_make_swap (info) < 0)
		return -1;
	if (restore_fixup_vol_list (info) < 0)
		return -1;
	if (restore_fixup_zone_maps (info) < 0)
		return -1;

	mfs_cleanup_volumes ();
	setenv ("MFS_HDA", info->devs[0].devname, 1);
	if (info->ndevs > 1)
		setenv ("MFS_HDB", info->devs[1].devname, 1);
	if (mfs_init (O_RDWR) < 0)
		return -1;
	if (restore_fudge_inodes (info) < 0)
		return -1;

	return 0;
}
