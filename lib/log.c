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

#include "mfs.h"
#include "log.h"

unsigned int
mfs_log_last_sync (struct mfs_handle *mfshnd)
{
	if (mfshnd->is_64)
	{
		return intswap32 (mfshnd->vol_hdr.v64.logstamp);
	}
	else
	{
		return intswap32 (mfshnd->vol_hdr.v32.logstamp);
	}
}

uint64_t
mfs_log_stamp_to_sector (struct mfs_handle *mfshnd, unsigned int logstamp)
{
	if (mfshnd->is_64)
	{
		return (logstamp % intswap32 (mfshnd->vol_hdr.v64.lognsectors)) + intswap64 (mfshnd->vol_hdr.v64.logstart);
	}
	else
	{
		return (logstamp % intswap32 (mfshnd->vol_hdr.v32.lognsectors)) + intswap32 (mfshnd->vol_hdr.v32.logstart);
	}
}

int
mfs_log_read (struct mfs_handle *mfshnd, void *buf, unsigned int logstamp)
{
	log_hdr *tmp = buf;

	if (mfsvol_read_data (mfshnd->vols, buf, mfs_log_stamp_to_sector (mfshnd, logstamp), 1) != 512)
	{
		return -1;
	}

	if (logstamp != intswap32 (tmp->logstamp))
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
	unsigned int logstamp = intswap32 (tmp->logstamp);

	MFS_update_crc (buf, 512, tmp->crc);

	if (mfsvol_write_data (mfshnd->vols, buf, mfs_log_stamp_to_sector (mfshnd, logstamp), 1) != 512)
	{
		return -1;
	}

	return 512;
}

static int
mfs_log_write_current_log (struct mfs_handle *mfshnd)
{
	mfs_log_write (mfshnd, mfshnd->current_log);

	/* Rack 'em up for the next bit of data */
	mfshnd->current_log->logstamp = intswap32 (intswap32 (mfshnd->current_log->logstamp) + 1);
	mfshnd->current_log->crc = 0xdeadf00d;
	mfshnd->current_log->size = 0;
	mfshnd->current_log->first = 0;

	/* Zero out the data portion */
	memset (mfshnd->current_log + 1, 0, 512 - sizeof (log_hdr));
}

int
mfs_log_add_entry (struct mfs_handle *mfshnd, log_entry *entry)
{
	int copystart = 0;

	if (!mfshnd->current_log)
	{
		mfshnd->err_msg = "Transaction log must be synced before new entries can be added.";
		return 0;
	}

	/* Deal with overflow */
	while (intswap16 (entry->length) + 2 - copystart >= 512 - intswap32 (mfshnd->current_log->size) - sizeof (log_hdr))
	{
		int left;

		/* Copy the partial data */
		memcpy ((unsigned char *)(mfshnd->current_log + 1) + intswap32 (mfshnd->current_log->size), (unsigned char *)entry + copystart, 512 - intswap32 (mfshnd->current_log->size) - sizeof (log_hdr));
		mfshnd->current_log->size = intswap32 (512 - sizeof (log_hdr));

		/* Write the (now full) log entry */
		mfs_log_write_current_log (mfshnd);

		/* Record that part has been written */
		copystart += 512 - sizeof (log_hdr) - intswap32 (mfshnd->current_log->size);
		if (intswap16 (entry->length) - copystart > 512 - sizeof (log_hdr))
		{
			mfshnd->current_log->first = intswap32 (512 - sizeof (log_hdr));
		}
		else
		{
			mfshnd->current_log->first = intswap32 (intswap16 (entry->length) - copystart);
		}
	}

	if (copystart < intswap16 (entry->length))
	{
		memcpy ((unsigned char *)(mfshnd->current_log + 1) + intswap32 (mfshnd->current_log->size), (unsigned char *)entry + copystart, intswap16 (entry->length) + 2 - copystart);
		mfshnd->current_log->size = intswap32 (intswap32 (mfshnd->current_log->size) + intswap16 (entry->length) + 2 - copystart);
	}

	return 1;
}

