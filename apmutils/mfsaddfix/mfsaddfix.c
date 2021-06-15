/* mfsaddfix.c
 * This program is to be used on Series 5 and later TiVos after using MFSTools
 * to add a pair of partitions to an internal drive.  It will move partitions 
 * /dev/sda15 and /dev/sda16 to one of the place holder partitions /dev/sda2 and
 * /dev/sda3, /dev/sda4 and /dev/sda5, or /dev/sda6 and /dev/sda7 if they have
 * not already been used for this purpose.  Afterwards, it will correct the 
 * MFS header to reflect the change.
 * 
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <zlib.h>

#define TO_CPY 92
#define SZ 512
#define crcinit 0xFFFFFFFF

void partitionreset(int fd, int lpcount, char eswap){
	int w,i;
	unsigned long pcount = 14;
	off_t t;
	unsigned char block[SZ];

	t = lseek(fd, 15*SZ, SEEK_SET);
	if(t < 0){perror("lseek"); exit(1);}
	memset(block, 0, SZ);
	for (i = 0; i < (lpcount-14); i++) {
		w=write(fd, block, SZ);
		if(w < 0){perror("write"); exit(1);}
	}
	//Let us make the APM congruent to the number of existing partitions.
	i = 1;
	t = lseek(fd, SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	if (eswap) {
    	pcount=(pcount>>24) | 
        ((pcount<<8) & 0x00FF0000) |
        ((pcount>>8) & 0x0000FF00) |
        (pcount<<24);
	}
	while(read(fd, block, SZ) > 0 && i <= 14){
		memcpy((block+4), &pcount, 4);		
        t = lseek(fd, i*SZ, SEEK_SET);
        if(t < 0){perror("lseek"); exit(1);}
        w=write(fd, block, SZ);
        if(w < 0){perror("write"); exit(1);}
       	i++;
	}
	return;
}

unsigned short endian_swap16(unsigned short x) {
    x = (x>>8) | 
        (x<<8);
	return(x);
}

unsigned long endian_swap32(unsigned long x) {
    x = (x>>24) | 
        ((x<<8) & 0x00FF0000) |
        ((x>>8) & 0x0000FF00) |
        (x<<24);
	return(x);
}

unsigned long long endian_swap64(unsigned long long x) {
    x = (x>>56) | 
        ((x<<40) & 0x00FF000000000000) |
        ((x<<24) & 0x0000FF0000000000) |
        ((x<<8)  & 0x000000FF00000000) |
        ((x>>8)  & 0x00000000FF000000) |
        ((x>>24) & 0x0000000000FF0000) |
        ((x>>40) & 0x000000000000FF00) |
        (x<<56);
	return(x);
}

void usage(){
    fprintf(stderr,"Usage:  /dev/sdX\nReplace X with the actual letter of the drive.\n");
    exit(1);
}

void warning(){
    char *w = "This program is designed to work on series 5 and later\n"
            "TiVos to allow an internal drive be expanded by MFSTools without\n"
			"causing the TiVo to reformat the drive when it is returned to the TiVo.\n"
			"Use in any other series TiVo may result in loss of data.\n\n"
			"This program will make sure that there is space for the added\n"
			"partitions to be relocated followed by relocating them to 'place holder'\n"
			"partitions located in the lower partition range and correcting the MFS\n"
			"header.  If not, it will then erase the added partitions from the APM\n"
			"so when the drive is put back into the TiVo, it will divorce the partitions\n"
			"and correct the issue without losing your recordings.\n"
			"In some cases a green screen bootloop may happen.  In this case try\n"
			"a kickstart 58 code on a reboot.  If that does not fix it, then\n"
			"a reimage of the drive will be needed.\n\n"
			"This utility is provided without warranty or guarantee that it will\n"
            "perform as intended.  All effort has been made to ensure a\n"
            "successful outcome.  Use at your own risk.\n"
            "Press 'y' if you want to continue. Any other key to exit.\n\n";

    printf("%s", w);

}

int main(int argc, char** argv)
{
    if(argc != 2) usage();

	short endian_test = 1;
	char *c = (char*)&endian_test;
	char eswap = 0;
	int fd;
	int i,j,w,s;
	int count = 0;
	int coalesce = 0;
    off_t t;
	int offset = 36;
 	char* path = argv[1];
    char* out = argv[2];
	unsigned char block[SZ];
	unsigned char block15[SZ];
	unsigned char block16[SZ];
	unsigned char tempblock[SZ];
	unsigned char proceed;
    unsigned char cmp[22] = " /dev/sda15 /dev/sda16";
	unsigned char deadfood[4] = {0x0D, 0xF0, 0xAD, 0xDE}; 
	unsigned long crc=crc32(0L, Z_NULL, 0);
	unsigned long pcount = 14;
	unsigned long lpcount;
	unsigned long long psize = 0;
	unsigned long long pstart = 0;
	unsigned long long psize15 = 0;
	unsigned long long pstart15 = 0;
	unsigned long long psize16 = 0;
	unsigned long long pstart16 = 0;

	
    warning();

    fscanf(stdin, "%c", &proceed);
    printf("\n");

    if (proceed != 'y' && proceed != 'Y') exit(0);

    //endian test
		
	if (*c) {
		fprintf(stdout, "Little endian computer detected.\n\n");
	}
	else {
	    eswap = 1;
		fprintf(stdout, "Big endian computer detected.\n\n");
	}

	//open the drive
	fd = open(path, O_RDWR);
    if(fd < 0){perror("open");exit(1);}
	
	//read block 0
	if(read(fd, block, SZ) < 0){
		perror("read");
		exit(1);
	}

	// Test for Series 5 & 6 Tivo
	// First make sure we have the correct signature for a TiVo drive
	if(block[0] != 0x92 || block[1] != 0x14){
		fprintf(stderr, "Error: Not a Series 5 and later TiVo drive. Signature expected to be 9214 but is %x%x\nUnable to process drive.\n\n", block[0], block[1]);
		exit(1);
	}
	fprintf(stdout, "Drive has expected TiVo signature.\nWill begin to process drive.\n\n");	

	// Although Apple_Free partitions are innocuous, they can problematic with some 3rd part TiVo tools.
	//Consequently it is easier just to trim them off.  They should be at the end of the partition map.  Any in the middle of the partition map will stay,
	//but then we are dealing with a non-standard TiVo partition map and should probably quit.
	
	//First read in the current partition count
	fprintf(stdout, "Pruning Apple_Free partitions....\n");
	t = lseek(fd, SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	if(read(fd, block, SZ) < 0){
		perror("read");
		exit(1);
	}
	memcpy(&pcount,(block + 4), 4);
	// If we are working on a little endian machine, lets correct the values so we can work with them
	if (eswap) {
    	pcount=endian_swap32(pcount);
	}
	lpcount = pcount;
	
	//Go to the last APM entry
	t = lseek(fd, pcount*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	if(read(fd, block, SZ) < 0){
		perror("read");
		exit(1);
    }

	//Lets start deleteing the entries and decrement the total number of blocks while we are at it.
	while (pcount >= 14 && !(memcmp((block + 56),"Apple_Free",10) && memcmp((block + 48),"Apple_Free",10))) {
	    memset(block, 0, 512);
		t = lseek(fd, pcount*512, SEEK_SET);
		if(t < 0){perror("lseek"); exit(1);}
		w=write(fd, block, SZ);
		if(w < 0){perror("write"); exit(1);}
		pcount--;
		t = lseek(fd, pcount*512, SEEK_SET);
		if(t < 0){perror("lseek"); exit(1);}
		if(read(fd, block, SZ) < 0){
            perror("read");
            exit(1);
        }
    }

	//Now lets reset the APM entries to reflect the number of blocks left after trimming off the Apple_Free partitions.  We could just wait until the end, but if there is an error sometime before then, it would be nice to have a valid APM
    count= 1;
	t = lseek(fd, SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	
	//count here is inclusive and now reset the value
	while(read(fd, block, SZ) > 0 && count <= pcount){
		memcpy((block+4), &pcount, 4);		
        t = lseek(fd, count*SZ, SEEK_SET);
        if(t < 0){perror("lseek"); exit(1);}
        w=write(fd, block, SZ);
        if(w < 0){perror("write"); exit(1);}
        count++;
	}
	fprintf(stdout, "Pruning Apple_Free partitions complete.\n\n");	
	
 
	//Check to see if mfsadd has added any partitions and that we have exactly 16 partitions in APM and usable partitions between sda2 to sda7 inclusive
	//Read block 1 and check for number of partitions.
	t = lseek(fd, SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	if(read(fd, block, SZ) < 0){
		perror("read");
		exit(1);
	}
	memcpy(&pcount,(block + 4), 4);
	// If we are working on a little endian machine, lets correct the values so we can work with them
	if (eswap) {
    	pcount=endian_swap32(pcount);
	}
	if (pcount!=16){
		//Not the correct number of partitions
		fprintf(stderr,"Incorrect number of partitions found.\nExpected 16 and found %d.\nUnable to process drive.\n\n",pcount);
		exit(1);
	}
	//since we have the correct number of partititions, will convert everything to a 64 bit partition structure to make things easier later on.

    fprintf(stdout, "Converting to 64 bit APM in progress....\n");

	count= 2;
	//count here is inclusive
	while(read(fd, block, SZ) > 0 && count <= pcount){
		if(block[0] == 'M' ){
			memset(tempblock, 0, SZ);

			i=j=0;
			//this introduces the correct offsets for certain 
			//blocks to become 64 bit quants. 
			for(i = 0; i < TO_CPY; i++){
				if(i == 12) j+=4;
				if(i == 16) j+=4;
				if(i == 84) j+=4;
				if(i == 88) j=156;

				tempblock[j] = block[i];
				j++;
			}
			tempblock[0] = 'N';

			memcpy(block, tempblock, SZ);
		}	
            t = lseek(fd, count*SZ, SEEK_SET);
            if(t < 0){perror("lseek"); exit(1);}
            w=write(fd, block, SZ);
            if(w < 0){perror("write"); exit(1);}

            if (count == 15) memcpy(block15, block, SZ);
            
            if (count == 16) memcpy(block16, block, SZ);
      
        count++;
    }
    fprintf(stdout, "Conversion to 64 bit APM complete.\n\n");


	//Check to see if the added partitions are of the correct type

	if (memcmp((block15 + 56),"MFS",3) || memcmp((block16 + 56),"MFS",3)) {
	        fprintf(stderr, "Partition structure is not as expected. Partition 15 & 16 not of MFS type.\nPartition 15 is of %.32s type and Partition 16 is of %.32s type.\nUnable to coalesce.\nProcessing of drive is incomplete.\n\n",(block15+56),(block16+56));
        exit(1);
	}

	//Check to see if the added partitions are able to be coalesced or not.  If they are then coalesce them and then we need only one open space rather than 2.
    // The "first partition block" plus the "partition block count" of partition 15 equals the "first partition block" of partition 16
	// The total size should be under 2TiB, unless of course TiVo fixes the bug on their side.
    // If the above parameters are met, then we are good to go

	fprintf(stdout, "Contemplating if coalescing the added partitions makes sense.\n");

	// Only two more tests that need to be satisfied and so we need to set it up the variables and switch endianess if necessary
	memcpy(&pstart15,(block15 + 8),8);
	memcpy(&psize15,(block15 + 16),8);
	memcpy(&pstart16,(block16 + 8),8);
	memcpy(&psize16,(block16 + 16),8);

	// If we are working on a little endian machine, lets correct the values so we can work with them
	if (eswap) {
	    pstart15=endian_swap64(pstart15);
	    psize15=endian_swap64(psize15);
	    pstart16=endian_swap64(pstart16);
	    psize16=endian_swap64(psize16);
	}

	if (psize15 > 0xFFFFFFFF) {fprintf(stderr,"Added partition 15 size is too large.\nActual size is %lld bytes but should not exceed %lld bytes.\nThe MFS partition needs to shrink by at least %lld bytes.\nUnable to proceed.\nProcessing of drive is incomplete.\nResetting APM. TiVo should ask you to divorce the external drive.\nPlease go and do so. If you start having a green screen boot loop, run kisckstart code 58.\nIf that dooes not work, then the drive will need to be reimaged.\n\n", psize15,0xFFFFFFFF,(psize15 - 0xFFFFFFFF)); partitionreset(fd, lpcount, eswap); exit(1);}

	if (psize16 > 0xFFFFFFFF) {fprintf(stderr,"Added partition 16 size is too large.\nActual size is %lld bytes but should not exceed %lld bytes.\nThe MFS partition needs to shrink by at least %lld bytes.\nUnable to proceed.\nProcessing of drive is incomplete.\nResetting APM. Tivo should ask you to divorce the external drive.\nPlease go and do so. If you start having a green screen boot loop, run kisckstart code 58.\nIf that dooes not work, then the drive will need to be reimaged.\n\n", psize16,0xFFFFFFFF,(psize16 - 0xFFFFFFFF)); partitionreset(fd, lpcount, eswap); exit(1);}

	if (((pstart15 + psize15) == pstart16) && ((psize15 + psize16) <= 0xFFFFFFFF)) {
		coalesce = 1; 
		fprintf(stdout,"Coalescing makes sense.\n\n");
	}
	else {
		fprintf(stdout,"Coalescing does not make sense.\n\n");
	}

	//Now make sure there is enough space to move the partitions.  We have 2 cases.  One with coalesce and one without.
	i = 2;
	j = 0;
    t = lseek(fd, i*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	if (coalesce) {
		while (read(fd, block, SZ) > 0 && i <= 7 && j < 1){
			memset(&psize, 0, sizeof(psize));
			memcpy(&psize,(block + 16),8);
			// If we are working on a little endian machine, lets correct the values so we can work with them
			if (eswap) {
	    	psize=endian_swap64(psize);
			}
			if (psize <= 8) {
				j++;
			}
			i++;
		}
		if (j < 1) {
			fprintf(stderr,"No free partitions found.  Erasing MFSTools added partition pair.\nTiVo should prompt you to divorce extrenal drive.\nPlease go and do so.  If you start having a green screen boot loop, run kisckstart code 58.\nIf that dooes not work, then the drive will need to be reimaged.\n\n");
			partitionreset(fd, lpcount, eswap);
			fprintf(stderr,"APM reset completed.\n\n");
			exit(1);
		}
	}
	else {
		while (read(fd, block, SZ) > 0 && i <= 7 && j < 2){
			memset(&psize, 0, sizeof(psize));
			memcpy(&psize,(block + 16),8);
			// If we are working on a little endian machine, lets correct the values so we can work with them
			if (eswap) {
	    	psize=endian_swap64(psize);
			}
			if (psize <= 8) {
				j++;
			}
			else if (j > 0) {
				j--;
			}
			i++;
		}
		if (j < 2) {
			fprintf(stderr,"No free partitions pairs found.  Erasing MFSTools added partition pair.\nTiVo should prompt you to divorce extrenal drive.\nPlease go and do so.   If you start having a green screen boot loop, run kisckstart code 58.\nIf that dooes not work, then the drive will need to be reimaged.\n\n");
			partitionreset(fd, lpcount, eswap);
			fprintf(stderr,"APM reset completed.\n\n");
			exit(1);
		}
	}


	// passed that last test so now proceed to coalesce. First we calculate the size of the coalesced partition and store it in the correct locations followed by writing the block.  Then reset the APM partition count.
	if (coalesce) {
		//If we are here then we can coalesce the partition pair and have space to move them.
		//First we calculate the new size then store it in the correct place in the APM entry followed by moving the APM entry to the correct place in the APM.
		//Then we reset the APM partition count.
		psize15=psize15+psize16;
		// If we are working on a littel endian machine, correct the value to write it correctly to the file
		if (eswap) psize15=endian_swap64(psize15);
		//Now copy the size to the appropriate places in the block
		memcpy((block15+16),&psize15,8);
		memcpy((block15+96),&psize15,8);
		memcpy((block15+24),"MFS application/media region\0\0",30);
		//Write the new APM entry
		s = i - 1;
		fprintf(stdout,"Moving coalesced partitions 15 and 16 to %d.\n", s);
		t = lseek(fd, s*SZ, SEEK_SET);
	    if(t < 0){perror("lseek"); exit(1);}
	    w = write(fd, block15, SZ);
	    if(w < 0){perror("write"); exit(1);}
	}
	else {
		//If we get here, then we found a pair of partitions to use.  
		//Let us get started.  All we need to do is physically copy the information from partition 15 and 16 to the partition pair we found followed by resetting
		//the APM partition count.
		s = i - 2;
		fprintf(stdout,"Moving partitions 15 and 16 to %d and %d\n", s,s+1);
		t = lseek(fd, s*SZ, SEEK_SET);
		if(t < 0){perror("lseek"); exit(1);}
	    w=write(fd, block15, SZ);
	    if(w < 0){perror("write"); exit(1);}
	    w=write(fd, block16, SZ);
	    if(w < 0){perror("write"); exit(1);}
	}
    //We need to reset the APM to 14 partitions since we moved partitions.
	partitionreset(fd, lpcount, eswap);

	fprintf(stdout,"Finished moving parititons.\n\nBegin correcting the MFS header.\n");

	//Lets now correct the MFS header so we do not have to force a divorce of the now non-existant partition or worse a reformat of the drive.
	t = lseek(fd, 10*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	read(fd, block, SZ);
	memset(&psize, 0, sizeof(psize));
	memset(&pstart, 0, sizeof(pstart));

	//Read the starting block for partition 10 and the size of partition 10 so we know where to go.  The header is the first block of partition 10 and the backup header is the last block of partition 10.
	memcpy(&pstart,(block + 8), 8);
	memcpy(&psize,(block + 16),8);
	// If we are working on a little endian machine, lets correct the values so we can work with them
	if (eswap) {
   		psize=endian_swap64(psize);
		pstart=endian_swap64(pstart);
	}

	fprintf(stdout,"Evaluating MFS header to see if it can be appropriately modified to complete the process.\n");
	//Now lets go to the MFS header and read it in.
	t = lseek(fd, pstart*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	t=read(fd, block, SZ);
	if(t < 0){perror("read"); exit(1);}

	//Now let fix the header.  Here we have to replace references of /dev/sda15 /dev/sda16 with where we moved the partitions to so lets look for it	
	for (i = offset; i < 132 + offset; i++){
		j = memcmp(&block[i], &cmp[0], 22);
		if(j == 0)break;// Found it!
	} 	
	if(j != 0){
		//Something is wrong, could not find reference to /dev/sda15 /dev/sda16 in the MFS header.  See if the Tivo can fix it since we cannot.
		fprintf(stderr,"Error: Unexpected finding.\nCould not find reference to /dev/sda15 /dev/sda16 in MFS header.\nRecommend placing drive in the TiVo to see if it can fix this.\nYou may see a screen that indicates you need to divorce an external drive.\nGo ahead and do so.  If you start having a green screen boot loop, run kisckstart code 58.\nIf that dooes not work, then the drive will need to be reimaged.\n\n");
		exit(1);
	}
	j = i;
	if(block[j+22] != 0) {
		//Something is wrong, /dev/sda16 is not the last partition entry in the MFS header. See if the Tivo can fix this since we cannot. 
		fprintf(stderr,"Error: Unexpected finding. Partition /dev/sda16 is not the last partition entry in the MFS header.\nRecommend placing the drive in the TiVo to see if it can fix this.\nYou may see a screen that indicates you need to divorce an external drive.\nGo ahead and do so.  If you start having a green screen boot loop, run kisckstart code 58.\nIf that dooes not work, then the drive will need to be reimaged.\n\n");
		exit(1);
	}

	fprintf(stdout,"Correcting the MFS header.\n");
	//Now let us correct the entry for /dev/sda15 /dev/sda16 in the MFS header so first lets erase it
	memset(&block[j], 0, 22);

	//Now lets place the correct entries in there.  
	if (coalesce) {
		switch (s){
			case 2: memcpy(block+j, " /dev/sda2", 10); break;
            case 3: memcpy(block+j, " /dev/sda3", 10); break;
			case 4: memcpy(block+j, " /dev/sda4", 10); break;
            case 5: memcpy(block+j, " /dev/sda5", 10); break;
			case 6: memcpy(block+j, " /dev/sda6", 10); break;
			case 7: memcpy(block+j, " /dev/sda7", 10); break;
			default: fprintf(stderr, "Something is very wrong here! Exiting.....\n");exit(1);
		}
	}
	else {
		switch (s){
			case 2: memcpy(block+j, " /dev/sda2 /dev/sda3", 20); break;
            case 3: memcpy(block+j, " /dev/sda3 /dev/sda4", 20); break;
			case 4: memcpy(block+j, " /dev/sda4 /dev/sda5", 20); break;
            case 5: memcpy(block+j, " /dev/sda5 /dev/sda6", 20); break;
			case 6: memcpy(block+j, " /dev/sda6 /dev/sda7", 20); break;
			default: fprintf(stderr, "Something is very wrong here! Exiting.....\n");exit(1);
		}
  	}
	//Lets get ready to recompute the checksum of the MFS header by replacing the current checksum with the magic number
	memcpy(&block[8], deadfood, 4);

	//Now compute the CRC for the block and correct for endianess and replace the magic number with the checksum for the block
	//zlib's crc routine XORs 0xFFFFFFFF at the begninng and the end.  We do not want this.  TiVo wants to start with zero as the starting value so we preload our
	//crc value with all ones so that it XORs to zero.  We also want to reverse the final XOR so we XOR our result with all ones again.
	crc=crcinit;
	for (i=0; i<280; ++i){
		crc = crc32(crc, block+i, 1);
	}
	crc = crc^crcinit;

	//endian swap if we need to
	if(eswap){
		crc = endian_swap32(crc);
	}

	//load the value in the correct place
	memcpy(&block[8],&crc, 4); 

	fprintf(stdout,"MFS header corrected.\n\nWriting corrected header to the MFS.\n");
	//Write the corrected block to the MFS header
	t = lseek(fd, pstart*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	write(fd, block, SZ);

	//Write the corrected block to the backup MFS header as well
	t = lseek(fd, (pstart + psize -1)*SZ, SEEK_SET);
	if(t < 0){perror("lseek"); exit(1);}
	write(fd, block, SZ);

	fprintf(stdout, "Corrected MFS header written to drive.\n\nProcessing of the drive is complete.\n");
	exit(0);
}
