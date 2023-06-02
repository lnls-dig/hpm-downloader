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

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>

#include <ipmi.h>
#include <ipmi_intf.h>

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include "lan.h"
#include "rmcp.h"
#include "asf.h"
#include "auth.h"

#define IPMI_LAN_TIMEOUT	2
#define IPMI_LAN_RETRY		4
#define IPMI_LAN_PORT		0x26f
#define IPMI_LAN_CHANNEL_E	0x0e

struct ipmi_rq_entry * ipmi_req_entries;
static struct ipmi_rq_entry * ipmi_req_entries_tail;
static uint8_t bridge_possible = 0;

static int ipmi_lan_send_packet(struct ipmi_intf * intf, uint8_t * data, int data_len);
static struct ipmi_rs * ipmi_lan_recv_packet(struct ipmi_intf * intf);
static struct ipmi_rs * ipmi_lan_poll_recv(struct ipmi_intf * intf);
static int ipmi_lan_setup(struct ipmi_intf * intf);
static int ipmi_lan_keepalive(struct ipmi_intf * intf);
static struct ipmi_rs * ipmi_lan_send_cmd(struct ipmi_intf * intf, struct ipmi_rq * req);
static int ipmi_lan_open(struct ipmi_intf * intf);
static void ipmi_lan_close(struct ipmi_intf * intf);
static int ipmi_lan_ping(struct ipmi_intf * intf);

struct ipmi_intf ipmi_lan_intf = {
	name:		"lan",
	desc:		"IPMI v1.5 LAN Interface",
	setup:		ipmi_lan_setup,
	open:		ipmi_lan_open,
	close:		ipmi_lan_close,
	sendrecv:	ipmi_lan_send_cmd,
	keepalive:	ipmi_lan_keepalive,
	target_addr:	IPMI_BMC_SLAVE_ADDR,
};

/* ipmi_csum  -  calculate an ipmi checksum
 *
 * @d:		buffer to check
 * @s:		position in buffer to start checksum from
 */
uint8_t ipmi_csum(uint8_t * d, int s){
	uint8_t c = 0;
	for (; s > 0; s--, d++)
		c += *d;
	return -c;
}

static struct ipmi_rq_entry * ipmi_req_add_entry(struct ipmi_intf * intf, struct ipmi_rq * req, uint8_t req_seq)
{
	struct ipmi_rq_entry * e;

	e = malloc(sizeof(struct ipmi_rq_entry));
	if (e == NULL) {
		return NULL;
	}

	memset(e, 0, sizeof(struct ipmi_rq_entry));
	memcpy(&e->req, req, sizeof(struct ipmi_rq));

	e->intf = intf;
	e->rq_seq = req_seq;

	if (ipmi_req_entries == NULL)
		ipmi_req_entries = e;
	else
		ipmi_req_entries_tail->next = e;

	ipmi_req_entries_tail = e;
	return e;
}

static struct ipmi_rq_entry * ipmi_req_lookup_entry(uint8_t seq, uint8_t cmd){
	struct ipmi_rq_entry * e = ipmi_req_entries;
	while (e && (e->rq_seq != seq || e->req.msg.cmd != cmd)) {
		if (e->next == NULL || e == e->next)
			return NULL;
		e = e->next;
	}
	return e;
}

static void ipmi_req_remove_entry(uint8_t seq, uint8_t cmd){
	struct ipmi_rq_entry * p, * e, * saved_next_entry;

	e = p = ipmi_req_entries;

	while (e && (e->rq_seq != seq || e->req.msg.cmd != cmd)) {
		p = e;
		e = e->next;
	}
	if (e) {
		saved_next_entry = e->next;
		p->next = (p->next == e->next) ? NULL : e->next;
		/* If entry being removed is first in list, fix up list head */
		if (ipmi_req_entries == e) {
			if (ipmi_req_entries != p)
				ipmi_req_entries = p;
			else
				ipmi_req_entries = saved_next_entry;
		}
		/* If entry being removed is last in list, fix up list tail */
		if (ipmi_req_entries_tail == e) {
			if (ipmi_req_entries_tail != p)
			 	ipmi_req_entries_tail = p;
			else
				ipmi_req_entries_tail = NULL;
		}
		if (e->msg_data)
			free(e->msg_data);
		free(e);
	}
}

static void ipmi_req_clear_entries(void)
{
	struct ipmi_rq_entry * p, * e;

	e = ipmi_req_entries;
	while (e) {
		if (e->next != NULL) {
			p = e->next;
			free(e);
			e = p;
		} else {
			free(e);
			break;
		}
	}
	ipmi_req_entries = NULL;
}

static int get_random(void *data, int len)
{
	int fd = open("/dev/urandom", O_RDONLY);
	int rv;

	if (fd < 0 || len < 0)
		return errno;

	rv = read(fd, data, len);

	close(fd);
	return rv;
}

static int ipmi_lan_send_packet(struct ipmi_intf * intf, uint8_t * data, int data_len){
	return send(intf->fd, data, data_len, 0);
}

static struct ipmi_rs * ipmi_lan_recv_packet(struct ipmi_intf * intf)
{
	static struct ipmi_rs rsp;
	fd_set read_set, err_set;
	struct timeval tmout;
	int ret;

