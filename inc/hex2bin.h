#ifndef HEX2BIN_H
#define HEX2BIN_H

#define MAX_LINE_LEN	128

unsigned int compute_binary_size(FILE *fp, unsigned int *firstAddr, unsigned int *lastAddr);
unsigned char *get_binary(const char *filename, unsigned int *firstAddr, unsigned int *lastAddr);

#endif