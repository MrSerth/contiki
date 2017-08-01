/*
 * Copyright (c) 2017, Hasso Plattner Institute, Potsdam
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
 *      Collection of pre shared keys.
 */

#ifndef ER_COAP_PSK_H_
#define ER_COAP_PSK_H_

/**
 * To add a new PSK:
 * 1. Create a new variable similar to coap_psk_1 (increase the counter by one) with a variable length of raw bytes
 *    Caution: The encryption / decryption module will currently always use the first 16 Byte
 *             (fixed length due to AES-128) or add Null Bytes (0x00) if the length is smaller than 16 Byte.
 *             The variable length is currently only used for the HMAC.
 * 2. Add your variable name to the presharedkeys and presharedkeys_len arrays similar to the existing ones
 */


/*
 * Pre-Shared Keys
 */
static uint8_t coap_psk_1[] =             { 0x00 , 0x01 , 0x02 , 0x03 , \
                                            0x04 , 0x05 , 0x06 , 0x07 , \
                                            0x08 , 0x09 , 0x0A , 0x0B , \
                                            0x0C , 0x0D , 0x0E , 0x0F };

static uint8_t coap_psk_2[] =             { 0x0F , 0x0E , 0x0D , 0x0C , \
                                            0x0B , 0x0A , 0x09 , 0x08 , \
                                            0x07 , 0x06 , 0x05 , 0x04 , \
                                            0x03 , 0x02 , 0x01 , 0x00 };

/*
 * Array with easy access at run time to all keys
 */
static uint8_t* presharedkeys[] =         { (uint8_t *) NULL ,
                                            (uint8_t *) &coap_psk_1 ,
                                            (uint8_t *) &coap_psk_2 };

static uint8_t presharedkeys_len[] =      { 0 ,
                                            sizeof(coap_psk_1) ,
                                            sizeof(coap_psk_2) };

#endif
