#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mtca.h>

#include <hpmParser.h>
#include <hpmWriter.h>
#include <hex2bin.h>

#define RED    "\033[22;31m"
#define RESET  "\033[0m"

#define UC32BIT_BOOTLOADER_OFFSET               0x20000 //Bootloader offset for the AT32UC3A uC type
#define UC32BIT_HEADER_TO_REPLACE_CNT   8

static char *getExt (const char *fspec) {
    char *e = strrchr (fspec, '.');
    if (e == NULL)
        e = ""; // fast method, could also use &(fspec[strlen(fspec)]).
    return e;
}

void get_information(unsigned char *iana, unsigned char *product_id, unsigned char *earliest_major, unsigned char *earliest_min, unsigned char *new_major, unsigned char *new_minor, unsigned char *ip, unsigned char *username, unsigned char *password, unsigned char *slots, unsigned int *bootloader_offset, unsigned int *bootloader_header, unsigned int *component) {

    unsigned char iana_ascii[25], prodid_ascii[25], earliest_maj_ascii[25], earliest_min_ascii[25], new_maj_ascii[25], new_min_ascii[25];
    unsigned int iana_int, prodid_int, earliest_maj_int, earliest_min_int, new_maj_int, new_min_int;
    unsigned char slot_inp[128];
    unsigned int uc, comp;

    char *token;

    printf("\n  MMC Information \n");
    printf("*******************\n");

    printf("Component ([0] - Bootloader, [1] IPMC, [2] FPGA): ");
    fflush(stdout);
    scanf("%d",component);

    if (*component == 0 || *component == 1) {
        /** uC version */
        do{
            printf("Microcontroller version ([0] 8 bits / [1] 32 bits): ");
            scanf("%u",&uc);
            if(uc == 0){
                *bootloader_offset = 0;
                *bootloader_header = 0;
            }else if(uc == 1){
                *bootloader_offset = UC32BIT_BOOTLOADER_OFFSET;
                *bootloader_header = UC32BIT_HEADER_TO_REPLACE_CNT;
            }else{
                printf("{Error} Wrong parameter, shall be 0 or 1 \n");
            }
        }while(uc != 0 && uc != 1);
    } else {
        *bootloader_offset = 0;
        *bootloader_header = 0;
    }

    /** IANA */
    printf("IANA Manufacturer ID: ");
    fflush(stdout);
    scanf("%s",iana_ascii);

    if(strstr(iana_ascii,"x")) {
        sscanf(iana_ascii, "%x",&iana_int);
    } else {
        sscanf(iana_ascii, "%d",&iana_int);
    }

    iana[0] = (iana_int & 0x00FF0000) >> 16;
    iana[1] = (iana_int & 0x0000FF00) >> 8;
    iana[2] = (iana_int & 0x000000FF);

    /** Product ID */
    printf("Product ID: ");
    fflush(stdout);
    scanf("%s",prodid_ascii);

    if(strstr(prodid_ascii,"x")){
        sscanf(prodid_ascii, "%x", &prodid_int);
    }else{
        sscanf(prodid_ascii, "%d", &prodid_int);
    }

    product_id[0] = (prodid_int & 0x0000FF00) >> 8;
    product_id[1] = (prodid_int & 0x000000FF);

    /** Earlied Major revision */
    printf("Earliest major firmware rev. compatible : ");
    fflush(stdout);
    scanf("%s",earliest_maj_ascii);

    if(strstr(earliest_maj_ascii,"x")){
        sscanf(earliest_maj_ascii, "%x", &earliest_maj_int);
    }else{
        sscanf(earliest_maj_ascii, "%d", &earliest_maj_int);
    }

    *earliest_major = (earliest_maj_int & 0x000000FF);

    /** Earliest Minor revision */
    printf("Earliest minor firmware rev. compatible : ");
    fflush(stdout);
    scanf("%s",earliest_min_ascii);

    if(strstr(earliest_min_ascii,"x")){
        sscanf(earliest_min_ascii, "%x", &earliest_min_int);
    }else{
        sscanf(earliest_min_ascii, "%d", &earliest_min_int);
    }

    *earliest_min = (earliest_min_int & 0x000000FF);

    /** New major revision */
    printf("New major firmware : ");
    fflush(stdout);
    scanf("%s",new_maj_ascii);

    if(strstr(new_maj_ascii,"x")){
        sscanf(new_maj_ascii, "%x", &new_maj_int);
    }else{
        sscanf(new_maj_ascii, "%d", &new_maj_int);
    }

    *new_major = (new_maj_int & 0x000000FF);

    /** New minor revision */
    printf("New minor firmware : ");
    fflush(stdout);
    scanf("%s",new_min_ascii);

    if(strstr(new_min_ascii,"x")){
        sscanf(new_min_ascii, "%x", &new_min_int);
    }else{
        sscanf(new_min_ascii, "%d", &new_min_int);
    }

    *new_minor = (new_min_int & 0x000000FF);

    printf("\n  Download information  \n");
    printf("************************\n");

    /** MCH IP */
    printf("MCH IP: ");
    fflush(stdout);
    scanf("%s",ip);

    /** Username */
    printf("Username: ");
    fflush(stdout);
    while (getchar()!='\n');    //Flush stdin
    fgets(username,20,stdin);
    username[strnlen(username, 16)-1] = 0x00;

    /** Password */
    printf("Password: ");
    fflush(stdout);
    fgets(password,20,stdin);
    password[strnlen(password, 16)-1] = 0x00;

    /** Slot to be updated */
    printf("Slot(s) (should be separed by comma - all for all slots): ");
    fflush(stdout);
    scanf("%s",slot_inp);

    if(!strcmp(slot_inp, "all")){
        memset(slots, 1, 12);
    }else{
        token = strtok(slot_inp, ",");

        while( token != NULL ){
            slots[atoi(token)-1] = 1;
            token = strtok(NULL, ",");
        }
    }

    printf("\n");
    printf("  Download firmware  \n");
    printf("*********************\n");
}