int
mfs_log_zone_update (struct mfs_handle *mfshnd, unsigned int fsid, uint64_t sector, uint64_t size, int state, int flag)
{
	log_entry_all entry;

	/* Clear everything out first */
	memset ((unsigned char *)&entry + sizeof (log_entry), 0xaa, sizeof (entry) - sizeof (log_entry));

	/* Fill out the generic parts */
	entry.log.unk1 = 0;
	entry.log.unk2 = 0;
	entry.log.bootcycles = intswap32 (mfshnd->bootcycle);
	entry.log.bootsecs = intswap32 (mfshnd->bootsecs);
	entry.log.fsid = intswap32 (fsid);

	/* Fill in the details */
	if (mfshnd->is_64)
	{
		entry.log.length = intswap16 (sizeof (log_map_update_64) - 2);
		entry.log.transtype = intswap32 (ltMapUpdate64);
		entry.zonemap_64.remove = state? intswap32 (1) : 0;
		entry.zonemap_64.sector = intswap64 (sector);
		entry.zonemap_64.size = intswap64 (size);
		entry.zonemap_64.flag = flag? 1: 0;
	}
	else
	{
		entry.log.length = intswap16 (sizeof (log_map_update_32) - 2);
		entry.log.transtype = intswap32 (ltMapUpdate);
		entry.zonemap_32.remove = state? intswap32 (1) : 0;
		entry.zonemap_32.sector = intswap32 (sector);
		entry.zonemap_32.size = intswap32 (size);
		entry.zonemap_32.unk = 0;
	}

	/* Add the entry */
	return mfs_log_add_entry (mfshnd, &entry.log);
}

int
mfs_log_zone_update_for_inodes (struct mfs_handle *mfshnd, mfs_inode *inode1, mfs_inode *inode2, unsigned int fsid, int newstate)
{
	int loop;

	if (inode1 && 
		(inode1->inode_flags & intswap32 (INODE_DATA) || !inode1->refcount ||
		!inode1->numblocks || !inode1->fsid))
	{
		inode1 = NULL;
	}

	if (inode2 && 
		(inode2->inode_flags & intswap32 (INODE_DATA) || !inode2->refcount ||
		!inode2->numblocks || !inode2->fsid))
	{
		inode2 = NULL;
	}

	if (!inode1)
	{
		return 1;
	}

	/* Act on blocks in the first inode that don't exist in the second */
	for (loop = 0; loop < intswap32 (inode1->numblocks); loop++)
	{
		/* Check to make sure the datablock in question isn't in use anymore */
		int wasfound = 0;

		if (inode2)
		{
			int loop2;

			if (mfshnd->is_64)
			{
				for (loop2 = 0; loop2 < intswap32 (inode2->numblocks); loop2++)
				{
					if (inode1->datablocks.d64[loop].sector == inode2->datablocks.d64[loop2].sector &&
						inode1->datablocks.d64[loop].count == inode2->datablocks.d64[loop2].count)
					{
						wasfound = 1;
						break;
					}
				}
			}
			else
			{
				for (loop2 = 0; loop2 < intswap32 (inode2->numblocks); loop2++)
				{
					if (inode1->datablocks.d32[loop].sector == inode2->datablocks.d32[loop2].sector &&
						inode1->datablocks.d32[loop].count == inode2->datablocks.d32[loop2].count)
					{
						wasfound = 1;
						break;
					}
				}
			}
		}

		/* It wasn't found in the new inode, so update it */
		if (!wasfound)
		{
			if (mfshnd->is_64)
			{
				mfs_log_zone_update (mfshnd, fsid,
					intswap64 (inode1->datablocks.d64[loop].sector),
					intswap32 (inode1->datablocks.d64[loop].count), newstate, 0);
			}
			else
			{
				mfs_log_zone_update (mfshnd, fsid,
					intswap32 (inode1->datablocks.d32[loop].sector),
					intswap32 (inode1->datablocks.d32[loop].count), newstate, 0);
			}
		}
	}
}

