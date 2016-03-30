#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hex2bin.h>

/** Refer to https://fr.wikipedia.org/wiki/HEX_%28Intel%29 */

unsigned int compute_binary_size(FILE *fp, unsigned int *firstAddr, unsigned int *lastAddr){
	
	unsigned char line[MAX_LINE_LEN];
	
	unsigned int addr = 0x00000000;	
	unsigned int nbBytes;
	unsigned int localAddr;
	unsigned int type;
	unsigned int byte;
	
	unsigned char *data;
	
	*firstAddr = (unsigned int)-1;
	*lastAddr = 0;
	
	while ( fgets ( line, MAX_LINE_LEN, fp ) != NULL ){
        
		sscanf(line,":%2x%4x%2x",&nbBytes, &localAddr, &type);
		data = &line[9];
		
		switch(type){
			case 0x00:	addr &= 0xFFFF0000;
						addr |= (localAddr & 0x0000FFFF);
						
						if(addr < *firstAddr)	*firstAddr = addr;
						if((addr+nbBytes) > *lastAddr)	*lastAddr = (addr+nbBytes);
						
						break;
						
			case 0x04:	sscanf(data,"%4x",&byte);
						addr = 0x00000000 | (byte << 16);
						break;
		}
	}
	
	return (*lastAddr - *firstAddr);
}

unsigned char *get_binary(const char *filename, unsigned int *firstAddr, unsigned int *lastAddr){
	
	FILE *fp;
	
	unsigned char *binary;
	unsigned char *data;
	unsigned char line[MAX_LINE_LEN];
	
    size_t len = 0;
	
	unsigned int addr = 0x00000000;	
	unsigned int nbBytes;
	unsigned int localAddr;
	unsigned int type;
	unsigned int byte;
	unsigned int i;
		
	fp = fopen(filename, "r");

    if (fp == NULL)
        return NULL;
	
	binary = malloc(compute_binary_size(fp, firstAddr, lastAddr));
	if(binary == NULL)	return NULL;	
	rewind(fp);
	
	for(i=0; i<(*lastAddr - *firstAddr); i++)	binary[i] = 0xff;
	
	while ( fgets ( line, MAX_LINE_LEN, fp ) != NULL ){
		
		sscanf(line,":%2x%4x%2x",&nbBytes, &localAddr, &type);
		data = &line[9];
		
		switch(type){
			case 0x00:	addr &= 0xFFFF0000;
						addr |= (localAddr & 0x0000FFFF);
						
						for(i=0; i<nbBytes; i++){
							sscanf(data,"%2x",&byte);
							binary[(addr - *firstAddr)+i] = (unsigned char)byte;
							data += 2;
						}
						break;
						
			case 0x04:	sscanf(data,"%4x",&byte);
						addr = 0x00000000 | (byte << 16);
						break;
			
			default:	break;
		}
		
	}
	
    fclose(fp);
	
    return binary;
}