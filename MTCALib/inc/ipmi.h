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

#ifndef IPMI_H
#define IPMI_H

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/types.h>
#include <netinet/in.h>

#if HAVE_CONFIG_H
# include <config.h>
#endif

#define IPMI_BUF_SIZE 1024

#if HAVE_PRAGMA_PACK
#define ATTRIBUTE_PACKING
#else
#define ATTRIBUTE_PACKING __attribute__ ((packed))
#endif


/* From table 13.16 of the IPMI v2 specification */
#define IPMI_PAYLOAD_TYPE_IPMI               0x00
#define IPMI_PAYLOAD_TYPE_SOL                0x01
#define IPMI_PAYLOAD_TYPE_OEM                0x02
#define IPMI_PAYLOAD_TYPE_RMCP_OPEN_REQUEST  0x10
#define IPMI_PAYLOAD_TYPE_RMCP_OPEN_RESPONSE 0x11
#define IPMI_PAYLOAD_TYPE_RAKP_1             0x12
#define IPMI_PAYLOAD_TYPE_RAKP_2             0x13
#define IPMI_PAYLOAD_TYPE_RAKP_3             0x14
#define IPMI_PAYLOAD_TYPE_RAKP_4             0x15

//Completion code list
#define IPMI_CC_OK                                 0x00 
#define IPMI_CC_NODE_BUSY                          0xc0 
#define IPMI_CC_INV_CMD                            0xc1 
#define IPMI_CC_INV_CMD_FOR_LUN                    0xc2 
#define IPMI_CC_TIMEOUT                            0xc3 
#define IPMI_CC_OUT_OF_SPACE                       0xc4 
#define IPMI_CC_RES_CANCELED                       0xc5 
#define IPMI_CC_REQ_DATA_TRUNC                     0xc6 
#define IPMI_CC_REQ_DATA_INV_LENGTH                0xc7 
#define IPMI_CC_REQ_DATA_FIELD_EXCEED              0xc8 
#define IPMI_CC_PARAM_OUT_OF_RANGE                 0xc9 
#define IPMI_CC_CANT_RET_NUM_REQ_BYTES             0xca 
#define IPMI_CC_REQ_DATA_NOT_PRESENT               0xcb 
#define IPMI_CC_INV_DATA_FIELD_IN_REQ              0xcc 
#define IPMI_CC_ILL_SENSOR_OR_RECORD               0xcd 
#define IPMI_CC_RESP_COULD_NOT_BE_PRV              0xce 
#define IPMI_CC_CANT_RESP_DUPLI_REQ                0xcf 
#define IPMI_CC_CANT_RESP_SDRR_UPDATE              0xd0 
#define IPMI_CC_CANT_RESP_FIRM_UPDATE              0xd1 
#define IPMI_CC_CANT_RESP_BMC_INIT                 0xd2 
#define IPMI_CC_DESTINATION_UNAVAILABLE            0xd3 
#define IPMI_CC_INSUFFICIENT_PRIVILEGES            0xd4 
#define IPMI_CC_NOT_SUPPORTED_PRESENT_STATE        0xd5 
#define IPMI_CC_ILLEGAL_COMMAND_DISABLED           0xd6 
#define IPMI_CC_UNSPECIFIED_ERROR                  0xff 

struct ipmi_rq {
	struct {
		uint8_t netfn:6;
		uint8_t lun:2;
		uint8_t cmd;
		uint8_t target_cmd;
		uint16_t data_len;
		uint8_t *data;
	} msg;
};

struct ipmi_rq_entry {
	struct ipmi_rq req;
	struct ipmi_intf *intf;
	uint8_t rq_seq;
	uint8_t *msg_data;
	int msg_len;
	int bridging_level;
	struct ipmi_rq_entry *next;
};

struct ipmi_rs {
	uint8_t ccode;
	uint8_t data[IPMI_BUF_SIZE];

	/*
	 * Looks like this is the length of the entire packet, including the RMCP
	 * stuff, then modified to be the length of the extra IPMI message data
	 */
	int data_len;

	struct {
		uint8_t netfn;
		uint8_t cmd;
		uint8_t seq;
		uint8_t lun;
	} msg;

	struct {
		uint8_t authtype;									//Used
		uint32_t seq;										//Used
		uint32_t id;										//Used
		uint8_t bEncrypted;	/* IPMI v2 only */				
		uint8_t bAuthenticated;	/* IPMI v2 only */
		uint8_t payloadtype;	/* IPMI v2 only */			//Used
		/* This is the total length of the payload or
		   IPMI message.  IPMI v2.0 requires this to
		   be 2 bytes.  Not really used for much. */
		uint16_t msglen;									//Used
	} session;

	/*
	 * A union of the different possible payload meta-data
	 */
	union {
		struct {
			uint8_t rq_addr;
			uint8_t netfn;
			uint8_t rq_lun;
			uint8_t rs_addr;
			uint8_t rq_seq;
			uint8_t rs_lun;
			uint8_t cmd;
		} ipmi_response;
		struct {
			uint8_t packet_sequence_number;
			uint8_t acked_packet_number;
			uint8_t accepted_character_count;
			uint8_t is_nack;	/* bool */
			uint8_t transfer_unavailable;	/* bool */
			uint8_t sol_inactive;	/* bool */
			uint8_t transmit_overrun;	/* bool */
			uint8_t break_detected;	/* bool */
		} sol_packet;
	} payload;
};

#define IPMI_NETFN_CHASSIS		0x0
#define IPMI_NETFN_BRIDGE		0x2
#define IPMI_NETFN_SE			0x4
#define IPMI_NETFN_APP			0x6
#define IPMI_NETFN_FIRMWARE		0x8
#define IPMI_NETFN_STORAGE		0xa
#define IPMI_NETFN_TRANSPORT	0xc
#define IPMI_NETFN_PICMG		0x2C
#define IPMI_NETFN_DCGRP		0x2C
#define IPMI_NETFN_ISOL			0x34
#define IPMI_NETFN_TSOL			0x30

#define IPMI_BMC_SLAVE_ADDR		0x20
#define IPMI_REMOTE_SWID		0x81

#endif				/* IPMI_H */