	FD_ZERO(&read_set);
	FD_SET(intf->fd, &read_set);

	FD_ZERO(&err_set);
	FD_SET(intf->fd, &err_set);

	tmout.tv_sec = intf->session->timeout;
	tmout.tv_usec = 0;

	ret = select(intf->fd + 1, &read_set, NULL, &err_set, &tmout);
	if (ret < 0 || FD_ISSET(intf->fd, &err_set) || !FD_ISSET(intf->fd, &read_set))
		return NULL;

	/* the first read may return ECONNREFUSED because the rmcp ping
	 * packet--sent to UDP port 623--will be processed by both the
	 * BMC and the OS.
	 *
	 * The problem with this is that the ECONNREFUSED takes
	 * priority over any other received datagram; that means that
	 * the Connection Refused shows up _before_ the response packet,
	 * regardless of the order they were sent out.  (unless the
	 * response is read before the connection refused is returned)
	 */
	ret = recv(intf->fd, &rsp.data, IPMI_BUF_SIZE, 0);

	if (ret < 0) {
		FD_ZERO(&read_set);
		FD_SET(intf->fd, &read_set);

		FD_ZERO(&err_set);
		FD_SET(intf->fd, &err_set);

		tmout.tv_sec = intf->session->timeout;
		tmout.tv_usec = 0;

		ret = select(intf->fd + 1, &read_set, NULL, &err_set, &tmout);
		if (ret < 0 || FD_ISSET(intf->fd, &err_set) || !FD_ISSET(intf->fd, &read_set))
			return NULL;

		ret = recv(intf->fd, &rsp.data, IPMI_BUF_SIZE, 0);
		if (ret < 0)
			return NULL;
	}

	if (ret == 0)
		return NULL;

	rsp.data[ret] = '\0';
	rsp.data_len = ret;

	return &rsp;
}

/*
 * parse response RMCP "pong" packet
 *
 * return -1 if ping response not received
 * returns 0 if IPMI is NOT supported
 * returns 1 if IPMI is supported
 *
 * udp.source	= 0x026f	// RMCP_UDP_PORT
 * udp.dest	= ?		// udp.source from rmcp-ping
 * udp.len	= ?
 * udp.check	= ?
 * rmcp.ver	= 0x06		// RMCP Version 1.0
 * rmcp.__res	= 0x00		// RESERVED
 * rmcp.seq	= 0xff		// no RMCP ACK
 * rmcp.class	= 0x06		// RMCP_CLASS_ASF
 * asf.iana	= 0x000011be	// ASF_RMCP_IANA
 * asf.type	= 0x40		// ASF_TYPE_PONG
 * asf.tag	= ?		// asf.tag from rmcp-ping
 * asf.__res	= 0x00		// RESERVED
 * asf.len	= 0x10		// 16 bytes
 * asf.data[3:0]= 0x000011be	// IANA# = RMCP_ASF_IANA if no OEM
 * asf.data[7:4]= 0x00000000	// OEM-defined (not for IPMI)
 * asf.data[8]	= 0x81		// supported entities
 * 				// [7]=IPMI [6:4]=RES [3:0]=ASF_1.0
 * asf.data[9]	= 0x00		// supported interactions (reserved)
 * asf.data[f:a]= 0x000000000000
 */
static int ipmi_handle_pong(struct ipmi_intf * intf, struct ipmi_rs * rsp)
{
	struct rmcp_pong * pong;

	if (rsp == NULL)
		return -1;

	pong = (struct rmcp_pong *)rsp->data;

	return (pong->sup_entities & 0x80) ? 1 : 0;
}

/* build and send RMCP presence ping packet
 *
 * RMCP ping
 *
 * udp.source	= ?
 * udp.dest	= 0x026f	// RMCP_UDP_PORT
 * udp.len	= ?
 * udp.check	= ?
 * rmcp.ver	= 0x06		// RMCP Version 1.0
 * rmcp.__res	= 0x00		// RESERVED
 * rmcp.seq	= 0xff		// no RMCP ACK
 * rmcp.class	= 0x06		// RMCP_CLASS_ASF
 * asf.iana	= 0x000011be	// ASF_RMCP_IANA
 * asf.type	= 0x80		// ASF_TYPE_PING
 * asf.tag	= ?		// ASF sequence number
 * asf.__res	= 0x00		// RESERVED
 * asf.len	= 0x00
 *
 */
static int ipmi_lan_ping(struct ipmi_intf * intf)
{
	struct asf_hdr asf_ping = {
		.iana	= htonl(ASF_RMCP_IANA),
		.type	= ASF_TYPE_PING,
	};
	struct rmcp_hdr rmcp_ping = {
		.ver	= RMCP_VERSION_1,
		.class	= RMCP_CLASS_ASF,
		.seq	= 0xff,
	};
	uint8_t * data;
	int len = sizeof(rmcp_ping) + sizeof(asf_ping);
	int rv;

	data = malloc(len);
	if (data == NULL) {
		return -1;
	}
	memset(data, 0, len);
	memcpy(data, &rmcp_ping, sizeof(rmcp_ping));
	memcpy(data+sizeof(rmcp_ping), &asf_ping, sizeof(asf_ping));

	rv = ipmi_lan_send_packet(intf, data, len);

	free(data);

	if (rv < 0) {
		return -1;
	}

	if (ipmi_lan_poll_recv(intf) == 0)
		return 0;

	return 1;
}

