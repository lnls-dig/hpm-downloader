#ifndef HPM_H
#define HPM_H

#define MAX_ACTION              10
#define MAX_COMPONENTS  8
#define DATA_PER_BLOCK  20

#include <stdbool.h>

typedef struct action_s{
    unsigned char action;
    unsigned char components;
    unsigned char firmware_version[6];
    unsigned char firmware_description[21];
    unsigned int firmware_length;
    unsigned char data_offset;
}action_t;

typedef struct img_info_s{
    unsigned char device_id;
    unsigned char manufacturer_id[3];
    unsigned char product_id[2];
    unsigned char image_capabilities;
    unsigned char components;
    unsigned char self_test_timeout;
    unsigned char rollback_timeout;
    unsigned char inaccessibility_timeout;
    unsigned char earliest_compatibility_vers[2];
    unsigned char firware_rev[6];
    bool check_component;

    unsigned short oem_data_len;

    unsigned char upgrade_timeout;      //5 second per unit

    unsigned char nb_actions;
    action_t actions[MAX_ACTION];
}img_info_t;

unsigned char get_img_information(unsigned char *byte, unsigned int  binsize, bool check_component);
unsigned char check_hpm_info(unsigned char *ip, unsigned char *username, unsigned char *password, unsigned char amc_slot_number);
unsigned char hpm_upgrade(unsigned char *ip, unsigned char *username, unsigned char *password, unsigned char amc_slot_number, action_t *action, unsigned char *byte, unsigned int component);
unsigned char get_action(unsigned char *byte, unsigned int binsize);
int hpmdownload(unsigned char *byte, unsigned int filesize, unsigned char *ip, unsigned char *username, unsigned char *password, unsigned char slot, unsigned int comp, bool check_component);
unsigned char scan_upgrade_status(ipmi_intf * intf, unsigned long max_timeout);
//function in main.c
void set_percent(float percent);

#endif
