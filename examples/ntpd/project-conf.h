/*
 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
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
 *
 * This file is part of the Contiki operating system.
 */

/**
 * \file
 *      NTP example project configuration.
 */

#ifndef __PROJECT_NTP_CONF_H__
#define __PROJECT_NTP_CONF_H__

/* Custom channel and PAN ID configuration for your project. */
/*
   #undef RF_CHANNEL
   #define RF_CHANNEL                     26

   #undef IEEE802154_CONF_PANID
   #define IEEE802154_CONF_PANID          0xABCD
 */

/* IP buffer size must match all other hops, in particular the border router. */
/*
   #undef UIP_CONF_BUFFER_SIZE
   #define UIP_CONF_BUFFER_SIZE           256
 */

/* Disabling RDC and CSMA for demo purposes. Core updates often
   require more memory. */
/* For projects, optimize memory and enable RDC and CSMA again. */
#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC              nullrdc_driver

#undef RPL_CONF_MAX_DAG_PER_INSTANCE
#define RPL_CONF_MAX_DAG_PER_INSTANCE     1

/* Disabling TCP on CoAP nodes. */
#undef UIP_CONF_TCP
#define UIP_CONF_TCP                   0

#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC     nullmac_driver

/* Turn off DAO ACK to make code smaller */
#undef RPL_CONF_WITH_DAO_ACK
#define RPL_CONF_WITH_DAO_ACK          0


/* configure RDC layer */
#if 1
#include "cpu/cc2538/dev/cc2538-rf-async-autoconf.h"
#include "net/mac/contikimac/secrdc-autoconf.h"
#elif 0
#undef CONTIKIMAC_CONF_COMPOWER
#define CONTIKIMAC_CONF_COMPOWER 0
#undef RDC_CONF_HARDWARE_CSMA
#define RDC_CONF_HARDWARE_CSMA 1
#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC contikimac_driver
#else
#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC nullrdc_driver
#endif

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
#if 1
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
#include "net/mac/contikimac/ilocs-autoconf.h"
#endif
#endif
#endif

/* configure FRAMERs */
#include "net/mac/contikimac/framer-autoconf.h"

/* set a seeder */
#undef CSPRNG_CONF_SEEDER
#define CSPRNG_CONF_SEEDER iq_seeder

#endif /* __PROJECT_NTP_CONF_H__ */