/*
 * The "thump" functions are used to send an extra packet following each
 * request message.  This may kick-start some BMCs that get confused with
 * bad passwords or operate poorly under heavy network load.
 */
static void ipmi_lan_thump_first(struct ipmi_intf * intf)
{
	/* is this random data? */
	uint8_t data[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x07, 0x20, 0x18, 0xc8, 0xc2, 0x01, 0x01, 0x3c };
	ipmi_lan_send_packet(intf, data, 16);
}

static void ipmi_lan_thump(struct ipmi_intf * intf)
{
	uint8_t data[10] = "thump";
	ipmi_lan_send_packet(intf, data, 10);
}

static struct ipmi_rs * ipmi_lan_poll_recv(struct ipmi_intf * intf)
{
	struct rmcp_hdr rmcp_rsp;
	struct ipmi_rs * rsp;
	struct ipmi_rq_entry * entry;
	int x=0, rv;
	uint8_t our_address = intf->my_addr;

	if (our_address == 0)
		our_address = IPMI_BMC_SLAVE_ADDR;

	rsp = ipmi_lan_recv_packet(intf);

	while (rsp != NULL) {

		/* parse response headers */
		memcpy(&rmcp_rsp, rsp->data, 4);

		switch (rmcp_rsp.class) {
		case RMCP_CLASS_ASF:
			/* ping response packet */
			rv = ipmi_handle_pong(intf, rsp);
			return (rv <= 0) ? NULL : rsp;
		case RMCP_CLASS_IPMI:
			/* handled by rest of function */
			break;
		default:
			rsp = ipmi_lan_recv_packet(intf);
			continue;
		}

		x = 4;
		rsp->session.authtype = rsp->data[x++];
		memcpy(&rsp->session.seq, rsp->data+x, 4);
		x += 4;
		memcpy(&rsp->session.id, rsp->data+x, 4);
		x += 4;

		if (rsp->session.id == (intf->session->session_id + 0x10000000)) {
			/* With SOL, authtype is always NONE, so we have no authcode */
			rsp->session.payloadtype = IPMI_PAYLOAD_TYPE_SOL;
	
			rsp->session.msglen = rsp->data[x++];
			
			rsp->payload.sol_packet.packet_sequence_number =
				rsp->data[x++] & 0x0F;

			rsp->payload.sol_packet.acked_packet_number =
				rsp->data[x++] & 0x0F;

			rsp->payload.sol_packet.accepted_character_count =
				rsp->data[x++];

			rsp->payload.sol_packet.is_nack =
				rsp->data[x] & 0x40;

			rsp->payload.sol_packet.transfer_unavailable =
				rsp->data[x] & 0x20;

			rsp->payload.sol_packet.sol_inactive = 
				rsp->data[x] & 0x10;

			rsp->payload.sol_packet.transmit_overrun =
				rsp->data[x] & 0x08;
	
			rsp->payload.sol_packet.break_detected =
				rsp->data[x++] & 0x04;

			x++; 	
		}
		else
		{
			/* Standard IPMI 1.5 packet */
			rsp->session.payloadtype = IPMI_PAYLOAD_TYPE_IPMI;
			if (intf->session->active && (rsp->session.authtype || intf->session->authtype))
				x += 16;

			rsp->session.msglen = rsp->data[x++];
			rsp->payload.ipmi_response.rq_addr = rsp->data[x++];
			rsp->payload.ipmi_response.netfn   = rsp->data[x] >> 2;
			rsp->payload.ipmi_response.rq_lun  = rsp->data[x++] & 0x3;
			x++;		/* checksum */
			rsp->payload.ipmi_response.rs_addr = rsp->data[x++];
			rsp->payload.ipmi_response.rq_seq  = rsp->data[x] >> 2;
			rsp->payload.ipmi_response.rs_lun  = rsp->data[x++] & 0x3;
			rsp->payload.ipmi_response.cmd     = rsp->data[x++];
			rsp->ccode          = rsp->data[x++];
			
			/* now see if we have outstanding entry in request list */
			entry = ipmi_req_lookup_entry(rsp->payload.ipmi_response.rq_seq,
						      rsp->payload.ipmi_response.cmd);
			if (entry) {
				if ((intf->target_addr != our_address) && bridge_possible) {
					
					/* bridged command: lose extra header */
					if (entry->bridging_level &&
					    rsp->payload.ipmi_response.netfn == 7 &&
					    rsp->payload.ipmi_response.cmd == 0x34) {
						entry->bridging_level--;
						if (rsp->data_len - x - 1 == 0) {
							rsp = !rsp->ccode ? ipmi_lan_recv_packet(intf) : NULL;
							if (!entry->bridging_level)
								entry->req.msg.cmd = entry->req.msg.target_cmd;
							if (rsp == NULL) {
								ipmi_req_remove_entry(entry->rq_seq, entry->req.msg.cmd);
							}
							continue;
						} else {
							/* The bridged answer data are inside the incoming packet */
							memmove(rsp->data + x - 7,
								rsp->data + x, 
								rsp->data_len - x - 1);
							rsp->data[x - 8] -= 8;
							rsp->data_len -= 8;
							entry->rq_seq = rsp->data[x - 3] >> 2;
							if (!entry->bridging_level)
								entry->req.msg.cmd = entry->req.msg.target_cmd;
							continue;
						}
					}
				}
				ipmi_req_remove_entry(rsp->payload.ipmi_response.rq_seq,
						      rsp->payload.ipmi_response.cmd);
			} else {
				rsp = ipmi_lan_recv_packet(intf);
				continue;
			}
		}

		break;
	}

