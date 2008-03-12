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
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <sys/param.h>
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#ifdef HAVE_LINUX_UNISTD_H
#include <linux/unistd.h>
#endif

/* For htonl() */
#include <netinet/in.h>

#include "mfs.h"
#include "log.h"

unsigned int
mfs_log_last_sync (struct mfs_handle *mfshnd)
{
	return htonl (mfshnd->vol_hdr.logstamp);
}

int
mfs_log_read (struct mfs_handle *mfshnd, void *buf, unsigned int logstamp)
{
	log_hdr *tmp = buf;

	if (mfsvol_read_data (mfshnd->vols, buf, (logstamp % htonl (mfshnd->vol_hdr.lognsectors)) + htonl (mfshnd->vol_hdr.logstart), 1) != 512)
	{
		return -1;
	}

	if (logstamp != htonl (tmp->logstamp))
	{
		return 0;
	}

	if (!MFS_check_crc (buf, 512, tmp->crc))
	{
		mfshnd->err_msg = "MFS transaction logstamp %ud has invalid checksum";
		mfshnd->err_arg1 = (void *)logstamp;
		return 0;
	}

	return 512;
}

int
mfs_log_write (struct mfs_handle *mfshnd, void *buf)
{
	log_hdr *tmp = buf;
	unsigned int logstamp = htonl (tmp->logstamp);

	MFS_update_crc (buf, 512, tmp->crc);

	if (mfsvol_write_data (mfshnd->vols, buf, (logstamp % htonl (mfshnd->vol_hdr.lognsectors)) + htonl (mfshnd->vol_hdr.logstart), 1) != 512)
	{
		return -1;
	}

	return 512;
}

static int
mfs_log_sync_inode (struct mfs_handle *mfshnd, log_inode_update *entry)
{
	mfs_inode *inode;

	if (htonl (entry->inode) == -1)
		inode = mfs_find_inode_for_fsid (mfshnd, htonl (entry->fsid));
	else
		inode = mfs_read_inode (mfshnd, htonl (entry->inode));

	if (!inode)
	{
		mfshnd->err_msg = "Error loading inode %d for fsid %d";
		mfshnd->err_arg1 = (void *)htonl (entry->inode);
		mfshnd->err_arg2 = (void *)htonl (entry->fsid);
		return 0;
	}

	inode->fsid = entry->fsid;
	inode->refcount = entry->refcount;
	inode->bootcycles = entry->bootcycles;
	inode->bootsecs = entry->bootsecs;
	inode->unk3 = entry->unk3;
	inode->size = entry->size;
	inode->blocksize = entry->blocksize;
	inode->blockused = entry->blockused;
	inode->lastmodified = entry->lastmodified;
	inode->type = entry->type;
	inode->zone = entry->zone;
	inode->pad = entry->pad;
	if (entry->inodedata)
	{
		inode->inode_flags |= htonl (INODE_DATA);
		inode->numblocks = 0;
	}
	else
	{
		inode->inode_flags &= htonl (~INODE_DATA);
		inode->numblocks = htonl (htonl (entry->datasize) / sizeof (inode->datablocks[0]));
	}
	memcpy (&inode->datablocks[0], &entry->datablocks[0], htonl (entry->datasize));
	if (mfs_write_inode (mfshnd, inode) < 0)
		return 0;

	/* Update the next fsid field in the volume header if it's needed */
	if (htonl (mfshnd->vol_hdr.next_fsid) <= htonl (entry->fsid))
	{
		mfshnd->vol_hdr.next_fsid = htonl (htonl (entry->fsid) + 1);
	}
	return 1;
} 

/************************************************************************/
/* Take a list of transactions and commit them */
static int
mfs_log_fssync_list (struct mfs_handle *mfshnd, struct log_entry_list *list, unsigned int logstamp, int logsync)
{
	struct log_entry_list *cur;

	/* Scan the list to make sure every entry is valid and understood */
	for (cur = list; cur; cur = cur->next)
	{
		if (cur->entry.log.length < sizeof (log_entry) + 2)
		{
			mfshnd->err_msg = "Log entry too short";
			return 0;
		}

		switch (htonl (cur->entry.log.transtype))
		{
			case ltMapUpdate:
			case ltInodeUpdate:
			case ltInodeUpdate2:
			case ltCommit:
			case ltFsSync:
				break;
			default:
				mfshnd->err_msg = "Unknown transaction log type %d";
				mfshnd->err_arg1 = (void *)htonl (cur->entry.log.transtype);
				return 0;
		}
	}

	/* Commit each log entry in turn */
	for (cur = list; cur; cur = cur->next)
	{
		switch (htonl (cur->entry.log.transtype))
		{
			case ltMapUpdate:
				if (mfs_zone_map_update (mfshnd, htonl (cur->entry.zonemap.sector), htonl (cur->entry.zonemap.size), htonl (cur->entry.zonemap.remove), logstamp) < 1)
				{
					return 0;
				}
				break;
			case ltInodeUpdate:
			case ltInodeUpdate2:
				if (mfs_log_sync_inode (mfshnd, &cur->entry.inode) < 1)
				{
					return 0;
				}
				break;
			case ltCommit:
			case ltFsSync:
				break;
		}
	}

	mfs_zone_map_commit (mfshnd);
	if (logsync)
	{
		unsigned char buf[512];
		memset (buf, 0, sizeof (buf));
		log_hdr *hdr = (log_hdr *)buf;
		log_entry *entry = (log_entry *)(hdr + 1);
		hdr->logstamp = htonl (logstamp + 1);
		hdr->size = htonl (sizeof (*entry) + 2);
		entry->length = htons (sizeof (*entry) - 2);
		entry->bootcycles = htonl (mfshnd->bootcycle);
		entry->bootsecs = htonl (++mfshnd->bootsecs);
		entry->transtype = htonl (ltFsSync);
		
		mfs_log_write (mfshnd, buf);
		
		mfshnd->vol_hdr.logstamp = htonl (logstamp);
		mfshnd->vol_hdr.bootcycles = htonl (mfshnd->bootcycle);
		mfshnd->vol_hdr.bootsecs = htonl (mfshnd->bootsecs);
		/* Increment it again so this transaction will be distinct from */
		/* updates before the next transaction */
		mfshnd->bootsecs++;

		mfs_write_volume_header (mfshnd);
	}

	return 1;
}