int main(int argc,char **argv) {

/** User specific information */
    unsigned char iana[3];
    unsigned char product_id[2];
    unsigned char earliest_major;
    unsigned char earliest_min;
    unsigned char new_major;
    unsigned char new_minor;
    unsigned int component;

    unsigned char ip[20];
    unsigned char username[20];
    unsigned char password[20];
    unsigned char slots[12] = {0};

    /** HEX2BIN variables */
    unsigned int firstAddr, lastAddr;
    unsigned char *binary;

    /** HPM image variables */
    unsigned char *hpmImg;
    unsigned int hpmImgSize;

    /** HPM upgrade variable */
    unsigned int update_results[12] = {0};
    unsigned int ucOffset;
    unsigned int cntHeaderToReplace;

    /** General variables */
    unsigned int i;
    FILE *hpm_fd;
    FILE * fileptr;
    unsigned char *filename;
    unsigned int binsize;

    if(argc < 2){
        printf("\tUSAGE: ./hpmdowloader <.hex/.bin file> \n");
        return -1;
    }

    /** Get user information */
    get_information(iana, product_id, &earliest_major, &earliest_min, &new_major, &new_minor, ip, username, password, slots, &ucOffset, &cntHeaderToReplace, &component);

    filename = (argv[1]);

    if (strcmp(getExt(filename),".bin") == 0) {
        printf("Binary File found: %s\n", filename );

        fileptr = fopen(filename, "rb");      // Open the file in binary mode
        fseek(fileptr, 0, SEEK_END);          // Jump to the end of the file
        binsize = ftell(fileptr);             // Get the current byte offset in the file
        rewind(fileptr);                      // Jump back to the beginning of the file

        binary = (unsigned char *)malloc((binsize+1)*sizeof(unsigned char)); // Enough memory for file + \0
        fread(binary, binsize, 1, fileptr); // Read in the entire file
        fclose(fileptr); // Close the file
        firstAddr = 0;
        lastAddr = binsize;
    } else if (strcmp(getExt(filename),".hex") == 0) {
        /** Translation from .hex (intel) to .bin */
        binary = get_binary(filename, &firstAddr, &lastAddr);
    }

    if (binary == NULL) {
        return -1;
    }

    //for(i=0; i<cntHeaderToReplace; i++) binary[ucOffset+i] = binary[i];

    /** Creation of the HPM file here */
    hpmImg = hpm_parse(&(binary[ucOffset]), (lastAddr - (firstAddr + ucOffset)), &hpmImgSize, iana, product_id, earliest_major, earliest_min, new_major, new_minor, component);
    free(binary);
    if(hpmImg == NULL)  return -2;

    /*
      hpm_fd = fopen("img.hpm", "wb");
      fwrite(hpmImg, hpmImgSize, 1, hpm_fd);
      fclose(hpm_fd);
    */

    /** Write the image */
    for(i=0; i<12; i++) {
        if(slots[i]) {
	    update_results[i] = hpmdownload(hpmImg, hpmImgSize, ip, username, password, (i+1), component);
	}
    }

    /** Print results */
    for(i=0; i < 12; i++){
        if(slots[i] && update_results[i]){
            printf(RED "AMC slot %d : Programming failed \n" RESET, i+1);
        }
    }

    return 0;
}