	/* shift response data to start of array */
	if (rsp && rsp->data_len > x) {
		rsp->data_len -= x;
		if (rsp->session.payloadtype == IPMI_PAYLOAD_TYPE_IPMI)
			rsp->data_len -= 1; /* We don't want the checksum */
		memmove(rsp->data, rsp->data + x, rsp->data_len);
		memset(rsp->data + rsp->data_len, 0, IPMI_BUF_SIZE - rsp->data_len);
	}

	return rsp;
}

/*
 * IPMI LAN Request Message Format
 * +--------------------+
 * |  rmcp.ver          | 4 bytes
 * |  rmcp.__reserved   |
 * |  rmcp.seq          |
 * |  rmcp.class        |
 * +--------------------+
 * |  session.authtype  | 9 bytes
 * |  session.seq       |
 * |  session.id        |
 * +--------------------+
 * | [session.authcode] | 16 bytes (AUTHTYPE != none)
 * +--------------------+
 * |  message length    | 1 byte
 * +--------------------+
 * |  message.rs_addr   | 6 bytes
 * |  message.netfn_lun |
 * |  message.checksum  |
 * |  message.rq_addr   |
 * |  message.rq_seq    |
 * |  message.cmd       |
 * +--------------------+
 * | [request data]     | data_len bytes
 * +--------------------+
 * |  checksum          | 1 byte
 * +--------------------+
 */
static struct ipmi_rq_entry * ipmi_lan_build_cmd(struct ipmi_intf * intf, struct ipmi_rq * req, int isRetry)
{
	struct rmcp_hdr rmcp = {
		.ver		= RMCP_VERSION_1,
		.class		= RMCP_CLASS_IPMI,
		.seq		= 0xff,
	};
	uint8_t * msg, * temp;
	int cs, mp, tmp;
	int ap = 0;
	int len = 0;
	int cs2 = 0, cs3 = 0;
	struct ipmi_rq_entry * entry;
	struct ipmi_session * s = intf->session;
	static int curr_seq = 0;
	uint8_t our_address = intf->my_addr;

	if (our_address == 0)
		our_address = IPMI_BMC_SLAVE_ADDR;

	if (isRetry == 0)
		curr_seq++;

	if (curr_seq >= 64)
		curr_seq = 0;

	// Bug in the existing code where it keeps on adding same command/seq pair 
	// in the lookup entry list.
	// Check if we have cmd,seq pair already in our list. As we are not changing 
	// the seq number we have to re-use the node which has existing
	// command and sequence number. If we add then we will have redundant node with
	// same cmd,seq pair
	entry = ipmi_req_lookup_entry(curr_seq, req->msg.cmd);
	if (entry)
	{
		// This indicates that we have already same command and seq in list
		// No need to add once again and we will re-use the existing node.
		// Only thing we have to do is clear the msg_data as we create
		// a new one below in the code for it.
		if (entry->msg_data)
			free(entry->msg_data);
	}
	else
	{
		// We dont have this request in the list so we can add it 
		// to the list
		entry = ipmi_req_add_entry(intf, req, curr_seq);
		if (entry == NULL)
			return NULL;
	}
 
	len = req->msg.data_len + 29;
	if (s->active && s->authtype)
		len += 16;
	if (intf->transit_addr != intf->my_addr && intf->transit_addr != 0)
		len += 8;
	msg = malloc(len);
	if (msg == NULL) {
		return NULL;
	}
	memset(msg, 0, len);

	/* rmcp header */
	memcpy(msg, &rmcp, sizeof(rmcp));
	len = sizeof(rmcp);

	/* ipmi session header */
	msg[len++] = s->active ? s->authtype : 0;

	msg[len++] = s->in_seq & 0xff;
	msg[len++] = (s->in_seq >> 8) & 0xff;
	msg[len++] = (s->in_seq >> 16) & 0xff;
	msg[len++] = (s->in_seq >> 24) & 0xff;
	memcpy(msg+len, &s->session_id, 4);
	len += 4;

	/* ipmi session authcode */
	if (s->active && s->authtype) {
		ap = len;
		memcpy(msg+len, s->authcode, 16);
		len += 16;
	}

