#include <ipmi.h>
#include <ipmi_intf.h>
#include <mtca.h>
#include <string.h>

extern struct ipmi_intf ipmi_lan_intf;

struct ipmi_intf * open_lan_session(unsigned char *hostname, 
									unsigned char *username, 
									unsigned char *password, 
									unsigned char target_addr, 
									unsigned char transit_addr, 
									unsigned char target_ch,
									unsigned char transit_ch
									){
		
	struct ipmi_intf *intf = &ipmi_lan_intf;
	
	if (intf->setup(intf) < 0){
		//printf("Error: Unable to setup interface LAN");
		return NULL;
	}
	
	strncpy(intf->session->hostname, hostname, 64);
	strncpy(intf->session->username, username, 16);
	strncpy(intf->session->authcode, password, IPMI_AUTHCODE_BUFFER_SIZE);
	intf->session->password = 1;
	intf->session->privlvl = IPMI_SESSION_PRIV_ADMIN;
	//intf->session->port = port;
	intf->my_addr = 0x20;
	
	if(target_addr > 0){
		intf->target_addr = target_addr;
		intf->target_channel =  target_ch;
	}
	
	if(transit_addr > 0){
		intf->transit_addr = transit_addr;
		intf->transit_channel = transit_ch;
	}
	
	return intf;
}

struct ipmi_rs * send_ipmi_cmd(struct ipmi_intf *intf, unsigned char netfn, unsigned char cmd, unsigned char *data, unsigned char data_len){
	struct ipmi_rq req;

	if(intf == NULL)
		return NULL;
	
	memset(&req, 0, sizeof(req));	//Init
	req.msg.netfn = netfn;
	req.msg.cmd = cmd;
	req.msg.data = data;
	req.msg.data_len = data_len;
	
	return intf->sendrecv(intf, &req);
}