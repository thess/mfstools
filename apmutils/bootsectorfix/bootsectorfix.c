/* bootsectorfix.c
 * This program is to be used on Series 5 and later TiVos when MFSTools
 * gives you a "Can not determine primary boot partition from boot 
 * sector" error.  It will scan the boot sector of the drive to 
 * determine if there is enough information present to identify the 
 * primary boot partition.  If there is, it will then attempt to update
 * the boot sector of the drive with the information found.
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

#define SZ 512

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
    fprintf(stderr,"Usage: bootsectorfix /dev/sdX\nReplace X with the actual letter of the drive.\n");
    exit(1);
}

void warning(){
    char *w = "This program is designed to work on series 5 and later\n"
            "TiVos to resolve a 'Can not determine primary boot partition\n"
			"from boot sector' error when running MFSTools.  Use on any\n"
			"other series TiVo may result in loss of data.\n\n"
			"This program will scan the boot sector and determine if there is\n"
			"enough information present to identify the primary boot partition.\n"
			"If there is, then the boot sector will be updated so MFSTools can\n"
			"function correctly.\n\n"
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
	int i,j,w;
	int count = 0;
    off_t t;
	char* path = argv[1];
    char* out = argv[2];
	unsigned char block[SZ];
	unsigned char proceed;
	
    warning();

    j = fscanf(stdin, "%c", &proceed);
    if(j < 0){perror("fscanf"); exit(1);}

    printf("\n");

    if (proceed != 'y' && proceed != 'Y') exit(0);

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

	// Test for Series 5 & 6 Tivo
	// First make sure we have the correct signature for a TiVo drive
	if(block[0] != 0x92 || block[1] != 0x14){
		fprintf(stderr, "Error: Not a Series 5 and later TiVo drive. Signature expected to be 9214 but is %x%x\nUnable to process drive.\n\n", block[0], block[1]);
		exit(1);
	}
	fprintf(stdout, "Drive has expected TiVo signature.\nWill begin to process drive.\n\n");	

	// See if we even need to modify the drive.  If the boot sector is clearly identified, then quit.
	if ((block[2] == 0x03 && block[3] == 0x06) || (block[2] == 0x06 && block[3] == 0x03)){
		fprintf(stderr, "Boot partition already specifed.  Primary boot partition is %x.\nNo need to modify boot sector.\nExiting program.\n\n", block[2]);
		exit(0);
	}


    fprintf(stdout, "Scanning boot sector in progress....\n");

	count= 4;
	//count here is inclusive
	while(memcmp((block + count),"sda",3) != 0 && count <= 64){
		count++;
    }

	//If we have come to the end of the scan and have not found the booting root partition, then we cannot do anything.

	if (count >= 64){
		fprintf(stderr, "Unable to determine booting partition.\nExiting program.\n\n");
		exit(1);
	}

	//If we are still here then we must of found something.  Lets proceed.
	if (block[count + 3] == '4'){
		fprintf(stdout, "Determined the booting partition is partition 3.\nCorrection to boot sector made.\n\n");
		block[2] = 0x03;
		block[3] = 0x06;
		block[count - 10] = 'r';
	}
	else {
			if (block[count + 3] == '7') {
				fprintf(stdout, "Determined the booting partition is partition 6.\nCorrection to boot sector made.\n\n");
				block [2] = 0x06;
				block [3] = 0x03;
				block[count - 10] = 'r';
			}
		else {
				fprintf(stderr, "Nonstandard booting root partition %c found.\nUnable to proceed.\nExiting program.\n\n",block[count+3]);
				exit(1);
			}
	}

	t = lseek(fd, 0, SEEK_SET);
    if(t < 0){perror("lseek"); exit(1);}
    w=write(fd, block, SZ);
    if(w < 0){perror("write"); exit(1);}
	
	exit(0);
}
