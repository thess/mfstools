#ifndef FSID_H
#define FSID_H

/* Prime number used in hash for finding base inode of fsid. */
#define MFS_FSID_HASH 0x106d9

typedef enum fsid_type_e {
    tyNone=0,
    tyFile=1,
    tyStream=2,
    tyDir=4,
    tyDb=8,
} __attribute__ ((packed)) fsid_type;

/* For inode_flags below. */
#define INODE_CHAINED	0x80000000 /* I have no idea what this really is. */

typedef struct mfs_inode_s {
    unsigned int fsid;		/* This FSID */
    unsigned int refcount;	/* References to this FSID */
    unsigned int unk1;
    unsigned int unk2;		/* Seems to be related to last ?transaction? block used */
    unsigned int inode;		/* Should be *sectornum - 1122) / 2 */
    unsigned int unk3;		/* Also block size? */
    unsigned int size;		/* In bytes or blocksize sized blocks */
    unsigned int blocksize;
    unsigned int blockused;
    unsigned int lastmodified;	/* In seconds since epoch */
    fsid_type type;		/* For files not referenced by filesystem */
    unsigned char unk6;		/* Always 8? */
    unsigned short beef;	/* Placeholder */
    unsigned int sig;		/* Seems to be 0x91231ebc */
    unsigned int checksum;
    unsigned int inode_flags;	/* It seems to be flags at least. */
    unsigned int numblocks;	/* Number of data blocks.  0 = in this sector */
    struct {
	unsigned int sector;
	unsigned int count;
    } datablocks[0];
} mfs_inode;

typedef struct fs_entry_s {
    unsigned int fsid;
    unsigned char entry_length;
    fsid_type type;
    unsigned char name[0];
} fs_entry;

#endif/*FSID_H*/