	/* message length */
	if ((intf->target_addr == our_address) || !bridge_possible) {
		entry->bridging_level = 0;
		msg[len++] = req->msg.data_len + 7;
		cs = mp = len;
	} else {
		/* bridged request: encapsulate w/in Send Message */
		entry->bridging_level = 1;
		msg[len++] = req->msg.data_len + 15 +
		  (intf->transit_addr != intf->my_addr && intf->transit_addr != 0 ? 8 : 0);
		cs = mp = len;
		msg[len++] = IPMI_BMC_SLAVE_ADDR;
		msg[len++] = IPMI_NETFN_APP << 2;
		tmp = len - cs;
		msg[len++] = ipmi_csum(msg+cs, tmp);
		cs2 = len;
		msg[len++] = IPMI_REMOTE_SWID;
		msg[len++] = curr_seq << 2;
		msg[len++] = 0x34;			/* Send Message rqst */
		entry->req.msg.target_cmd = entry->req.msg.cmd;	/* Save target command */
		entry->req.msg.cmd = 0x34;		/* (fixup request entry) */

		if (intf->transit_addr == intf->my_addr || intf->transit_addr == 0) {
		        msg[len++] = (0x40|intf->target_channel); /* Track request*/
		} else {
		        entry->bridging_level++;
               		msg[len++] = (0x40|intf->transit_channel); /* Track request*/
			cs = len;
			msg[len++] = intf->transit_addr;
			msg[len++] = IPMI_NETFN_APP << 2;
			tmp = len - cs;
			msg[len++] = ipmi_csum(msg+cs, tmp);
			cs3 = len;
			msg[len++] = intf->my_addr;
			msg[len++] = curr_seq << 2;
			msg[len++] = 0x34;			/* Send Message rqst */
			msg[len++] = (0x40|intf->target_channel); /* Track request */
		}
		cs = len;
	}

	/* ipmi message header */
	msg[len++] = intf->target_addr;
	msg[len++] = req->msg.netfn << 2 | (req->msg.lun & 3);
	//printf("Lun : %2x \n", (req->msg.lun & 3));	//JULIAN (Debug)
	
	tmp = len - cs;
	msg[len++] = ipmi_csum(msg+cs, tmp);
	cs = len;

	if (!entry->bridging_level)
		msg[len++] = IPMI_REMOTE_SWID;
   /* Bridged message */ 
	else if (entry->bridging_level) 
		msg[len++] = intf->my_addr;
   
	entry->rq_seq = curr_seq;
	msg[len++] = entry->rq_seq << 2;
	msg[len++] = req->msg.cmd;

	/* message data */
	if (req->msg.data_len) {
 		memcpy(msg+len, req->msg.data, req->msg.data_len);
		len += req->msg.data_len;
	}

	/* second checksum */
	tmp = len - cs;
	msg[len++] = ipmi_csum(msg+cs, tmp);

	/* bridged request: 2nd checksum */
	if (entry->bridging_level) {
		if (intf->transit_addr != intf->my_addr && intf->transit_addr != 0) {
			tmp = len - cs3;
			msg[len++] = ipmi_csum(msg+cs3, tmp);
		}
		tmp = len - cs2;
		msg[len++] = ipmi_csum(msg+cs2, tmp);
	}

	if (s->active) {
		/*
		 * s->authcode is already copied to msg+ap but some
		 * authtypes require portions of the ipmi message to
		 * create the authcode so they must be done last.
		 */
		switch (s->authtype) {
		case IPMI_SESSION_AUTHTYPE_MD5:
			temp = ipmi_auth_md5(s, msg+mp, msg[mp-1]);
			memcpy(msg+ap, temp, 16);
			break;
		case IPMI_SESSION_AUTHTYPE_MD2:
			temp = ipmi_auth_md2(s, msg+mp, msg[mp-1]);
			memcpy(msg+ap, temp, 16);
			break;
		}
	}

	if (s->in_seq) {
		s->in_seq++;
		if (s->in_seq == 0)
			s->in_seq++;
	}

	entry->msg_len = len;
	entry->msg_data = msg;

	return entry;
}

static struct ipmi_rs * ipmi_lan_send_cmd(struct ipmi_intf * intf, struct ipmi_rq * req)
{
	struct ipmi_rq_entry * entry;
	struct ipmi_rs * rsp = NULL;
	int try = 0;
	int isRetry = 0;

	if (intf->opened == 0 && intf->open != NULL) {
		if (intf->open(intf) < 0) {
			return NULL;
		}
	}

	for (;;) {
		isRetry = ( try > 0 ) ? 1 : 0;

		entry = ipmi_lan_build_cmd(intf, req, isRetry);
		if (entry == NULL) {
			return NULL;
		}

		if (ipmi_lan_send_packet(intf, entry->msg_data, entry->msg_len) < 0) {
			try++;
			usleep(5000);
			ipmi_req_remove_entry(entry->rq_seq, entry->req.msg.target_cmd);	
			continue;
		}

		usleep(100);

		rsp = ipmi_lan_poll_recv(intf);

		/* Duplicate Request ccode most likely indicates a response to
		   a previous retry. Ignore and keep polling. */
		if((rsp != NULL) && (rsp->ccode == 0xcf)) {
			rsp = NULL;
			rsp = ipmi_lan_poll_recv(intf);
		}
		
		if (rsp)
			break;

		usleep(5000);
		if (++try >= intf->session->retry) {
			if(intf->session->retry == -1){
				fprintf(stderr, "Retries are disabled.\n");
			}
			break;
		}
	}