/************************************************************************/
/* Load a list of transactions, truncating it at the last commit. */
/* If the start is passed in as ~0, it is assumed to be the last successful sync */
/* If the end is passed as ~0, it is assumed to be the last log written */
static int
mfs_log_load_committed_list (struct mfs_handle *mfshnd, unsigned int start, unsigned int *end, struct log_entry_list **list)
{
	struct log_entry_list *cur, **tail, **synctail;
	unsigned char buf[512];
	log_hdr *curlog = (log_hdr *)buf;
	unsigned int partremaining, partread;
	unsigned int lastsynclog = ~0;

	if (!~start)
	{
		start = mfs_log_last_sync (mfshnd) + 1;
	}
	
	/* No need to update end, ~0 is bigger than anything else, and it stops */
	/* on a non read anyway */
	
	*list = NULL;
	synctail = list;
	tail = list;

	cur = NULL;
	partremaining = 0;
	partread = 0;

	/* Read in all the log entries since the last sync */
	for (; start <= *end && mfs_log_read (mfshnd, (void *)buf, start) >= 512; start++)
	{
		unsigned int curstart = 0;

		if (!cur)
		{
			/* Start a new log entry */
			partread = 0;
			partremaining = htons (*(unsigned short *)(buf + curstart + sizeof (log_hdr))) + 2;
			if (partremaining > 2)
			{
				/* Only allocate if there is going to be actual data */
				cur = calloc (partremaining + sizeof (cur->next) + sizeof (cur->logstamp), 1);
			}
		}
		else if (partremaining < htonl (curlog->first) || curlog->first == 0)
		{
			/* Existing entry that doesn't look like it's properly continued */
			mfshnd->err_msg = "Error reading from log entry %d";
			mfshnd->err_arg1 = (void *)start;
			while (*list)
			{
				cur = *list;
				*list = (*list)->next;
				free (cur);
			}
			return 0;
		}

		/* Read in the entries */
		while (partremaining > 0 && curstart < htonl (curlog->size))
		{
			int tocopy = partremaining;

			if (tocopy > htonl (curlog->size) - curstart)
			{
				tocopy = htonl (curlog->size) - curstart;
			}

			if (cur)
			{
				/* If cur is NULL, this is just an empty 2 byte entry with no log entry */
				memcpy ((unsigned char *)&cur->entry + partread, buf + curstart + sizeof (log_hdr), tocopy);
			}
			partread += tocopy;
			partremaining -= tocopy;
			curstart += tocopy;

			if (partremaining > 0)
			{
				/* Continued in the next log sector */
				break;
			}

			if (cur)
			{
				*tail = cur;
				tail = &cur->next;

				if (cur->entry.log.transtype == htonl (ltCommit))
				{
					/* Mark the new end of what is to be committed */
					synctail = &cur->next;
					lastsynclog = start;
				}
			}

			cur = NULL;
			partread = 0;
			partremaining = 0;
			if (curstart + 2 <= htonl (curlog->size))
			{
				partremaining = htons (*(unsigned short *)(buf + curstart + sizeof (log_hdr))) + 2;
				if (partremaining > 2)
				{
					/* Only allocate if there is going to be actual data */
					cur = calloc (partremaining + sizeof (cur->next) + sizeof (cur->logstamp), 1);
				}
			}
		}
	}

	/* All valid entries are read in */
	/* Clear out any partial read */
	if (cur)
	{
		free (cur);
	}
	
	/* Clear out anything after the last commit */
	while (*synctail)
	{
		cur = *synctail;
		*synctail = cur->next;
		free (cur);
	}

	*end = lastsynclog;

	return 1;
}

/************************************************************************/
/* Replay the transaction log and write a FSSync log entry */
int
mfs_log_fssync (struct mfs_handle *mfshnd)
{
	unsigned int lastsynclog = ~0;
	struct log_entry_list *list;

	if (mfs_log_load_committed_list (mfshnd, ~0, &lastsynclog, &list) != 1)
		return 0;
	if (list)
	{
		return mfs_log_fssync_list (mfshnd, list, lastsynclog, 1);
	}
	return 1;
}