int
mfs_log_inode_update (struct mfs_handle *mfshnd, mfs_inode *inode)
{
	mfs_inode *oldinode;
	log_inode_update *entry;
	int datasize = 0;
	int inodedata = 0;
	unsigned int fsid = 0;

	if (!mfshnd->inode_log_type)
	{
		mfshnd->err_msg = "Unable to determine transaction type for inodes";
		return 0;
	}

	/* Read in the previous contents of the inode if there was any */
	if (inode->inode != -1)
	{
		oldinode = mfs_read_inode (mfshnd, intswap32 (inode->inode));
	}

	/* Get the fsid for log entries, just in case it's not in the inode block anymore (IE a free) */
	if (inode->fsid)
		fsid = intswap32 (inode->fsid);
	else if (oldinode && inode->fsid)
		fsid = intswap32 (inode->fsid);

	if (oldinode)
	{
		/* Free any blocks no longer in use */
		mfs_log_zone_update_for_inodes (mfshnd, oldinode, inode, fsid, 1);
	}

	if (inode->refcount && inode->fsid)
	{
		/* Allocate any blocks now in use */
		mfs_log_zone_update_for_inodes (mfshnd, inode, oldinode, fsid, 0);

		if (inode->type != tyStream && (inode->inode_flags & intswap32 (INODE_DATA)))
		{
			/* Data is in the inode block */
			datasize = intswap32 (inode->size);
			inodedata = 1;
		}
		else
		{
			/* Data is in referenced extents */
			if (mfshnd->is_64)
			{
				datasize = intswap32 (inode->numblocks) * sizeof (inode->datablocks.d64[0]);
			}
			else
			{
				datasize = intswap32 (inode->numblocks) * sizeof (inode->datablocks.d64[0]);
			}
		}
	}

	entry = alloca (sizeof (*entry) + datasize);

	/* Generic log stuff */
	entry->log.length = intswap16 (sizeof (*entry) + datasize - 2);
	entry->log.unk1 = 0;
	entry->log.bootcycles = intswap32 (mfshnd->bootcycle);
	entry->log.bootsecs = intswap32 (mfshnd->bootsecs);
	entry->log.fsid = intswap32 (fsid);
	entry->log.transtype = intswap32 (mfshnd->inode_log_type);
	entry->log.unk2 = 0;

	/* Copy the inode */
	entry->fsid = inode->fsid;
	entry->refcount = inode->refcount;
	entry->bootcycles = mfshnd->bootcycle;
	entry->bootsecs = mfshnd->bootsecs;
	entry->inode = inode->inode;
	entry->unk3 = inode->unk3;
	entry->size = inode->size;
	entry->blocksize = inode->blocksize;
	entry->blockused = inode->blockused;
	entry->lastmodified = inode->lastmodified;
	entry->type = inode->type;
	entry->zone = inode->zone;
	entry->pad = inode->pad;
	entry->inodedata = inodedata? intswap32 (1) : 0;
	entry->datasize = intswap32 (datasize);
	if (datasize)
	{
		memcpy (&entry->datablocks, &inode->datablocks, datasize);
	}

	mfs_log_add_entry (mfshnd, &entry->log);
}

int
mfs_log_commit (struct mfs_handle *mfshnd)
{
	log_entry entry;

	/* Start with a clean structure */
	memset (&entry, 0, sizeof (entry));

	entry.length = intswap16 (sizeof (entry) - 2);
	entry.bootcycles = intswap32 (mfshnd->bootcycle);
	/* Increment the seconds for thenext set of data to commit */
	entry.bootsecs = intswap32 (mfshnd->bootsecs++);
	entry.transtype = intswap32 (ltCommit);

	if (mfs_log_add_entry (mfshnd, &entry) <= 0)
		return 0;

	/* Add entry should never leave the pointer right at the end, so this is safe. */
	/* Use the zeroed out structure as a zero length entry */
	mfshnd->current_log->size = intswap32 (intswap32 (mfshnd->current_log->size) + 2);
	mfs_log_write_current_log (mfshnd);

	return 1;
}