	// We need to cleanup the existing entries from the list. Because if we 
	// keep it and then when we send the new command and if the response is for
	// old command it still matches it and then returns success.
	// This is the corner case where the remote controller responds very slowly.
	//
	// Example: We have to send command 23 and 2d.
	// If we send command,seq as 23,10 and if we dont get any response it will 
	// retry 4 times with 23,10 and then come out here and indicate that there is no
	// reponse from the remote controller and will send the next command for 
	// ie 2d,11. And if the BMC is slow to respond and returns 23,10 then it 
	// will match it in the list and will take response of command 23 as response 
	// for command 2d and return success. So ideally when retries are done and 
	// are out of this function we should be clearing the list to be safe so that
	// we dont match the old response with new request.
	//          [23, 10] --> BMC
	//          [23, 10] --> BMC
	//          [23, 10] --> BMC
	//          [23, 10] --> BMC
	//          [2D, 11] --> BMC
	//                   <-- [23, 10]
	//  here if we maintain 23,10 in the list then it will get matched and consider
	//  23 response as response for 2D.   
	ipmi_req_clear_entries();
 
	return rsp;
}

static uint8_t * ipmi_lan_build_rsp(struct ipmi_intf * intf, struct ipmi_rs * rsp, int * llen){
	struct rmcp_hdr rmcp = {
		.ver	= RMCP_VERSION_1,
		.class	= RMCP_CLASS_IPMI,
		.seq	= 0xff,
	};
	struct ipmi_session * s = intf->session;
	int cs, mp, ap = 0, tmp;
	int len;
	uint8_t * msg;

	len = rsp->data_len + 22;
	if (s->active)
		len += 16;

	msg = malloc(len);
	if (msg == NULL) {
		return NULL;
	}
	memset(msg, 0, len);

	/* rmcp header */
	memcpy(msg, &rmcp, 4);
	len = sizeof(rmcp);

	/* ipmi session header */
	msg[len++] = s->active ? s->authtype : 0;

	if (s->in_seq) {
		s->in_seq++;
		if (s->in_seq == 0)
			s->in_seq++;
	}
	memcpy(msg+len, &s->in_seq, 4);
	len += 4;
	memcpy(msg+len, &s->session_id, 4);
	len += 4;

	/* session authcode, if session active and authtype is not none */
	if (s->active && s->authtype) {
		ap = len;
		memcpy(msg+len, s->authcode, 16);
		len += 16;
	}

	/* message length */
	msg[len++] = rsp->data_len + 8;

	/* message header */
	cs = mp = len;
	msg[len++] = IPMI_REMOTE_SWID;
	msg[len++] = rsp->msg.netfn << 2;
	tmp = len - cs;
	msg[len++] = ipmi_csum(msg+cs, tmp);
	cs = len;
	msg[len++] = IPMI_BMC_SLAVE_ADDR;
	msg[len++] = (rsp->msg.seq << 2) | (rsp->msg.lun & 3);
	msg[len++] = rsp->msg.cmd;

	/* completion code */
	msg[len++] = rsp->ccode;

	/* message data */
	if (rsp->data_len) {
		memcpy(msg+len, rsp->data, rsp->data_len);
		len += rsp->data_len;
	}

	/* second checksum */
	tmp = len - cs;
	msg[len++] = ipmi_csum(msg+cs, tmp);

	if (s->active) {
		uint8_t * d;
		switch (s->authtype) {
		case IPMI_SESSION_AUTHTYPE_MD5:
			d = ipmi_auth_md5(s, msg+mp, msg[mp-1]);
			memcpy(msg+ap, d, 16);
			break;
		case IPMI_SESSION_AUTHTYPE_MD2:
			d = ipmi_auth_md2(s, msg+mp, msg[mp-1]);
			memcpy(msg+ap, d, 16);
			break;
		}
	}

	*llen = len;
	return msg;
}

/* send a get device id command to keep session active */
static int
ipmi_lan_keepalive(struct ipmi_intf * intf)
{
	struct ipmi_rs * rsp;
	struct ipmi_rq req = { msg: {
		netfn: IPMI_NETFN_APP,
		cmd: 1,
	}};

	if (!intf->opened)
		return 0;

	rsp = intf->sendrecv(intf, &req);
	if (rsp == NULL)
		return -1;
	if (rsp->ccode > 0)
		return -1;

	return 0;
}

/*
 * IPMI Get Channel Authentication Capabilities Command
 */
