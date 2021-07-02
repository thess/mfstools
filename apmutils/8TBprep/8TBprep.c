/* 8TBprep.c
 * Converts 32 bit APM partitions for a TiVo
 * to a 64 bit APM partition in an inplace manner.
 * If it is successful, then if the image has been expanded once,
 * it will attempt to coalesce partitions 12 and 13 if appropriate.
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

#define SZ 512
#define TO_CPY 92
#define crcinit 0xFFFFFFFF

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
    fprintf(stderr,"Usage: 8TBprep /dev/sdX\nReplace X with the letter of the drive.\n");
    exit(1);
}

void warning(){
    char *w = "This program is only meant to work on a series 4 TiVo.\n"
            "Use on any other series TiVo will result in loss of data.\n\n"
			"This program will convert the TiVo partition map from a\n"
			"32 bit format to a 64 bit format. Once that is done, it will\n"
			"prune all Apple_Free partitions located at the end of the APM.\n"
			"After that it will check to see if it has been expanded and\n"
			"if so will coalesce partitions 12 and 13 and move partition 15\n"
            "to partition 13 if appropriate.\n\n"
			"This utility is provided without warranty or guarantee that it will\n"
            "perform as intended.  All effort has been made to ensure a\n"
            "successful outcome.  Use at your own risk.\n"
            "Press 'y' if you want to continue. Any other key to exit.\n";

    printf("%s ", w);

}

int main(int argc, char** argv)
{
    if(argc != 2) usage();

	short endian_test = 1;
	char *c = (char*)&endian_test;
	char eswap = 0;
	int fd;
	int i,j,w;
	int offset = 36;
	int count = 0;
    off_t t;
	char* path = argv[1];
    char* out = argv[2];
	unsigned char block[SZ];
	unsigned char new_b[SZ];
	unsigned char block12[SZ];
	unsigned char block13[SZ];
	unsigned char num_blocks;
    unsigned char cmp[11] = " /dev/sda15";
	unsigned char deadfood[4] = {0xDE, 0xAD, 0xF0, 0x0D}; 
	unsigned long crc = crc32(0L, Z_NULL, 0);
	unsigned long long pstart12 = 0;
	unsigned long long psize12 = 0;
	unsigned long long pstart13 = 0;
	unsigned long long psize13 = 0;

    warning();

    j = fscanf(stdin, "%c", &num_blocks);
    if(j < 0){perror("fscanf"); exit(1);}
    printf("\n");

    if(num_blocks != 'y') exit(0);

    //endian test
		
	if (*c) {
	    eswap = 1;
		fprintf(stdout, "Little endian computer detected.\n\n");
	}
	else {
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

	// Test for Series 4 Tivo
	// First make sure we have the correct signature for a TiVo drive
	if(block[0] != 0x14 || block[1] != 0x92){
		fprintf(stderr, "Error: Not a TiVo drive. Signature expected to be 1492 but is %x%x\nUnable to process drive.\n\n", block[0], block[1]);
		exit(1);
	}
	fprintf(stdout, "Drive has expected TiVo signature.\nWill begin to process drive.\n\n");	

	//Next check if Partition 14 is a SQLite partition.  If not then this is a pre Series 4 Tivo
	t = lseek(fd, 14*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	if(read(fd, block, SZ) < 0){
		perror("read");
		exit(1);
	}
	if((!(block[1] == 0x4D) || memcmp(block+16,"SQLite",6)) && (!(block[1] == 0x4E) || memcmp(block+24,"SQLite",6))){
		fprintf(stderr, "Error: Not a Series 4 TiVo drive. Partition 14 is not of type SQLite.\nUnable to process drive.\n");
		exit(1);
	}
	fprintf(stdout, "Drive appears to be from a Series 4 TiVo.\nWill continue to process drive...\n\n");

	//Now that is out of the way, lets get started and read block 1
	t = lseek(fd, SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	if(read(fd, block, SZ) < 0){
		perror("read");
		exit(1);
	}

	num_blocks = block[7];

    fprintf(stdout, "Converting to 64 bit APM in progress....\n");

	count= 2;
	//count here is inclusive
	while(read(fd, block, SZ) > 0 && count <= num_blocks){
		if(block[1] == 'M' ){
			memset(new_b, 0, SZ);

			i=j=0;
			//this introduces the correct offsets for certain 
			//blocks to become 64 bit quants
			for(i = 0; i < TO_CPY; i++){
				if(i == 8)  j+=4;
				if(i == 12) j+=4;
				if(i == 80) j+=4;
				if(i == 84) j+=4;
				if(i == 88) j=156;

				new_b[j] = block[i];
				j++;
			}
			new_b[1] = 'N';

			memcpy(block, new_b, SZ);
		}
			
        t = lseek(fd, count*SZ, SEEK_SET);
        if(t < 0){perror("lseek"); exit(1);}
        w=write(fd, block, SZ);
        if(w < 0){perror("write"); exit(1);}

        if (count == 12) memcpy(block12, block, SZ);
            
        if (count == 13) memcpy(block13, block, SZ);
     
        count++;
    }
    fprintf(stdout, "Conversion to 64 bit APM complete.\n\n");
	
	// Although Apple_Free partitions are innocuous, they can problematic with some 3rd part TiVo tools.
	//Consequently it is easier just to trim them off.  They should be at the end of the partition map.  Any in the middle of the partition map will stay,
	//but then we are dealing with a non-standard TiVo partition map and should probably quit.
	
	fprintf(stdout, "Pruning Apple_Free partitions....\n");
	
	//Go to the last APM entry
	t = lseek(fd, num_blocks*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	if(read(fd, block, SZ) < 0){
		perror("read");
		exit(1);
    }

	//Lets start deleteing the entries and decrement the total number of blocks while we are at it.
	while (num_blocks >= 17 && !memcmp((block + 56),"Apple_Free",10)) {
	    memset(block, 0, 512);
		t = lseek(fd, num_blocks*512, SEEK_SET);
		if(t < 0){perror("lseek"); exit(1);}
		w=write(fd, block, SZ);
		if(w < 0){perror("write"); exit(1);}
		num_blocks--;
		t = lseek(fd, num_blocks*512, SEEK_SET);
		if(t < 0){perror("lseek"); exit(1);}
		if(read(fd, block, SZ) < 0){
            perror("read");
            exit(1);
        }
    }

	//Now lets reset the APM entries to reflect the number of blocks left after trimming off the Apple_Free partitions.  We could just wait until the end, but if there is an error sometime before then, it would be nice to have a valid APM
    count= 1;
	t = lseek(fd, 512, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	
	//count here is inclusive and now reset the value
	while(read(fd, block, SZ) > 0 && count <= num_blocks){
		block[7]=num_blocks;		
        t = lseek(fd, count*512, SEEK_SET);
        if(t < 0){perror("lseek"); exit(1);}
        w=write(fd, block, SZ);
        if(w < 0){perror("write"); exit(1);}
        count++;
	}
	fprintf(stdout, "Pruning Apple_Free partitions complete.\n\n");	
	
    // Now test to see if a coalesce is possible and makes sense
    // There should be no more than 16 partitions after trimming Apple_Free partitions
    // We need to make sure partitions 15 and 16 are of the MFS type
    // The "first partition block" plus the "partition block count" of partition 15 equals the "first partition block" of partition 16
    // If the above parameters are met, then we are good to go

	fprintf(stdout, "Contemplating if moving and coalescing makes sense.\n");
	if (num_blocks != 15) {
        fprintf(stderr, "Partition structure is not as expected.  Too many or few partitions.\nNumber of blocks in partition map is %i and was expecting 15.\nUnable to coalesce.\nProcessing of drive is incomplete.\n\n",num_blocks); 
        exit(1);
    }

	if (memcmp((block12 + 56),"MFS",3) || memcmp((block13 + 56),"MFS",3)) {
        fprintf(stderr, "Partition structure is not as expected. Partition 12 & 13 not of MFS type.\nPartition 12 is of %.32s type and Partition 13 is of %.32s type.\nUnable to coalesce.\nProcessing of drive is incomplete.\n\n",(block12+56),(block13+56));
        exit(1);
    }

	// Only one more test that need to be satisfied and so we need to set it up the variables and switch endianess if necessary
	memcpy(&pstart12,(block12 + 8),8);
	memcpy(&psize12,(block12 + 16),8);
	memcpy(&pstart13,(block13 + 8),8);
	memcpy(&psize13,(block13 + 16),8);

	// If we are working on a little endian machine, lets correct the values so we can work with them
	if (eswap) {
	    pstart12=endian_swap64(pstart12);
	    psize12=endian_swap64(psize12);
	    pstart13=endian_swap64(pstart13);
	    psize13=endian_swap64(psize13);
	}
	
	if ((pstart12 + psize12) != pstart13) {fprintf(stderr,"Partition structure not as expected.\nPhysical locations of partitions 12 & 13 are not contiguous on drive.\nExpected start of partition 13 is at %Lu but actually starts at %Lu.\nUnable to coalesce.\nProcessing of drive is incomplete.\n\n",(pstart12+psize12),pstart13); exit(1);}

	if ((psize12 + psize13) > 0xFFFFFFFF) {fprintf(stderr,"Coalesced size is too large.\nActual size is %lld bytes but should not exceed %ud bytes.\nThe MFS media partition needs to shrink by atleast %lld bytes.\nUnable to coalesce.\nProcessing of drive is incomplete.\n\n", (psize12+psize13),0xFFFFFFFF,(psize12 + psize13 - 0xFFFFFFFF)); exit(1);}

	// passed that last test so now proceed to coalesce. First we calculate the size of the coalesced partition and store it in the correct locations followed by writing the block
	fprintf(stdout, "Moving and coalesing makes sense.\n\nProceeding with coalesing.\n");
	psize12=psize12+psize13;
	
	// If we are working on a little endian machine, correct the value to write it correctly to the file
	if (eswap) psize12=endian_swap64(psize12);
	
	//Now copy the size to the appropriate places in the block
	memcpy((block12+16),&psize12,8);
	memcpy((block12+96),&psize12,8);
	memcpy((block12+24),"MFS application/media region 2\0\0",32); 

	//Write the new APM entry for partition 12
	t = lseek(fd, 12*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
    w = write(fd, block12, SZ);
    if(w < 0){perror("write"); exit(1);}

	//Now lets move the information for partition 15 to partition 13.
	t = lseek(fd, 15*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	if(read(fd, block13, SZ) < 0){
            perror("read");
            exit(1);
    }
	t = lseek(fd, 13*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	w=write(fd, block13, SZ);
    if(w < 0){perror("write"); exit(1);}

    // Now we need to erase the APM entry for partition 15
	memset(block, 0, SZ);
	t = lseek(fd, 15*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	w=write(fd, block, SZ);
    if(w < 0){perror("write"); exit(1);}

	fprintf(stdout,"Moving and coalescing complete.\n\nNow resetting the APM\n");
	// now we have to reset the "block in partition map" value in all APM entries to one less.  Luckily the value is in the same place in both 32 bit and 64 bit APM entries
	num_blocks--;
	
	//go back to the beginning of the APM
	count= 1;
	t = lseek(fd, SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	
	//count here is inclusive and now reset the value
	while(read(fd, block, SZ) > 0 && count <= num_blocks){
		block[7]=num_blocks;		
        t = lseek(fd, count*SZ, SEEK_SET);
        if(t < 0){perror("lseek"); exit(1);}
        w=write(fd, block, SZ);
        if(w < 0){perror("write"); exit(1);}
        count++;
    }
	fprintf(stdout,"APM reset.\n\n");

	//Lets now correct the MFS header so we do not have to force a divorce of the now non-existant partition.
	t = lseek(fd, 10*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	t = read(fd, block, SZ);
	if(t < 0){perror("read"); exit(1);}

	//Read the starting block for partition 10 and the size of partition 10 so we know where to go.  The header is the first block of partition 10 and the backup header is the last block of partition 10.  Will use pstart13 and psize13 since we have them readily available and don't need them for anything else.
	memcpy(&pstart13,(block + 8),8);
	memcpy(&psize13,(block + 16),8);

	// If we are working on a little endian machine, lets correct the values so we can work with them
	if (eswap) {
	    pstart13=endian_swap64(pstart13);
	    psize13=endian_swap64(psize13);
	}

	fprintf(stdout,"Evaluating MFS header to see if it can be approptiately modified to complete the moving and coalescing process.\n");
	//Now lets go to the MFS header and read it in.
	t = lseek(fd, pstart13*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	t=read(fd, block, SZ);
	if(t < 0){perror("read"); exit(1);}

	//Now let fix the header.  Here we have to delete reference to /dev/sda15 so lets look for it	
	for (i = offset; i < 132 + offset; i++){
		j = memcmp(&block[i], &cmp[0], 11);
		if(j == 0)break;// Found it!
	} 	
	if(j != 0){
		//Something is wrong, could not find reference to /dev/sda15 in the MFS header.  See if the Tivo can fix it since we cannot.
		fprintf(stderr,"Error: Unexpected finding.  Could not find reference to /dev/sda15 in MFS header.\nRecommend placing drive in the TiVo to see if it can fix this.\nYou may see a screen that indicates you need to divorce an external drive.\nGo ahead and do so.\n");
		exit(1);
	}
	j = i;
	if(block[j+11] != 0) {
		//Something is wrong, /dev/sda15 is not the last partition entry in the MFS header. See if the Tivo can fix this since we cannot. 
		fprintf(stderr,"Error: Unexpected finding. Partition /dev/sda15 is not the last partition entry in the MFS header.\nRecommend placing the drive in the TiVo to see if it can fix this.\nYou may see a screen that indicates you need to divorce an external drive.\nGo ahead and do so.\n");
		exit(1);
	}

	fprintf(stdout,"Correcting the MFS header.\n");
	//Now let us erase the entry for /dev/sda15 in the MFS header
	memset(&block[j], 0, 11);

	//Lets get ready to recompute the checksum of the MFS header by replacing the current checksum with the magic number
	memcpy(&block[8], deadfood, 4);

	//Now compute the CRC for the block and correct for endianess and replace the magic number with the checksum for the block
	//zlib's crc routine XORs 0xFFFFFFFF at the begninng and the end.  We do not want this.  TiVo wants to start with zero as the starting value so we preload our
	//crc value with all ones so that it XORs to zero.  We also want to reverse the final XOR so we XOR our result with all ones again.
	crc = crcinit;
    for (i=0; i<280; ++i){
		crc = crc32(crc, block+i, 1);
	}
	crc = crc^crcinit;

	if(eswap){
		crc = endian_swap32(crc);
	}
	memcpy(&block[8],&crc, 4); 

	fprintf(stdout,"MFS header corrected.\n\nWriting corrected header to the MFS.\n");
	//Write the corrected block to the MFS header
	t = lseek(fd, pstart13*SZ, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
	w = write(fd, block, SZ);
	if(w < 0){perror("read"); exit(1);}

	//Write the corrected block to the backup MFS header as well
	t = lseek(fd, (pstart13+psize13 -1)*SZ, SEEK_SET);
	if(t < 0){perror("lseek"); exit(1);}
	w = write(fd, block, SZ);
	if(w < 0){perror("read"); exit(1);}

	fprintf(stdout, "Corrected MFS header written to drive.\n\nMoving of partition 15 to 13 and coalesing of partitions 12 and 13 to 12 is now complete.\n\nProcessing of the drive is complete.\n");

	exit(0);
}
