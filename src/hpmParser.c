/***********************************

File: parser.c

Description: Parser for HPM.1 standard

Author: Julian Mendez <julian.mendez@cern.ch>

************************************/
#include <openssl/md5.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <hpmParser.h>

unsigned char *hpm_parse(unsigned char *binary, int binsize, unsigned int *hpmsize, unsigned char *iana, unsigned char *prodid, unsigned char earliest_major, unsigned char earliest_min, unsigned char new_maj, unsigned char new_min, unsigned int component)
{
    int offset = 0, i, filesize;
    unsigned char *img;
    unsigned char md5arr[MD5_DIGEST_LENGTH];

    img = (unsigned char *)malloc(binsize+128);
    if(img == NULL){
        printf("ERROR: img is NULL \n");
        return NULL;
    }

    offset += header(img, 0, iana, prodid, earliest_major, earliest_min, new_maj, new_min, component);

    offset += upgrade_action(img, offset, binary, binsize, component);

    write_md5(img, offset, md5arr);

    for(i=0; i<MD5_DIGEST_LENGTH; i++){
        img[offset++] = md5arr[i];
    }

    *hpmsize = offset;

    return img;
}

int header(unsigned char val[], unsigned offset, unsigned char *iana, unsigned char *prodid, unsigned char earliest_major, unsigned char earliest_min, unsigned char new_maj, unsigned char new_min, unsigned int component){
    unsigned int timestamp, i;
    char crc = 0;

    unsigned char header[] = {
        0x50,           //Signature (start)
        0x49,
        0x43,
        0x4D,
        0x47,
        0x46,
        0x57,
        0x55,           //Signature (end)
        0x00,
        0x00,           //Device id (0x00 : MMC source code)
        iana[2],                //IANA LSB
        iana[1],                //IANA
        iana[0],                //IANA MSB
        prodid[1],              //Product ID LSB
        prodid[0],              //Product ID MSB
        0x00,           //Timestamp     (LSB)
        0x00,           //Timestamp
        0x00,           //Timestamp
        0x00,           //Timestamp     (MSB)
        0x08,           //Image capabilities (Update only)
        component,           //component
        0x00,           //Self-test timeout: Not implemented
        0x00,           //Rollback timeout: Not implemented
        0x0C,           //Inaccesibility timeout
        earliest_min,           //Earliest compatible Revision (Minor)
        earliest_major,         //Earliest compatible Revision (Major)
        new_min,                //New version (Should be set by user)
        new_maj,                //New version (Should be set by user)
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,           //No OEM data
        0x00,           //No OEM data
        0x00            //Header checksum: Should be calculated
    };

    timestamp = (unsigned int)time(NULL);

    header[15] = (unsigned char)(timestamp & 0x000000FF);
    header[16] = (unsigned char)((timestamp >> 8) & 0x000000FF);
    header[17] = (unsigned char)((timestamp >> 16) & 0x000000FF);
    header[18] = (unsigned char)((timestamp >> 24) & 0x000000FF);

    for(i=0; i<33; i++){
        crc -= header[i];
    }

    header[34] = crc;

    for(i=0; i<sizeof(header); i++){
        val[offset+i] = header[i];
    }

    return sizeof(header);
}

int prepare_action(unsigned char val[], int offset, unsigned int component){

    int i;
    int checksum = 0;
    
    /*
     * Calculates de 2's complement checksum of the header (action code, component)
     */
    checksum = -((0x01 + component)%256);

    unsigned char act[]={
        0x01,   //Upload firmware image
        component,   //Component 0
        checksum,   //Header checksum
    };

    for(i=0; i<sizeof(act); i++){
        val[offset+i] = act[i];
    }

    return (sizeof(act));
}

int upgrade_action(unsigned char val[], int offset, unsigned char *binary, int binsize, unsigned int component){
    int i;
    int checksum = 0;
    
    /*
     * Calculates de 2's complement checksum of the header (action code, component)
     */
    checksum = -((0x02 + component)%256);
    
    unsigned char act[]={
        0x02,   //Upload firmware image
        component,   //Component 0
        checksum,   //Header checksum
        0x01,   //FW version
        0x0A,
        0x00,
        0x00,
        0x00,
        0x00,
        //FW Description string
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x05, //Firmware length (LSB)
        0x00,
        0x00,
        0x00 //Firmware length (MSB)
    };

    for(i=0; Version_desc[i] != 0x00 && i<21; i++) {
        act[9+i] = Version_desc[i];
    }

    act[30] = (unsigned char)((binsize & 0x000000FF));
    act[31] = (unsigned char)((binsize >> 8) & 0x000000FF);
    act[32] = (unsigned char)((binsize >> 16) & 0x000000FF);
    act[33] = (unsigned char)((binsize >> 24) & 0x000000FF);

    //for(i=30; i < 34; i++)
    //printf("(%d)=%02x ",i,act[i]);
    //fflush(stdout);

    //printf("File size : %d (%x)\n",filesize, filesize);

    for(i=0; i<sizeof(act); i++){
        val[offset+i] = act[i];
    }

    for(i=0; i<binsize; i++){
        val[i+sizeof(act)+offset] = binary[i];
    }

    return (sizeof(act)+binsize);
}

void write_md5(char *buf, ssize_t length, unsigned char md5arr[]){
    int n;
    MD5_CTX c;
    unsigned char out[MD5_DIGEST_LENGTH];

    MD5_Init(&c);
    MD5_Update(&c, buf, length);
    MD5_Final(out, &c);

    for(n=0; n<MD5_DIGEST_LENGTH; n++){
        //printf("%02x ", out[n]);
        md5arr[n] = out[n];
    }

    //printf("\n");
}
