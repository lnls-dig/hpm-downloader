#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
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

void print_usage (void) {
    fprintf (stderr, "HPMDownloader\n");
    fprintf (stderr, "Formats a binary/hex file into the HPM format and sends using IPMI to the target MCH\n");
    fprintf (stderr,
             "  -h  --help                       Display this usage information.\n"
             "  -c  --component                  Select the target component:\n"
             "                                       [0]-Bootloader [1]-IPMC [2]-Payload\n"
             "  -o  --offset                     Offset address\n"
             "  -d  --header                     Bytes to change in header\n"
             "  -n  --iana                       IANA Manufacturer Code (defaults to 0x315A)\n"
             "  -i  --id                         Product ID\n"
             "  --early_major                    Earliest compatible major version (defaults to 0)\n"
             "  --early_minor                    Earliest compatible minor version (defaults to 0)\n"
             "  -j  --new_major                  New major version (defaults to 1)\n"
             "  -m  --new_minor                  New minor version (defaults to 0)\n"
             "  -p  --ip                         MCH IP Address\n"
             "  -u  --username                   MCH Username (defaults to \"\")\n"
             "  -w  --password                   MCH Password (defaults to \"\")\n"
             "  -s  --slot                       Slots to be updated (separated by comma):\n"
             "                                       [1 - 12], [all]\n"
             "  file                             Filename (including relative or absolute path)\n"
        );
    exit(EXIT_FAILURE);
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

    unsigned char iana_ascii[25], prodid_ascii[25], earliest_maj_ascii[25], earliest_min_ascii[25], new_maj_ascii[25], new_min_ascii[25];
    unsigned int iana_int, prodid_int, earliest_maj_int, earliest_min_int, new_maj_int, new_min_int;
    unsigned char slot_inp[128];
    unsigned int uc, comp;

    char *token, ch, *endptr;
    int c;

    enum {
        early_major,
        early_minor
    };

    /* Default values */
    iana[0] = 0x00;
    iana[1] = 0x31;
    iana[2] = 0x5A;

    strcpy(username, "");
    strcpy(password, "");

    earliest_major = 0;
    earliest_min = 0;
    new_major = 1;
    new_minor = 0;

    static struct option long_options[] =
        {
            {"help",                no_argument,         NULL, 'h'},
            {"component",           required_argument,   NULL, 'c'},
            {"offset",              required_argument,   NULL, 'o'},
            {"header",              required_argument,   NULL, 'd'},
            {"iana",                optional_argument,   NULL, 'n'},
            {"id",                  required_argument,   NULL, 'i'},
            {"early_major",         optional_argument,   NULL, early_major},
            {"early_minor",         optional_argument,   NULL, early_minor},
            {"new_major",           optional_argument,   NULL, 'j'},
            {"new_minor",           optional_argument,   NULL, 'm'},
            {"ip",                  required_argument,   NULL, 'p'},
            {"username",            optional_argument,   NULL, 'u'},
            {"password",            optional_argument,   NULL, 'w'},
            {"slot",                required_argument,   NULL, 's'},
            {0,0,0,0}
        };

    const char* shortopt = "hc:o::d::n::i:j::m::s:p:u::w::";

    while ((ch = getopt_long_only(argc, argv, shortopt , long_options, NULL)) != -1) {
        switch (ch) {
        case 'h':
            print_usage();
            break;

        case 'c':
            component = strtol(optarg, &endptr, 0);
            break;

        case 'o':
            if(strstr(optarg,"x")){
                sscanf(optarg, "%x", &ucOffset);
            } else {
                sscanf(optarg, "%d", &ucOffset);
            }
            break;

        case 'd':
            if(strstr(optarg,"x")){
                sscanf(optarg, "%x", &cntHeaderToReplace);
            } else {
                sscanf(optarg, "%d", &cntHeaderToReplace);
            }
            break;

        case 'n':
            if(strstr(optarg,"x")) {
                sscanf(optarg, "%x",&iana_int);
            } else {
                sscanf(optarg, "%d",&iana_int);
            }

            iana[0] = (iana_int & 0x00FF0000) >> 16;
            iana[1] = (iana_int & 0x0000FF00) >> 8;
            iana[2] = (iana_int & 0x000000FF);
            break;

        case 'i':
            if(strstr(optarg,"x")) {
                sscanf(optarg, "%x",&prodid_int);
            } else {
                sscanf(optarg, "%d",&prodid_int);
            }

            product_id[0] = (prodid_int & 0x0000FF00) >> 8;
            product_id[1] = (prodid_int & 0x000000FF);
            break;

        case early_major:
            if(strstr(optarg,"x")){
                sscanf(optarg, "%x", &earliest_maj_int);
            } else {
                sscanf(optarg, "%d", &earliest_maj_int);
            }

            earliest_major = (earliest_maj_int & 0x000000FF);
            break;

        case early_minor:
            if(strstr(optarg,"x")){
                sscanf(optarg, "%x", &earliest_min_int);
            } else {
                sscanf(optarg, "%d", &earliest_min_int);
            }
            earliest_min = (earliest_min_int & 0x000000FF);
            break;

        case 'j':
            if(strstr(optarg,"x")){
                sscanf(optarg, "%x", &new_maj_int);
            } else {
                sscanf(optarg, "%d", &new_maj_int);
            }
            new_major = (new_maj_int & 0x000000FF);
            break;

        case 'm':
            if(strstr(optarg,"x")){
                sscanf(optarg, "%x", &new_min_int);
            } else {
                sscanf(optarg, "%d", &new_min_int);
            }
            new_minor = (new_min_int & 0x000000FF);
            break;

        case 'p':
            strcpy(ip, optarg);
            break;

        case 'u':
            strcpy(username, optarg);
            break;

        case 'w':
            strcpy(password, optarg);
            break;

        case 's':
            if(!strcmp(optarg, "all")){
                memset(slots, 1, 12);
            } else {
                token = strtok(optarg, ",");
                while( token != NULL ) {
                    slots[atoi(token)-1] = 1;
                    token = strtok(NULL, ",");
                }
            }
            break;

        default:
            fprintf(stderr, "Bad option\n");
            break;
        }
    }

    if (optind == argc) {
        printf("No firmware found!\n");
        return -1;
    }

    filename = (argv[optind]);

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

#ifdef HPM_EXPORT
    /* Export HPM image to file */
    hpm_fd = fopen("img.hpm", "wb");
    fwrite(hpmImg, hpmImgSize, 1, hpm_fd);
    fclose(hpm_fd);
#endif

    /** Download the image */
    for(i=0; i<12; i++) {
        if( slots[i] ) {
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
