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

unsigned int
mfs_log_nentries (struct mfs_handle *mfshnd)
{
	if (mfshnd->is_64)
	{
		return intswap32 (mfshnd->vol_hdr.v64.lognsectors);
	}
	else
	{
		return intswap32 (mfshnd->vol_hdr.v32.lognsectors);
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
		copystart += 512 - sizeof (log_hdr) - intswap32 (mfshnd->current_log->size);
		mfshnd->current_log->size = intswap32 (512 - sizeof (log_hdr));

		/* Write the (now full) log entry */
		mfs_log_write_current_log (mfshnd);

		if (intswap16 (entry->length) + 2 - copystart > 512 - sizeof (log_hdr))
		{
			mfshnd->current_log->first = intswap32 (512 - sizeof (log_hdr));
		}
		else
		{
			mfshnd->current_log->first = intswap32 (intswap16 (entry->length) + 2 - copystart);
		}
	}

	if (copystart < intswap16 (entry->length) + 2)
	{
		memcpy ((unsigned char *)(mfshnd->current_log + 1) + intswap32 (mfshnd->current_log->size), (unsigned char *)entry + copystart, intswap16 (entry->length) + 2 - copystart);
		mfshnd->current_log->size = intswap32 (intswap32 (mfshnd->current_log->size) + intswap16 (entry->length) + 2 - copystart);
	}

	return 1;
}

int
mfs_log_zone_update (struct mfs_handle *mfshnd, unsigned int fsid, uint64_t sector, uint64_t size, int state)
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
		int oldstate = mfs_zone_map_block_state (mfshnd, sector, size);
		if (oldstate < 0)
			return 0;

		entry.log.length = intswap16 (sizeof (log_map_update_64) - 2);
		entry.log.transtype = intswap32 (ltMapUpdate64);
		entry.zonemap_64.remove = state? intswap32 (1) : 0;
		entry.zonemap_64.sector = intswap64 (sector);
		entry.zonemap_64.size = intswap64 (size);
		/* Flag is 1 if the new state matches the state it had been at the */
		/* start of the transaction.  For example, if allocating a block */
		/* from a larger block.  But not if that larger block is a result */
		/* of freeing something else this transaction. */
		/* Theoretically, the case of freeing something that was */
		/* free at the start of the transaction is an error, either */
		/* in code (Double-freeing) or transactional logic */
		/* (Allocate-free within a single transaction). */
		entry.zonemap_64.flag = (oldstate? 0: 1) ^ (state? 1: 0) ^ 1;
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
				if (mfs_log_zone_update (mfshnd, fsid,
					intswap64 (inode1->datablocks.d64[loop].sector),
					intswap32 (inode1->datablocks.d64[loop].count), newstate) <= 0)
					return 0;
			}
			else
			{
				if (mfs_log_zone_update (mfshnd, fsid,
					intswap32 (inode1->datablocks.d32[loop].sector),
					intswap32 (inode1->datablocks.d32[loop].count), newstate) <= 0)
					return 0;
			}
		}
	}

	return 1;
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
	else
	{
		oldinode = NULL;
	}

	/* Get the fsid for log entries, just in case it's not in the inode block anymore (IE a free) */
	if (inode->fsid)
		fsid = intswap32 (inode->fsid);
	else if (oldinode && oldinode->fsid)
		fsid = intswap32 (oldinode->fsid);

	if (oldinode)
	{
		/* Free any blocks no longer in use */
		if (mfs_log_zone_update_for_inodes (mfshnd, oldinode, inode, fsid, 1) <= 0)
			return 0;
	}

	if (inode->refcount && inode->fsid)
	{
		/* Allocate any blocks now in use */
		if (mfs_log_zone_update_for_inodes (mfshnd, inode, oldinode, fsid, 0) <= 0)
			return 0;

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
				datasize = intswap32 (inode->numblocks) * sizeof (inode->datablocks.d32[0]);
			}
		}
	}

	entry = alloca (offsetof (log_inode_update, datablocks) + datasize);

	/* Generic log stuff */
	entry->log.length = intswap16 ((offsetof (log_inode_update, datablocks) + datasize - 2 + 1) & ~1);
	entry->log.unk1 = 0;
	entry->log.bootcycles = intswap32 (mfshnd->bootcycle);
	entry->log.bootsecs = intswap32 (mfshnd->bootsecs);
	entry->log.fsid = intswap32 (fsid);
	entry->log.transtype = intswap32 (mfshnd->inode_log_type);
	entry->log.unk2 = 0;

	/* Copy the inode */
	entry->fsid = inode->fsid;
	entry->refcount = inode->refcount;
	entry->bootcycles = intswap32 (mfshnd->bootcycle);
	entry->bootsecs = intswap32 (mfshnd->bootsecs);
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

	return mfs_log_add_entry (mfshnd, &entry->log);
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
mfs_log_commit_list (struct mfs_handle *mfshnd, struct log_entry_list *list, unsigned int logstamp)
{
	struct log_entry_list *cur;
	struct log_entry_list *last = NULL;

	/* Commit each log entry in turn */
	for (cur = list; cur; cur = cur->next)
	{
		switch (intswap32 (cur->entry.log.transtype))
		{
			case ltMapUpdate:
				if (mfs_zone_map_update (mfshnd, intswap32 (cur->entry.zonemap_32.sector), intswap32 (cur->entry.zonemap_32.size), intswap32 (cur->entry.zonemap_32.remove), cur->logstamp) < 1)
				{
					return 0;
				}
				break;
			case ltMapUpdate64:
				if (mfs_zone_map_update (mfshnd, intswap64 (cur->entry.zonemap_64.sector), intswap64 (cur->entry.zonemap_64.size), intswap32 (cur->entry.zonemap_64.remove), cur->logstamp) < 1)
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

		last = cur;
	}

	mfshnd->lastlogcommit = logstamp;
	if (mfshnd->is_64)
	{
		struct zone_map *zone;

		for (zone = mfshnd->loaded_zones; zone; zone = zone->next_loaded)
		{
			if (zone->dirty && intswap32 (zone->map->z64.logstamp) < logstamp)
			{
				zone->map->z64.logstamp = intswap32 (logstamp);
			}
		}

		mfshnd->vol_hdr.v64.logstamp = intswap32 (logstamp);
		if (last)
		{
			mfshnd->vol_hdr.v64.bootcycles = last->entry.log.bootcycles;
			mfshnd->vol_hdr.v64.bootsecs = last->entry.log.bootsecs;
		}
	}
	else
	{
		struct zone_map *zone;

		for (zone = mfshnd->loaded_zones; zone; zone = zone->next_loaded)
		{
			if (zone->dirty && intswap32 (zone->map->z32.logstamp) < logstamp)
			{
				zone->map->z32.logstamp = intswap32 (logstamp);
			}
		}

		mfshnd->vol_hdr.v32.logstamp = intswap32 (logstamp);
		if (last)
		{
			mfshnd->vol_hdr.v32.bootcycles = last->entry.log.bootcycles;
			mfshnd->vol_hdr.v32.bootsecs = last->entry.log.bootsecs;
		}
	}

	return 1;
}

/************************************************************************/
/* Take a list of transactions and replay them */
static int
mfs_log_fssync_list (struct mfs_handle *mfshnd, struct log_entry_list *list)
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
				break;
			case ltFsSync:
				/* Record the last log sync, but replay the whole list anyway */
				mfshnd->lastlogsync = cur->logstamp;
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

	for (cur = list; cur; cur = cur->next)
	{
		if (cur->entry.log.transtype == intswap32 (ltCommit))
		{
			int ret;
			struct log_entry_list *next = cur->next;
			cur->next = NULL;

			ret = mfs_log_commit_list (mfshnd, list, cur->logstamp);
			cur->next = next;
			list = next;

			if (ret <= 0)
			{
				return ret;
			}
		}
	}

	return 1;
}

