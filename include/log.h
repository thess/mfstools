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
	unsigned int bootcycles;		/* See comment in mfs.h */
	unsigned int bootsecs;			/* See comment in mfs.h */
	unsigned int fsid;
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
	unsigned int bootcycles;
	unsigned int bootsecs;
	unsigned int inode;
	unsigned int unk3;			/* Also block size? */
	unsigned int size;			/* In bytes or blocksize sized blocks */
	unsigned int blocksize;
	unsigned int blockused;
	unsigned int lastmodified;	/* In seconds since epoch */
	fsid_type type;
	unsigned char zone;
	unsigned short pad;
	unsigned int inodedata;		/* 1 if data in inode, 0 if blocks */
	unsigned int datasize;		/* Size of datablocks array / data in inode */
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
