#ifndef LOG_H
#define LOG_H

typedef struct log_hdr_s
{
	unsigned int logstamp;
	unsigned int crc;
	unsigned int first;
	unsigned int size;
}
log_hdr;

typedef struct log_entry_s
{
	unsigned short length;
	unsigned int unk1;
	unsigned int transmaj;
	unsigned int transmin;
	unsigned int inode;
	unsigned int transtype;
	unsigned int unk2;
}
__attribute__ ((packed)) log_entry;

typedef struct log_map_update_s
{
	log_entry log;
	unsigned int remove;
	unsigned int sector;
	unsigned int size;
	unsigned int unk;
}
__attribute__ ((packed)) log_map_update;

typedef struct log_inode_update_s
{
	log_entry log;
	unsigned int fsid;			/* This FSID */
	unsigned int refcount;		/* References to this FSID */
	unsigned int transmaj;
	unsigned int transmin;		/* Seems to be related to last ?transaction? block used */
	unsigned int inode;			/* Should be *sectornum - 1122) / 2 */
	unsigned int unk3;			/* Also block size? */
	unsigned int size;			/* In bytes or blocksize sized blocks */
	unsigned int blocksize;
	unsigned int blockused;
	unsigned int lastmodified;	/* In seconds since epoch */
	fsid_type type;				/* For files not referenced by filesystem */
	unsigned char unk6;			/* Always 8? */
	unsigned short beef;		/* Placeholder */
	unsigned int unk2;
	unsigned int dbsize;		/* Size of datablocks array / data in inode */
	struct
	{
		unsigned int sector;
		unsigned int count;
	}
	datablocks[0];
}
__attribute__ ((packed)) log_inode_update;

typedef union log_entry_all_u
{
	log_entry log;
	log_map_update zonemap;
	log_inode_update inode;
} log_entry_all;

typedef enum log_trans_types_e
{
	ltMapUpdate = 0,
	ltInodeUpdate = 1,
	ltCommit = 2,
	ltFsSync = 4
}
log_trans_types;

unsigned int mfs_log_last_sync (struct mfs_handle *mfshnd);
int mfs_log_read (struct mfs_handle *mfshnd, void *buf, unsigned int logstamp);
int mfs_log_write (struct mfs_handle *mfshnd, void *buf);

#endif /*LOG_H */