/************************************************************************/
/* Load a list of transactions. */
/* If the start is passed in as ~0, it is assumed to be the last successful sync */
/* If the end is passed as ~0, it is assumed to be the last log written */
int
mfs_log_load_list (struct mfs_handle *mfshnd, unsigned int start, unsigned int end, struct log_entry_list **list)
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
	for (; start <= end && mfs_log_read (mfshnd, (void *)buf, start) >= 512; start++)
	{
		unsigned int curstart = 0;

		if (!cur)
		{
			/* If it started with a partial, read it that way */
			if (curlog->first)
			{
				if (*list)
				{
					/* Not the first entry and we missed something - that's bad */
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

				curstart = intswap32 (curlog->first);
				if (curstart >= intswap32 (curlog->size))
				{
					continue;
				}
			}

			/* Start a new log entry */
			partread = 0;
			partremaining = intswap16 (*(unsigned short *)(buf + curstart + sizeof (log_hdr))) + 2;
			if (partremaining > 2)
			{
				/* Only allocate if there is going to be actual data */
				cur = calloc (partremaining + offsetof (struct log_entry_list, entry), 1);
				cur->logstamp = start;
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
					cur = calloc (partremaining + offsetof (struct log_entry_list, entry), 1);
					cur->logstamp = start;
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

	return 1;
}

/************************************************************************/
/* Try and determine what transaction type is used for inodes */
static int
mfs_log_find_inode_log_type (struct mfs_handle *mfshnd, unsigned int logstamp)
{
	struct log_entry_list *list;

	/* Search the previous 32 entries, ignoring if they have been committed or not */
	if (mfs_log_load_list (mfshnd, logstamp < 32? 0: logstamp - 32, logstamp, &list) != 1)
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
	log_entry entry;
	entry.length = intswap16 (sizeof (entry) - 2);
	entry.unk1 = 0;
	entry.unk2 = 0;
	entry.fsid = 0;

	/* If this is the first time, replay the log first */
	if (!mfshnd->current_log)
	{
		struct log_entry_list *list;
		unsigned int startlogstamp = mfs_log_last_sync (mfshnd);

		if (mfs_log_load_list (mfshnd, startlogstamp + 1, ~0, &list) != 1)
			return 0;

		if (list)
		{
			int ret = mfs_log_fssync_list (mfshnd, list);

			/* Free the list */
			while (list)
			{
				struct log_entry_list *cur = list;
				list = cur->next;
				free (cur);
			}

			if (ret <= 0)
			{
				return ret;
			}
		}

		/* If this is the first time through and the inode log type hasn't */
		/* been determined, run a brief search through previous log entries */
		if (startlogstamp && !mfshnd->inode_log_type)
		{
			mfs_log_find_inode_log_type (mfshnd, startlogstamp);
		}

		if (!mfshnd->lastlogcommit)
		{
			mfshnd->lastlogcommit = startlogstamp;
		}
		/* This shouldn't happen, the fssync entry should have been loaded */
		/* and recorded */
		if (!mfshnd->lastlogsync)
		{
			mfshnd->lastlogsync = startlogstamp;
		}

		mfshnd->current_log = calloc (512, 1);
		mfshnd->current_log->logstamp = intswap32 (mfshnd->lastlogsync + 1);
		mfshnd->current_log->crc = 0xdeadf00d;
		mfshnd->current_log->size = 0;
		mfshnd->current_log->first = 0;

		/* Log the log replay */
		entry.bootcycles = intswap32 (mfshnd->bootcycle);
		entry.bootsecs = intswap32 (mfshnd->bootsecs);
		entry.transtype = intswap32 (ltLogReplay);
		mfs_log_add_entry (mfshnd, &entry);
	}
	else if (mfshnd->current_log->first || mfshnd->current_log->size)
	{
		mfshnd->err_msg = "Call to FS Sync with outstanding transactions";
		return 0;
	}
	else if (mfshnd->current_log->logstamp == intswap32 (mfshnd->lastlogsync + 1))
	{
		/* Already synced */
		return 1;
	}
	else if (mfshnd->current_log->logstamp != intswap32 (mfshnd->lastlogcommit + 1))
	{
		mfshnd->err_msg = "Call to FS Sync without calling commit first";
		return 0;
	}

	/* Log the FS Sync */
	entry.bootcycles = intswap32 (mfshnd->bootcycle);
	entry.bootsecs = intswap32 (++mfshnd->bootsecs);
	entry.transtype = intswap32 (ltFsSync);
	mfs_log_add_entry (mfshnd, &entry);
	mfs_log_write_current_log (mfshnd);

	/* Increment it again so this transaction will be distinct from */
	/* updates before the next transaction */
	mfshnd->bootsecs++;

	if (mfs_zone_map_commit (mfshnd, mfshnd->lastlogcommit) <= 0)
	{
		return 0;
	}
	if (mfs_write_volume_header (mfshnd) <= 0)
	{
		return 0;
	}

	return 1;
}

int
mfs_log_commit (struct mfs_handle *mfshnd)
{
	uint32_t endlog;
	log_entry entry;
	struct log_entry_list *list;

	/* Start with a clean structure */
	memset (&entry, 0, sizeof (entry));

	entry.length = intswap16 (sizeof (entry) - 2);
	entry.bootcycles = intswap32 (mfshnd->bootcycle);
	/* Increment the seconds for thenext set of data to commit */
	entry.bootsecs = intswap32 (mfshnd->bootsecs++);
	entry.transtype = intswap32 (ltCommit);
	entry.unk1 = 0;
	entry.unk2 = 0;
	entry.fsid = 0;

	if (mfs_log_add_entry (mfshnd, &entry) <= 0)
		return 0;

	/* Add entry should never leave the pointer right at the end, so this is safe. */
	/* Use the zeroed out structure as a zero length entry */
	mfshnd->current_log->size = intswap32 (intswap32 (mfshnd->current_log->size) + 2);
	endlog = intswap32 (mfshnd->current_log->logstamp);

	if (mfs_log_write_current_log (mfshnd) <= 0)
		return 0;

	if (mfs_log_load_list (mfshnd, mfshnd->lastlogcommit + 1, endlog, &list) <= 0)
		return 0;

	if (mfs_log_commit_list (mfshnd, list, endlog) <= 0)
		return 0;

	/* Perform a periodic fssync */
	if (mfshnd->lastlogcommit - mfshnd->lastlogsync > mfs_log_nentries (mfshnd) / 2)
	{
		return mfs_log_fssync (mfshnd);
	}

	return 1;
}
