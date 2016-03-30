#ifndef HPMPARSER_H
#define HPMPARSER_H

#define Version_desc    "CERN MMC"

/** Public function */
/** HPMPaser: Create the HPM image (returned) - Shall be free before closing the program */
unsigned char *hpm_parse( unsigned char *binary,
                          int binsize,
                          unsigned int *hpmsize,
                          unsigned char *iana,
                          unsigned char *prodid,
                          unsigned char earliest_major,
                          unsigned char earliest_min,
                          unsigned char new_maj,
                          unsigned char new_min,
			  unsigned int component);

/** Private functions */
/** Header calculation */
int header( unsigned char val[],
            unsigned offset,
            unsigned char *iana,
            unsigned char *prodid,
            unsigned char earliest_major,
            unsigned char earliest_min,
            unsigned char new_maj,
            unsigned char new_min,
	    unsigned int component);

/** Upgrade action fields calculation */
int upgrade_action( unsigned char val[],
                    int offset,
                    unsigned char *binary,
                    int binsize,
		    unsigned int component);

/** Prepare upgrade fields calculation */
int prepare_action( unsigned char val[],
                    int offset,
		    unsigned int component);

/** MD5 hash function */
void write_md5( char *buf,
                ssize_t length,
                unsigned char md5arr[]);

#endif
