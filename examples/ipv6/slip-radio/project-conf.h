/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

#undef UIP_CONF_BUFFER_SIZE
#define UIP_CONF_BUFFER_SIZE    140

#undef UIP_CONF_ROUTER
#define UIP_CONF_ROUTER                 0

#define CMD_CONF_OUTPUT slip_radio_cmd_output

/* add the cmd_handler_cc2420 + some sensors if TARGET_SKY */
#ifdef CONTIKI_TARGET_SKY
#define CMD_CONF_HANDLERS slip_radio_cmd_handler,cmd_handler_cc2420
#define SLIP_RADIO_CONF_SENSORS slip_radio_sky_sensors
/* add the cmd_handler_rf230 if TARGET_NOOLIBERRY. Other RF230 platforms can be added */
#elif CONTIKI_TARGET_NOOLIBERRY
#define CMD_CONF_HANDLERS slip_radio_cmd_handler,cmd_handler_rf230
#elif CONTIKI_TARGET_ECONOTAG
#define CMD_CONF_HANDLERS slip_radio_cmd_handler,cmd_handler_mc1322x
#else
#define CMD_CONF_HANDLERS slip_radio_cmd_handler
#endif

/* SLIP does not work otherwise */
#undef LPM_CONF_MAX_PM
#define LPM_CONF_MAX_PM 0

/* configure RDC layer */
#include "cpu/cc2538/dev/cc2538-rf-async-autoconf.h"
#include "net/mac/contikimac/secrdc-autoconf.h"

/* configure MAC layer */
#if 1
#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC csma_driver
#undef CSMA_CONF_MAX_FRAME_RETRIES
#define CSMA_CONF_MAX_FRAME_RETRIES 3
#undef CSMA_CONF_MAX_NEIGHBOR_QUEUES
#define CSMA_CONF_MAX_NEIGHBOR_QUEUES 5
#else
#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC nullmac_driver
#endif

/* configure LLSEC layer */
#undef ADAPTIVESEC_CONF_UNICAST_SEC_LVL
#define ADAPTIVESEC_CONF_UNICAST_SEC_LVL 2
#undef ADAPTIVESEC_CONF_BROADCAST_SEC_LVL
#define ADAPTIVESEC_CONF_BROADCAST_SEC_LVL 2
#undef LLSEC802154_CONF_USES_AUX_HEADER
#define LLSEC802154_CONF_USES_AUX_HEADER 0
#undef NBR_TABLE_CONF_MAX_NEIGHBORS
#define NBR_TABLE_CONF_MAX_NEIGHBORS 14
#if 0
#include "net/llsec/adaptivesec/coresec-autoconf.h"
#else
#include "net/llsec/adaptivesec/noncoresec-autoconf.h"
#endif
#if 1
#include "net/llsec/adaptivesec/potr-autoconf.h"
#if 1
#include "net/mac/contikimac/ilos-autoconf.h"
#endif
#endif

/* configure FRAMERs */
#include "net/mac/contikimac/framer-autoconf.h"

#undef NETSTACK_CONF_NETWORK
#define NETSTACK_CONF_NETWORK slipnet_driver

/* set a seeder */
#undef CSPRNG_CONF_SEEDER
#define CSPRNG_CONF_SEEDER iq_seeder

#endif /* PROJECT_CONF_H_ */
