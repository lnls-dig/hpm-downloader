#ifndef MTCA_H
#define MTCA_H

#include <ipmi.h>
#include <ipmi_intf.h>

struct ipmi_intf * open_lan_session(unsigned char *hostname, 
									unsigned char *username, 
									unsigned char *password, 
									unsigned char target_addr, 
									unsigned char transit_addr, 
									unsigned char target_ch,
									unsigned char transit_ch
									);
struct ipmi_rs * send_ipmi_cmd(struct ipmi_intf *intf, unsigned char netfn, unsigned char cmd, unsigned char *data, unsigned char data_len);

int sel_init(unsigned char *hostname, unsigned char *username, unsigned char *password);
int get_event(unsigned char *buf, unsigned char maxlen, unsigned short *entry_nb);

#endif				/* MTCA_H */