static int
mfs_log_sync_inode (struct mfs_handle *mfshnd, log_inode_update *entry)
{
	mfs_inode *inode;

	if (intswap32 (entry->inode) == -1)
		inode = mfs_find_inode_for_fsid (mfshnd, intswap32 (entry->fsid));
	else
		inode = mfs_read_inode (mfshnd, intswap32 (entry->inode));

	if (!inode)
	{
		mfshnd->err_msg = "Error loading inode %d for fsid %d";
		mfshnd->err_arg1 = (void *)intswap32 (entry->inode);
		mfshnd->err_arg2 = (void *)intswap32 (entry->fsid);
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
		inode->inode_flags |= intswap32 (INODE_DATA);
		inode->numblocks = 0;
	}
	else
	{
		inode->inode_flags &= intswap32 (~INODE_DATA);
		if (mfshnd->is_64)
		{
			inode->numblocks = intswap32 (intswap32 (entry->datasize) / sizeof (inode->datablocks.d64[0]));
		}
		else
		{
			inode->numblocks = intswap32 (intswap32 (entry->datasize) / sizeof (inode->datablocks.d32[0]));
		}
	}
	memcpy (&inode->datablocks.d32[0], &entry->datablocks.d32[0], intswap32 (entry->datasize));
	if (mfs_write_inode (mfshnd, inode) < 0)
		return 0;

	/* Update the next fsid field in the volume header if it's needed */
	if (mfshnd->is_64)
	{
		if (intswap32 (mfshnd->vol_hdr.v64.next_fsid) <= intswap32 (entry->fsid))
		{
			mfshnd->vol_hdr.v64.next_fsid = intswap32 (intswap32 (entry->fsid) + 1);
		}
	}
	else
	{
		if (intswap32 (mfshnd->vol_hdr.v32.next_fsid) <= intswap32 (entry->fsid))
		{
			mfshnd->vol_hdr.v32.next_fsid = intswap32 (intswap32 (entry->fsid) + 1);
		}
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

		switch (intswap32 (cur->entry.log.transtype))
		{
			case ltMapUpdate:
			case ltMapUpdate64:
			case ltCommit:
			case ltLogReplay:
			case ltFsSync:
				break;
			case ltInodeUpdate:
				mfshnd->inode_log_type = ltInodeUpdate;
				break;
			case ltInodeUpdate2:
				mfshnd->inode_log_type = ltInodeUpdate2;
				break;
			default:
				mfshnd->err_msg = "Unknown transaction log type %d";
				mfshnd->err_arg1 = (void *)intswap32 (cur->entry.log.transtype);
				return 0;
		}
	}

	/* Commit each log entry in turn */
	for (cur = list; cur; cur = cur->next)
	{
		switch (intswap32 (cur->entry.log.transtype))
		{
			case ltMapUpdate:
				if (mfs_zone_map_update (mfshnd, intswap32 (cur->entry.zonemap_32.sector), intswap32 (cur->entry.zonemap_32.size), intswap32 (cur->entry.zonemap_32.remove), logstamp) < 1)
				{
					return 0;
				}
				break;
			case ltMapUpdate64:
				if (mfs_zone_map_update (mfshnd, intswap64 (cur->entry.zonemap_64.sector), intswap64 (cur->entry.zonemap_64.size), intswap32 (cur->entry.zonemap_64.remove), logstamp) < 1)
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
			case ltLogReplay:
				break;
		}
	}

	mfs_zone_map_commit (mfshnd, logstamp);
	if (logsync)
	{
		log_entry entry;
		entry.length = intswap16 (sizeof (entry) - 2);

		/* On the initial sync, initialize the MFS Tool transaction logging */
		if (!mfshnd->current_log)
		{
			mfshnd->current_log = calloc (512, 1);
			mfshnd->current_log->logstamp = intswap32 (logstamp + 1);

			/* Log that it was a log replay */
			entry.bootcycles = intswap32 (mfshnd->bootcycle);
			entry.bootsecs = intswap32 (mfshnd->bootsecs);
			entry.transtype = intswap32 (ltLogReplay);
			mfs_log_add_entry (mfshnd, &entry);
		}

		/* Log the FS Sync */
		entry.bootsecs = intswap32 (++mfshnd->bootsecs);
		entry.transtype = intswap32 (ltFsSync);
		mfs_log_add_entry (mfshnd, &entry);

		mfs_log_write_current_log (mfshnd);

		if (mfshnd->is_64)
		{
			mfshnd->vol_hdr.v64.logstamp = intswap32 (logstamp);
			mfshnd->vol_hdr.v64.bootcycles = intswap32 (mfshnd->bootcycle);
			mfshnd->vol_hdr.v64.bootsecs = intswap32 (mfshnd->bootsecs);
		}
		else
		{
			mfshnd->vol_hdr.v32.logstamp = intswap32 (logstamp);
			mfshnd->vol_hdr.v32.bootcycles = intswap32 (mfshnd->bootcycle);
			mfshnd->vol_hdr.v32.bootsecs = intswap32 (mfshnd->bootsecs);
		}
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
mfs_log_load_list (struct mfs_handle *mfshnd, unsigned int start, unsigned int *end, struct log_entry_list **list, int committed)
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
			partremaining = intswap16 (*(unsigned short *)(buf + curstart + sizeof (log_hdr))) + 2;
			if (partremaining > 2)
			{
				/* Only allocate if there is going to be actual data */
				cur = calloc (partremaining + sizeof (cur->next) + sizeof (cur->logstamp), 1);
			}
		}
		else if (partremaining < intswap32 (curlog->first) || curlog->first == 0)
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
		while (partremaining > 0 && curstart < intswap32 (curlog->size))
		{
			int tocopy = partremaining;

			if (tocopy > intswap32 (curlog->size) - curstart)
			{
				tocopy = intswap32 (curlog->size) - curstart;
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

				if (cur->entry.log.transtype == intswap32 (ltCommit))
				{
					/* Mark the new end of what is to be committed */
					synctail = &cur->next;
					lastsynclog = start;
				}
			}

			cur = NULL;
			partread = 0;
			partremaining = 0;
			if (curstart + 2 <= intswap32 (curlog->size))
			{
				partremaining = intswap16 (*(unsigned short *)(buf + curstart + sizeof (log_hdr))) + 2;
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

	/* Clear out anything after the last commit if requested */
	if (committed)
	{
		while (*synctail)
		{
			cur = *synctail;
			*synctail = cur->next;
			free (cur);
		}
	}

	*end = lastsynclog;

	return 1;
}

/************************************************************************/
/* Try and determine what transaction type is used for inodes */
static int
mfs_log_find_inode_log_type (struct mfs_handle *mfshnd, unsigned int logstamp)
{
	struct log_entry_list *list;

	/* Search the previous 32 entries, ignoring if they have been committed or not */
	if (mfs_log_load_list (mfshnd, logstamp - 32, &logstamp, &list, 0) != 1)
		return 0;

	while (list)
	{
		struct log_entry_list *cur = list;
		list = cur->next;

		if (!mfshnd->inode_log_type)
		{
			switch (intswap32 (cur->entry.log.transtype))
			{
			case ltInodeUpdate:
				mfshnd->inode_log_type = ltInodeUpdate;
				break;
			case ltInodeUpdate2:
				mfshnd->inode_log_type = ltInodeUpdate2;
				break;
			}
		}

		free (cur);
	}
}

/************************************************************************/
/* Replay the transaction log and write a FSSync log entry */
int
mfs_log_fssync (struct mfs_handle *mfshnd)
{
	unsigned int lastsynclog = ~0;
	struct log_entry_list *list;

	if (mfs_log_load_list (mfshnd, ~0, &lastsynclog, &list, 1) != 1)
		return 0;

	if (list)
	{
		unsigned int startlogstamp = 0;
		if (!mfshnd->current_log)
		{
			startlogstamp = mfs_log_last_sync (mfshnd);
		}

		int ret = mfs_log_fssync_list (mfshnd, list, lastsynclog, 1);

		/* If this is the first time through and the inode log type hasn't */
		/* been determined, run a brief search through previous log entries */
		if (startlogstamp && !mfshnd->inode_log_type)
		{
			mfs_log_find_inode_log_type (mfshnd, startlogstamp);
		}

		/* Free the list */
		while (list)
		{
			struct log_entry_list *cur = list;
			list = cur->next;
			free (cur);
		}

		return ret;
	}

	return 1;
}