static int ipmi_get_auth_capabilities_cmd(struct ipmi_intf * intf)
{
	struct ipmi_rs * rsp;
	struct ipmi_rq req;
	struct ipmi_session * s = intf->session;
	uint8_t msg_data[2];

	msg_data[0] = IPMI_LAN_CHANNEL_E;
	msg_data[1] = s->privlvl;

	memset(&req, 0, sizeof(req));
	req.msg.netfn    = IPMI_NETFN_APP;
	req.msg.cmd      = 0x38;
	req.msg.data     = msg_data;
	req.msg.data_len = 2;

	rsp = intf->sendrecv(intf, &req);
	if (rsp == NULL) {
		return -1;
	}

	if (rsp->ccode > 0) {
		return -1;
	}

	s->authstatus = rsp->data[2];

	if (s->password &&
	    (s->authtype_set == 0 ||
	     s->authtype_set == IPMI_SESSION_AUTHTYPE_MD5) &&
	    (rsp->data[1] & 1<<IPMI_SESSION_AUTHTYPE_MD5))
	{
		s->authtype = IPMI_SESSION_AUTHTYPE_MD5;
	}
	else if (s->password &&
		 (s->authtype_set == 0 ||
		  s->authtype_set == IPMI_SESSION_AUTHTYPE_MD2) &&
		 (rsp->data[1] & 1<<IPMI_SESSION_AUTHTYPE_MD2))
	{
		s->authtype = IPMI_SESSION_AUTHTYPE_MD2;
	}
	else if (s->password &&
		 (s->authtype_set == 0 ||
		  s->authtype_set == IPMI_SESSION_AUTHTYPE_PASSWORD) &&
		 (rsp->data[1] & 1<<IPMI_SESSION_AUTHTYPE_PASSWORD))
	{
		s->authtype = IPMI_SESSION_AUTHTYPE_PASSWORD;
	}
	else if (s->password &&
		 (s->authtype_set == 0 ||
		  s->authtype_set == IPMI_SESSION_AUTHTYPE_OEM) &&
		 (rsp->data[1] & 1<<IPMI_SESSION_AUTHTYPE_OEM))
	{
		s->authtype = IPMI_SESSION_AUTHTYPE_OEM;
	}
	else if ((s->authtype_set == 0 ||
		  s->authtype_set == IPMI_SESSION_AUTHTYPE_NONE) &&
		 (rsp->data[1] & 1<<IPMI_SESSION_AUTHTYPE_NONE))
	{
		s->authtype = IPMI_SESSION_AUTHTYPE_NONE;
	}
	else {
		return -1;
	}

	return 0;
}

/*
 * IPMI Get Session Challenge Command
 * returns a temporary session ID and 16 byte challenge string
 */
static int ipmi_get_session_challenge_cmd(struct ipmi_intf * intf)
{
	struct ipmi_rs * rsp;
	struct ipmi_rq req;
	struct ipmi_session * s = intf->session;
	uint8_t msg_data[17];

	memset(msg_data, 0, 17);
	msg_data[0] = s->authtype;
	memcpy(msg_data+1, s->username, 16);

	memset(&req, 0, sizeof(req));
	req.msg.netfn		= IPMI_NETFN_APP;
	req.msg.cmd		= 0x39;
	req.msg.data		= msg_data;
	req.msg.data_len	= 17; /* 1 byte for authtype, 16 for user */

	rsp = intf->sendrecv(intf, &req);
	if (rsp == NULL) {
		return -1;
	}

	if (rsp->ccode > 0) {
		return -1;
	}

	memcpy(&s->session_id, rsp->data, 4);
	memcpy(s->challenge, rsp->data + 4, 16);

	return 0;
}

/*
 * IPMI Activate Session Command
 */
static int ipmi_activate_session_cmd(struct ipmi_intf * intf)
{
	struct ipmi_rs * rsp;
	struct ipmi_rq req;
	struct ipmi_session * s = intf->session;
	uint8_t msg_data[22];

	memset(&req, 0, sizeof(req));
	req.msg.netfn = IPMI_NETFN_APP;
	req.msg.cmd = 0x3a;

	msg_data[0] = s->authtype;
	msg_data[1] = s->privlvl;


	memcpy(msg_data + 2, s->challenge, 16);
		
	/* setup initial outbound sequence number */
	get_random(msg_data+18, 4);

	req.msg.data = msg_data;
	req.msg.data_len = 22;

	s->active = 1;

	rsp = intf->sendrecv(intf, &req);
	if (rsp == NULL) {
		s->active = 0;
		return -1;
	}

	if (rsp->ccode) {
		return -1;
	}

	memcpy(&s->session_id, rsp->data + 1, 4);
	s->in_seq = rsp->data[8] << 24 | rsp->data[7] << 16 | rsp->data[6] << 8 | rsp->data[5];
	if (s->in_seq == 0)
		++s->in_seq;

	if (s->authstatus & IPMI_AUTHSTATUS_PER_MSG_DISABLED)
		s->authtype = IPMI_SESSION_AUTHTYPE_NONE;
	else if (s->authtype != (rsp->data[0] & 0xf)) {
		return -1;
	}

	bridge_possible = 1;
	
	return 0;
}


/*
 * IPMI Set Session Privilege Level Command
 */
static int ipmi_set_session_privlvl_cmd(struct ipmi_intf * intf)
{
	struct ipmi_rs * rsp;
	struct ipmi_rq req;
	uint8_t privlvl = intf->session->privlvl;
	uint8_t backup_bridge_possible = bridge_possible;

	if (privlvl <= IPMI_SESSION_PRIV_USER)
		return 0;	/* no need to set higher */

	memset(&req, 0, sizeof(req));
	req.msg.netfn		= IPMI_NETFN_APP;
	req.msg.cmd		= 0x3b;
	req.msg.data		= &privlvl;
	req.msg.data_len	= 1;

	bridge_possible = 0;
	rsp = intf->sendrecv(intf, &req);
	bridge_possible = backup_bridge_possible;

	if (rsp == NULL) {
		return -1;
	}

	if (rsp->ccode > 0) {
		return -1;
	}

	return 0;
}

