#include <stdio.h>
#include <malloc.h>

#include "mfs.h"
#include "backup.h"

struct blocklist {
	int backup;
	unsigned int sector;
	struct blocklist *next;
	//struct blocklist *prev;
};

static struct blocklist *alloc_block (struct blocklist **pool)
{
	struct blocklist *newblock = *pool;

	if (newblock) {
		*pool = newblock->next;
		newblock->sector = 0;
		newblock->backup = 0;
		newblock->next = 0;
		//newblock->prev = 0;
	} else {
		newblock = calloc (sizeof (*newblock), 1);
	}

	return newblock;
}

static void free_block (struct blocklist **pool, struct blocklist *block)
{
	block->next = *pool;
	*pool = block;
}

static void free_block_list (struct blocklist **blocks)
{
	while (*blocks) {
		struct blocklist *tmp = *blocks;
		*blocks = tmp->next;
		free (tmp);
	}
}

static int backup_add_block (struct blocklist **blocks, struct blocklist **pool, int sector, int count)
{
	struct blocklist **loop;
	struct blocklist *prev = 0;

fprintf (stderr, "Adding block %d of %d\n", sector, count);

	for (loop = blocks; *loop && (*loop)->sector < sector; loop = &((*loop)->next)) {
		prev = *loop;
	}

	if (!*loop) {
		struct blocklist *newblock;
		newblock = alloc_block (pool);
		if (!newblock) {
			return -1;
		}
		newblock->next = alloc_block (pool);
		//newblock->prev = prev;
		newblock->backup = 1;
		newblock->sector = sector;
		if (!newblock->next) {
			return -1;
		}
		newblock->next->next = *loop;
		//newblock->next->prev = newblock;
		newblock->next->sector = sector + count;
		*loop = newblock;
	} else if ((*loop)->backup) {
		if (sector + count >= (*loop)->sector) {
			(*loop)->sector = sector;
		} else {
			struct blocklist *newblock;
			newblock = alloc_block (pool);
			if (!newblock) {
				return -1;
			}
			newblock->next = alloc_block (pool);
			//newblock->prev = prev;
			newblock->backup = 1;
			newblock->sector = sector;
			if (!newblock->next) {
				return -1;
			}
			newblock->next->next = *loop;
			//newblock->next->prev = newblock;
			newblock->next->sector = sector + count;
			*loop = newblock;
		}
	} else {
		if (prev && (*loop)->sector < sector + count) {
			(*loop)->sector = sector + count;
		} else if (!prev) {
			struct blocklist *newblock;
			newblock = alloc_block (pool);
			if (!newblock) {
				return -1;
			}
			newblock->next = alloc_block (pool);
			//newblock->prev = *loop;
			newblock->backup = 1;
			newblock->sector = sector;
			if (!newblock->next) {
				return -1;
			}
			newblock->next->next = (*loop)->next;;
			//newblock->next->prev = newblock;
			newblock->next->sector = sector + count;
			*loop = newblock;
			loop = &newblock->next;
		}
	}

	while ((*loop)->next && ((*loop)->next->sector < (*loop)->sector ||
				 (*loop)->next->backup == (*loop)->backup)) {
		struct blocklist *oldblock = (*loop)->next;
		(*loop)->next = oldblock->next;
		free_block (pool, oldblock);
	}

	return 0;
}

static struct backup_info *make_backup_info (struct blocklist *blocks)
{
	struct backup_info *info;
	struct blocklist *loop;
	int count = 0;

	info = calloc (sizeof (*info), 1);

	if (!info) {
		return 0;
	}

	for (loop = blocks; loop; loop = loop->next) {
		if (loop->backup) {
			info->nblocks++;
		}
	}

	info->blocks = calloc (sizeof (struct backup_block), info->nblocks);

	if (!info->blocks) {
		free (info);
		return 0;
	}

	for (loop = blocks; loop; loop = loop->next) {
		if (loop->backup) {
			info->blocks[count].firstsector = loop->sector;
			info->blocks[count].sectors = loop->next->sector - loop->sector;
			count++;
		}
	}

	info->presector = (info->nblocks * 4 + 511) & ~511;

	return info;
}

struct backup_info *init_backup (unsigned int thresh)
{
	int loop, loop2, loop3;
	int ninodes = mfs_inode_count ();
	struct blocklist *blocks = NULL;
	struct blocklist *pool = NULL;
	struct backup_info *info;

	blocks = calloc (sizeof (*blocks), 1);
	if (!blocks) {
		return 0;
	}

	for (loop = 0, loop3 = 1; loop2 = mfs_volume_size (loop); loop += loop2, loop3 ^= 1) {
		if (loop3) {
			if (backup_add_block (&blocks, &pool, loop, loop2) != 0) {
				free_block_list (&blocks);
				free_block_list (&pool);
			}
		}
	}

	for (loop = 0; loop < ninodes; loop++) {
		mfs_inode *inode = mfs_read_inode (loop);

		if (inode) {
			if (inode->type == tyStream) {
				unsigned int streamsize = htonl (inode->blocksize) / 512 * htonl (inode->blockused);
				if (streamsize < thresh) {
					for (loop2 = 0; loop2 < htonl (inode->numblocks); loop2++) {
						unsigned int thissector = htonl (inode->datablocks[loop2].sector);
						unsigned int thiscount = htonl (inode->datablocks[loop2].count);

						if (thiscount > streamsize) {
							thiscount = streamsize;
						}
						if (backup_add_block (&blocks, &pool, thissector, thiscount) != 0) {
							free_block_list (&blocks);
							free_block_list (&pool);
						}
						streamsize -= thiscount;

						if (streamsize == 0) {
							break;
						}
					}
					
				}
			}
			free (inode);
		}
	}

	info = make_backup_info (blocks);

	free_block_list (&blocks);
	free_block_list (&pool);

	return info;
}
