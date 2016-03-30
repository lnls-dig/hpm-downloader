/*
 * Copyright (c) 2003 Sun Microsystems, Inc.  All Rights Reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistribution of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * Redistribution in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of Sun Microsystems, Inc. or the names of
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * 
 * This software is provided "AS IS," without a warranty of any kind.
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND WARRANTIES,
 * INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE OR NON-INFRINGEMENT, ARE HEREBY EXCLUDED.
 * SUN MICROSYSTEMS, INC. ("SUN") AND ITS LICENSORS SHALL NOT BE LIABLE
 * FOR ANY DAMAGES SUFFERED BY LICENSEE AS A RESULT OF USING, MODIFYING
 * OR DISTRIBUTING THIS SOFTWARE OR ITS DERIVATIVES.  IN NO EVENT WILL
 * SUN OR ITS LICENSORS BE LIABLE FOR ANY LOST REVENUE, PROFIT OR DATA,
 * OR FOR DIRECT, INDIRECT, SPECIAL, CONSEQUENTIAL, INCIDENTAL OR
 * PUNITIVE DAMAGES, HOWEVER CAUSED AND REGARDLESS OF THE THEORY OF
 * LIABILITY, ARISING OUT OF THE USE OF OR INABILITY TO USE THIS SOFTWARE,
 * EVEN IF SUN HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef IPMI_INTF_H
#define IPMI_INTF_H

#include <ipmi.h>
#include <ipmi_constants.h>

#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

/*
 * An enumeration that describes every possible session state for
 * an IPMIv2 / RMCP+ session.
 */
enum LANPLUS_SESSION_STATE {
	LANPLUS_STATE_PRESESSION = 0,
	LANPLUS_STATE_OPEN_SESSION_SENT,
	LANPLUS_STATE_OPEN_SESSION_RECEIEVED,
	LANPLUS_STATE_RAKP_1_SENT,
	LANPLUS_STATE_RAKP_2_RECEIVED,
	LANPLUS_STATE_RAKP_3_SENT,
	LANPLUS_STATE_ACTIVE,
	LANPLUS_STATE_CLOSE_SENT,
};


#define IPMI_AUTHCODE_BUFFER_SIZE 20
#define IPMI_SIK_BUFFER_SIZE      20
#define IPMI_KG_BUFFER_SIZE       21 /* key plus null byte */

#define IPMI_AUTHSTATUS_PER_MSG_DISABLED	0x10
#define IPMI_AUTHSTATUS_PER_USER_DISABLED	0x08
#define IPMI_AUTHSTATUS_NONNULL_USERS_ENABLED	0x04
#define IPMI_AUTHSTATUS_NULL_USERS_ENABLED	0x02
#define IPMI_AUTHSTATUS_ANONYMOUS_USERS_ENABLED	0x01

struct ipmi_session {
	uint8_t hostname[64];								//Used: hostname
	uint8_t username[17];								//Used
	uint8_t authcode[IPMI_AUTHCODE_BUFFER_SIZE + 1];	//Used
	uint8_t challenge[16];								//Used
	uint8_t authtype;									//Used
	uint8_t authtype_set;								//Used
	uint8_t authstatus;									//Used
	uint8_t privlvl;									//Used: privilege level
	int password;										//Used
	int port;											//Used: port
	int active;											//Used
	int retry;											//Used: number of retry

	uint32_t session_id;								//Used
	uint32_t in_seq;									//Used
	uint32_t timeout;									//Used: ipmi lan timeout

	struct sockaddr_in addr;							//Used: connection information

	/*
	 * This data is specific to the Serial Over Lan session
	 */
	struct {
		uint8_t sequence_number;								//Used
		void (*sol_input_handler)(struct ipmi_rs * rsp);		//Used
	} sol_data;
};

struct ipmi_cmd {
	int (*func)(struct ipmi_intf * intf, int argc, char ** argv);
	const char * name;
	const char * desc;
};

struct ipmi_intf_support {
	const char * name;
	int supported;
};

typedef struct ipmi_intf {
	char name[16];
	char desc[128];
	int fd;									//Used: file descriptor (socket)
	int opened;								//Used: Check if interface is opened
	int abort;								//Used: for close session

	struct ipmi_session * session;
	//struct ipmi_oem_handle * oem;
	uint32_t my_addr;						//Used
	uint32_t target_addr;					//Used
	uint8_t target_channel;					//Used
	uint32_t transit_addr;					//Used
	uint8_t transit_channel;				//Used

	int (*setup)(struct ipmi_intf * intf);
	int (*open)(struct ipmi_intf * intf);
	void (*close)(struct ipmi_intf * intf);
	struct ipmi_rs *(*sendrecv)(struct ipmi_intf * intf, struct ipmi_rq * req);
	struct ipmi_rs *(*recv_sol)(struct ipmi_intf * intf);
	int (*keepalive)(struct ipmi_intf * intf);
} ipmi_intf;

#endif /* IPMI_INTF_H */