static int ipmi_close_session_cmd(struct ipmi_intf * intf)
{
	struct ipmi_rs * rsp;
	struct ipmi_rq req;
	uint8_t msg_data[4];
	uint32_t session_id = intf->session->session_id;

	if (intf->session->active == 0)
		return -1;

	intf->target_addr = IPMI_BMC_SLAVE_ADDR;
	bridge_possible = 0;  /* Not a bridge message */

	memcpy(&msg_data, &session_id, 4);

	memset(&req, 0, sizeof(req));
	req.msg.netfn		= IPMI_NETFN_APP;
	req.msg.cmd		= 0x3c;
	req.msg.data		= msg_data;
	req.msg.data_len	= 4;

	rsp = intf->sendrecv(intf, &req);
	if (rsp == NULL) {
		return -1;
	}
	
	if (rsp->ccode == 0x87) {
		return -1;
	}
	if (rsp->ccode > 0) {
		return -1;
	}

	return 0;
}

/*
 * IPMI LAN Session Activation (IPMI spec v1.5 section 12.9)
 *
 * 1. send "RMCP Presence Ping" message, response message will
 *    indicate whether the platform supports IPMI
 * 2. send "Get Channel Authentication Capabilities" command
 *    with AUTHTYPE = none, response packet will contain information
 *    about supported challenge/response authentication types
 * 3. send "Get Session Challenge" command with AUTHTYPE = none
 *    and indicate the authentication type in the message, response
 *    packet will contain challenge string and temporary session ID.
 * 4. send "Activate Session" command, authenticated with AUTHTYPE
 *    sent in previous message.  Also sends the initial value for
 *    the outbound sequence number for BMC.
 * 5. BMC returns response confirming session activation and
 *    session ID for this session and initial inbound sequence.
 */
static int ipmi_lan_activate_session(struct ipmi_intf * intf)
{
	int rc;

	/* don't fail on ping because its not always supported.
	 * Supermicro's IPMI LAN 1.5 cards don't tolerate pings.
	 */

	rc = ipmi_get_auth_capabilities_cmd(intf);
	if (rc < 0) {
		sleep(1);
		rc = ipmi_get_auth_capabilities_cmd(intf);
		if (rc < 0)
			goto fail;
	}

	rc = ipmi_get_session_challenge_cmd(intf);
	if (rc < 0)
		goto fail;

	rc = ipmi_activate_session_cmd(intf);
	if (rc < 0)
		goto fail;

	intf->abort = 0;

	rc = ipmi_set_session_privlvl_cmd(intf);
	if (rc < 0)
		goto fail;

	return 0;

 fail:
	return -1;
}

static void ipmi_lan_close(struct ipmi_intf * intf)
{
	if (intf->abort == 0)
		ipmi_close_session_cmd(intf);

	if (intf->fd >= 0)
		close(intf->fd);

	ipmi_req_clear_entries();

	if (intf->session != NULL) {
		free(intf->session);
		intf->session = NULL;
	}

	intf->opened = 0;
	intf = NULL;
}

static int ipmi_lan_open(struct ipmi_intf * intf)
{
	int rc;
	struct ipmi_session *s;

	if (intf == NULL || intf->session == NULL)
		return -1;
	
	s = intf->session;

	if (s->port == 0)
		s->port = IPMI_LAN_PORT;
	if (s->privlvl == 0)
		s->privlvl = IPMI_SESSION_PRIV_ADMIN;
	if (s->timeout == 0)
		s->timeout = IPMI_LAN_TIMEOUT;
	if (s->retry == 0)
		s->retry = IPMI_LAN_RETRY;

	if (s->hostname == NULL || strlen((const char *)s->hostname) == 0) {
		return -1;
	}

	intf->abort = 1;

	intf->session->sol_data.sequence_number = 1;
	
	/* open port to BMC */
	memset(&s->addr, 0, sizeof(struct sockaddr_in));
	s->addr.sin_family = AF_INET;
	s->addr.sin_port = htons(s->port);

	rc = inet_pton(AF_INET, (const char *)s->hostname, &s->addr.sin_addr);
	if (rc <= 0) {
		struct hostent *host = gethostbyname((const char *)s->hostname);
		if (host == NULL) {
			return -1;
		}
		s->addr.sin_family = host->h_addrtype;
		memcpy(&s->addr.sin_addr, host->h_addr, host->h_length);
	}

	intf->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (intf->fd < 0) {
		return -1;
	}

	/* connect to UDP socket so we get async errors */
	rc = connect(intf->fd, (struct sockaddr *)&s->addr, sizeof(struct sockaddr_in));
	if (rc < 0) {
		intf->close(intf);
		return -1;
	}

	intf->opened = 1;

	/* try to open session */
	rc = ipmi_lan_activate_session(intf);
	if (rc < 0) {
		intf->close(intf);
		intf->opened = 0;
		return -1;
	}

	return intf->fd;
}

static int ipmi_lan_setup(struct ipmi_intf * intf)
{
	intf->session = malloc(sizeof(struct ipmi_session));
	if (intf->session == NULL) {
		return -1;
	}
	memset(intf->session, 0, sizeof(struct ipmi_session));
	return 0;
}
