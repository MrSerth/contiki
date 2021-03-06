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
 *      An implementation of the Constrained Application Protocol (RFC).
 * \author
 *      Matthias Kovatsch <kovatsch@inf.ethz.ch>
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "contiki.h"
#include "sys/cc.h"
#include "contiki-net.h"
#include "dev/sha256.h"
#include "lib/aes-128.h"
#include <cfs/cfs.h>

#include "er-coap.h"
#include "er-coap-transactions.h"

#include "er-coap-psk.h"

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF("[%02x:%02x:%02x:%02x:%02x:%02x]", (lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3], (lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif

/*---------------------------------------------------------------------------*/
/*- Variables ---------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
static struct uip_udp_conn *udp_conn = NULL;
static uint16_t current_mid = 0;

coap_status_t erbium_status_code = NO_ERROR;
char *coap_error_message = "";
/*---------------------------------------------------------------------------*/
/*- Local helper functions --------------------------------------------------*/
/*---------------------------------------------------------------------------*/
static uint16_t
coap_log_2(uint16_t value)
{
  uint16_t result = 0;

  do {
    value = value >> 1;
    result++;
  } while(value);

  return result ? result - 1 : result;
}
/*---------------------------------------------------------------------------*/
static uint32_t
coap_parse_int_option(uint8_t *bytes, size_t length)
{
  uint32_t var = 0;
  int i = 0;

  while(i < length) {
    var <<= 8;
    var |= bytes[i++];
  }
  return var;
}
/*---------------------------------------------------------------------------*/
static uint8_t
coap_option_nibble(unsigned int value)
{
  if(value < 13) {
    return value;
  } else if(value <= 0xFF + 13) {
    return 13;
  } else {
    return 14;
  }
}
/*---------------------------------------------------------------------------*/
static size_t
coap_set_option_header(unsigned int delta, size_t length, uint8_t *buffer)
{
  size_t written = 0;

  buffer[0] = coap_option_nibble(delta) << 4 | coap_option_nibble(length);

  if(delta > 268) {
    buffer[++written] = ((delta - 269) >> 8) & 0xff;
    buffer[++written] = (delta - 269) & 0xff;
  } else if(delta > 12) {
    buffer[++written] = (delta - 13);
  }

  if(length > 268) {
    buffer[++written] = ((length - 269) >> 8) & 0xff;
    buffer[++written] = (length - 269) & 0xff;
  } else if(length > 12) {
    buffer[++written] = (length - 13);
  }

  PRINTF("WRITTEN %zu B opt header\n", 1 + written);

  return ++written;
}
/*---------------------------------------------------------------------------*/
static size_t
coap_serialize_int_option(unsigned int number, unsigned int current_number,
                          uint8_t *buffer, uint32_t value)
{
  size_t i = 0;

  if(0xFF000000 & value) {
    ++i;
  }
  if(0xFFFF0000 & value) {
    ++i;
  }
  if(0xFFFFFF00 & value) {
    ++i;
  }
  if(0xFFFFFFFF & value) {
    ++i;
  }
  PRINTF("OPTION %u (delta %u, len %zu)\n", number, number - current_number,
         i);

  i = coap_set_option_header(number - current_number, i, buffer);

  if(0xFF000000 & value) {
    buffer[i++] = (uint8_t)(value >> 24);
  }
  if(0xFFFF0000 & value) {
    buffer[i++] = (uint8_t)(value >> 16);
  }
  if(0xFFFFFF00 & value) {
    buffer[i++] = (uint8_t)(value >> 8);
  }
  if(0xFFFFFFFF & value) {
    buffer[i++] = (uint8_t)(value);
  }
  return i;
}
/*---------------------------------------------------------------------------*/
static size_t
coap_serialize_array_option(unsigned int number, unsigned int current_number,
                            uint8_t *buffer, uint8_t *array, size_t length,
                            char split_char)
{
  size_t i = 0;

  PRINTF("ARRAY type %u, len %zu, full [%.*s]\n", number, length,
         (int)length, array);

  if(split_char != '\0') {
    int j;
    uint8_t *part_start = array;
    uint8_t *part_end = NULL;
    size_t temp_length;

    for(j = 0; j <= length + 1; ++j) {
      PRINTF("STEP %u/%zu (%c)\n", j, length, array[j]);
      if(array[j] == split_char || j == length) {
        part_end = array + j;
        temp_length = part_end - part_start;

        i += coap_set_option_header(number - current_number, temp_length,
                                    &buffer[i]);
        memcpy(&buffer[i], part_start, temp_length);
        i += temp_length;

        PRINTF("OPTION type %u, delta %u, len %zu, part [%.*s]\n", number,
               number - current_number, i, (int)temp_length, part_start);

        ++j;                    /* skip the splitter */
        current_number = number;
        part_start = array + j;
      }
    }                           /* for */
  } else {
    i += coap_set_option_header(number - current_number, length, &buffer[i]);
    memcpy(&buffer[i], array, length);
    i += length;

    PRINTF("OPTION type %u, delta %u, len %zu\n", number,
           number - current_number, length);
  }

  return i;
}
/*---------------------------------------------------------------------------*/
static void
coap_merge_multi_option(char **dst, size_t *dst_len, uint8_t *option,
                        size_t option_len, char separator)
{
  /* merge multiple options */
  if(*dst_len > 0) {
    /* dst already contains an option: concatenate */
    (*dst)[*dst_len] = separator;
    *dst_len += 1;

    /* memmove handles 2-byte option headers */
    memmove((*dst) + (*dst_len), option, option_len);

    *dst_len += option_len;
  } else {
    /* dst is empty: set to option */
    *dst = (char *)option;
    *dst_len = option_len;
  }
}
/*---------------------------------------------------------------------------*/
static int
coap_get_variable(const char *buffer, size_t length, const char *name,
                  const char **output)
{
  const char *start = NULL;
  const char *end = NULL;
  const char *value_end = NULL;
  size_t name_len = 0;

  /*initialize the output buffer first */
  *output = 0;

  name_len = strlen(name);
  end = buffer + length;

  for(start = buffer; start + name_len < end; ++start) {
    if((start == buffer || start[-1] == '&') && start[name_len] == '='
       && strncmp(name, start, name_len) == 0) {

      /* Point start to variable value */
      start += name_len + 1;

      /* Point end to the end of the value */
      value_end = (const char *)memchr(start, '&', end - start);
      if(value_end == NULL) {
        value_end = end;
      }
      *output = start;

      return value_end - start;
    }
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
/*- Internal API ------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
void
coap_init_connection(uint16_t port)
{
  /* new connection with remote host */
  udp_conn = udp_new(NULL, 0, NULL);
  udp_bind(udp_conn, port);
  PRINTF("Listening on port %u\n", uip_ntohs(udp_conn->lport));

  /* initialize transaction ID */
  current_mid = random_rand();

  /* increment boot counter */
  uint16_t boot_counter = coap_read_persistent_boot_counter(true);
  PRINTF("\b\b\n");
  boot_counter++;
  coap_write_persistent_boot_counter(boot_counter);
  PRINTF("\b\b\n");
}
/*---------------------------------------------------------------------------*/
uint16_t
coap_get_mid()
{
  return ++current_mid;
}
/*---------------------------------------------------------------------------*/
void
coap_init_message(void *packet, coap_message_type_t type, uint8_t code,
                  uint16_t mid)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  /* Important thing */
  memset(coap_pkt, 0, sizeof(coap_packet_t));

  coap_pkt->type = type;
  coap_pkt->code = code;
  coap_pkt->mid = mid;
}
/*---------------------------------------------------------------------------*/
size_t
coap_serialize_message(void *packet, uint8_t *buffer) {
  return coap_serialize_message_with_counter(packet, buffer, 0);
}
/*---------------------------------------------------------------------------*/
size_t
coap_serialize_message_with_counter(void *packet, uint8_t *buffer,
                                    uint8_t retransmission_counter)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;
  uint8_t *option;
  unsigned int current_number = 0;

  /* Initialize */
  coap_pkt->buffer = buffer;
  coap_pkt->version = 1;

  PRINTF("-Serializing MID %u to %p, ", coap_pkt->mid, coap_pkt->buffer);

  /* set header fields */
  coap_pkt->buffer[0] = 0x00;
  coap_pkt->buffer[0] |= COAP_HEADER_VERSION_MASK
    & (coap_pkt->version) << COAP_HEADER_VERSION_POSITION;
  coap_pkt->buffer[0] |= COAP_HEADER_TYPE_MASK
    & (coap_pkt->type) << COAP_HEADER_TYPE_POSITION;
  coap_pkt->buffer[0] |= COAP_HEADER_TOKEN_LEN_MASK
    & (coap_pkt->token_len) << COAP_HEADER_TOKEN_LEN_POSITION;
  coap_pkt->buffer[1] = coap_pkt->code;
  coap_pkt->buffer[2] = (uint8_t)((coap_pkt->mid) >> 8);
  coap_pkt->buffer[3] = (uint8_t)(coap_pkt->mid);

  /* set security headers */
  coap_enable_integrity_check_and_encrypt_payload(coap_pkt, retransmission_counter);

  /* empty packet, dont need to do more stuff */
  if(!coap_pkt->code) {
    PRINTF("-Done serializing empty message at %p-\n", coap_pkt->buffer);
    return 4;
  }

  /* set Token */
  PRINTF("Token (len %u)", coap_pkt->token_len);
  option = coap_pkt->buffer + COAP_HEADER_LEN;
  for(current_number = 0; current_number < coap_pkt->token_len;
      ++current_number) {
    PRINTF(" %02X", coap_pkt->token[current_number]);
    *option = coap_pkt->token[current_number];
    ++option;
  }
  PRINTF("-\n");

  /* Serialize options */
  current_number = 0;

  PRINTF("-Serializing options at %p-\n", option);

  /* The options must be serialized in the order of their number */
  COAP_SERIALIZE_BYTE_OPTION(COAP_OPTION_IF_MATCH, if_match, "If-Match");
  COAP_SERIALIZE_STRING_OPTION(COAP_OPTION_URI_HOST, uri_host, '\0',
                               "Uri-Host");
  COAP_SERIALIZE_BYTE_OPTION(COAP_OPTION_ETAG, etag, "ETag");
  COAP_SERIALIZE_INT_OPTION(COAP_OPTION_IF_NONE_MATCH,
                            content_format -
                            coap_pkt->
                            content_format /* hack to get a zero field */,
                            "If-None-Match");
  COAP_SERIALIZE_INT_OPTION(COAP_OPTION_OBSERVE, observe, "Observe");
  COAP_SERIALIZE_INT_OPTION(COAP_OPTION_URI_PORT, uri_port, "Uri-Port");
  COAP_SERIALIZE_STRING_OPTION(COAP_OPTION_LOCATION_PATH, location_path, '/',
                               "Location-Path");
  COAP_SERIALIZE_STRING_OPTION(COAP_OPTION_URI_PATH, uri_path, '/',
                               "Uri-Path");
  PRINTF("Serialize content format: %d\n", coap_pkt->content_format);
  COAP_SERIALIZE_INT_OPTION(COAP_OPTION_CONTENT_FORMAT, content_format,
                            "Content-Format");
  COAP_SERIALIZE_INT_OPTION(COAP_OPTION_MAX_AGE, max_age, "Max-Age");
  COAP_SERIALIZE_STRING_OPTION(COAP_OPTION_URI_QUERY, uri_query, '&',
                               "Uri-Query");
  COAP_SERIALIZE_INT_OPTION(COAP_OPTION_ACCEPT, accept, "Accept");
  COAP_SERIALIZE_STRING_OPTION(COAP_OPTION_LOCATION_QUERY, location_query,
                               '&', "Location-Query");
  COAP_SERIALIZE_BLOCK_OPTION(COAP_OPTION_BLOCK2, block2, "Block2");
  COAP_SERIALIZE_BLOCK_OPTION(COAP_OPTION_BLOCK1, block1, "Block1");
  COAP_SERIALIZE_INT_OPTION(COAP_OPTION_SIZE2, size2, "Size2");
  COAP_SERIALIZE_STRING_OPTION(COAP_OPTION_PROXY_URI, proxy_uri, '\0',
                               "Proxy-Uri");
  COAP_SERIALIZE_STRING_OPTION(COAP_OPTION_PROXY_SCHEME, proxy_scheme, '\0',
                               "Proxy-Scheme");
  COAP_SERIALIZE_INT_OPTION(COAP_OPTION_SIZE1, size1, "Size1");

  COAP_SERIALIZE_INT_OPTION(COAP_OPTION_CLIENT_IDENTITY, client_identity, "Client Identity");
  COAP_SERIALIZE_INT_OPTION(COAP_OPTION_BOOT_COUNTER, boot_counter, "Boot Counter");
  COAP_SERIALIZE_INT_OPTION(COAP_OPTION_RETRANSMISSION_COUNTER, retransmission_counter, "Retransmission Counter");
  COAP_SERIALIZE_STRING_OPTION(COAP_OPTION_HMAC, hmac, '\0', "HMAC");
  uint8_t *byte_after_hmac = option;
  COAP_SERIALIZE_INT_OPTION(COAP_OPTION_ENCR_ALG, encr_alg, "Encryption Algorithm");

  PRINTF("-Done serializing at %p----\n", option);

  /* Pack payload */
  if((option - coap_pkt->buffer) <= COAP_MAX_HEADER_SIZE) {
    /* Payload marker */
    if(coap_pkt->payload_len) {
      *option = 0xFF;
      ++option;
    }
    memmove(option, coap_pkt->payload, coap_pkt->payload_len);
  } else {
    /* an error occurred: caller must check for !=0 */
    coap_pkt->buffer = NULL;
    coap_error_message = "Serialized header exceeds COAP_MAX_HEADER_SIZE";
    return 0;
  }

  size_t packet_len = (option - buffer) + coap_pkt->payload_len;
  coap_update_hmac(coap_pkt, byte_after_hmac, packet_len);

  PRINTF("-Done %u B (header len %u, payload len %u)-\n",
         (unsigned int)(coap_pkt->payload_len + option - buffer),
         (unsigned int)(option - buffer),
         (unsigned int)coap_pkt->payload_len);

  PRINTF("Dump [0x%02X %02X %02X %02X  %02X %02X %02X %02X]\n",
         coap_pkt->buffer[0],
         coap_pkt->buffer[1],
         coap_pkt->buffer[2],
         coap_pkt->buffer[3],
         coap_pkt->buffer[4],
         coap_pkt->buffer[5], coap_pkt->buffer[6], coap_pkt->buffer[7]
         );

  return packet_len;
}
/*---------------------------------------------------------------------------*/
void
coap_send_message(uip_ipaddr_t *addr, uint16_t port, uint8_t *data,
                               uint16_t length) {
  coap_send_message_with_counter(addr, port, data, length, 0);
}
/*---------------------------------------------------------------------------*/
void
coap_send_message_with_counter(uip_ipaddr_t *addr, uint16_t port, uint8_t *data,
                  uint16_t length, uint8_t counter)
{
  /* Integrity check */
  if (counter != 0) {
    static coap_packet_t coap_pkt[1];
    coap_parse_message(coap_pkt, data, length);
    length = coap_serialize_message_with_counter(coap_pkt, coap_pkt->buffer, counter);
  }

  /* configure connection to reply to client */
  uip_ipaddr_copy(&udp_conn->ripaddr, addr);
  udp_conn->rport = port;

  uip_udp_packet_send(udp_conn, data, length);

  PRINTF("-sent UDP datagram (%u): ", length);
  for (uint8_t i = 0; i < length; ++i) {
    PRINTF("%02x ", data[i]);
  }
  PRINTF("-\n");

  /* restore server socket to allow data from any node */
  memset(&udp_conn->ripaddr, 0, sizeof(udp_conn->ripaddr));
  udp_conn->rport = 0;
}
/*---------------------------------------------------------------------------*/
coap_status_t
coap_parse_message(void *packet, uint8_t *data, uint16_t data_len)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  static uint8_t *original_data = NULL;

  void *new_ptr = realloc(original_data, data_len * sizeof(uint8_t));
  if (new_ptr != NULL) {
    original_data = (uint8_t *) new_ptr;
  } else { // realloc failed - probably out of memory
    free(original_data);
  }
  memcpy(original_data, data, data_len);

  /* initialize packet */
  memset(coap_pkt, 0, sizeof(coap_packet_t));
  PRINTF("-Parsing at: %p-------\n", (void*)&coap_pkt);

  /* pointer to packet bytes */
  coap_pkt->buffer = data;

  /* parse header fields */
  coap_pkt->version = (COAP_HEADER_VERSION_MASK & coap_pkt->buffer[0])
    >> COAP_HEADER_VERSION_POSITION;
  coap_pkt->type = (COAP_HEADER_TYPE_MASK & coap_pkt->buffer[0])
    >> COAP_HEADER_TYPE_POSITION;
  coap_pkt->token_len = (COAP_HEADER_TOKEN_LEN_MASK & coap_pkt->buffer[0])
    >> COAP_HEADER_TOKEN_LEN_POSITION;
  coap_pkt->code = coap_pkt->buffer[1];
  coap_pkt->mid = coap_pkt->buffer[2] << 8 | coap_pkt->buffer[3];

  if(coap_pkt->version != 1) {
    coap_error_message = "CoAP version must be 1";
    return BAD_REQUEST_4_00;
  }

  if(coap_pkt->token_len > COAP_TOKEN_LEN) {
    coap_error_message = "Token Length must not be more than 8";
    return BAD_REQUEST_4_00;
  }

  uint8_t *current_option = data + COAP_HEADER_LEN;

  memcpy(coap_pkt->token, current_option, coap_pkt->token_len);
  PRINTF("Token (len %u) [0x%02X%02X%02X%02X%02X%02X%02X%02X]\n",
         coap_pkt->token_len, coap_pkt->token[0], coap_pkt->token[1],
         coap_pkt->token[2], coap_pkt->token[3], coap_pkt->token[4],
         coap_pkt->token[5], coap_pkt->token[6], coap_pkt->token[7]
         );                     /*FIXME always prints 8 bytes */

  /* parse options */
  memset(coap_pkt->options, 0, sizeof(coap_pkt->options));
  current_option += coap_pkt->token_len;

  unsigned int option_number = 0;
  unsigned int option_delta = 0;
  size_t option_length = 0;

  uint32_t hmac_position = 0;

  while(current_option < data + data_len) {
    /* payload marker 0xFF, currently only checking for 0xF* because rest is reserved */
    if((current_option[0] & 0xF0) == 0xF0) {
      coap_pkt->payload = ++current_option;
      coap_pkt->payload_len = data_len - (coap_pkt->payload - data);

      /* also for receiving, the Erbium upper bound is REST_MAX_CHUNK_SIZE */
      if(coap_pkt->payload_len > REST_MAX_CHUNK_SIZE) {
        coap_pkt->payload_len = REST_MAX_CHUNK_SIZE;
        /* null-terminate payload */
      }
      coap_pkt->payload[coap_pkt->payload_len] = '\0';

      break;
    }

    option_delta = current_option[0] >> 4;
    option_length = current_option[0] & 0x0F;
    ++current_option;

    if(option_delta == 13) {
      option_delta += current_option[0];
      ++current_option;
    } else if(option_delta == 14) {
      option_delta += 255;
      option_delta += current_option[0] << 8;
      ++current_option;
      option_delta += current_option[0];
      ++current_option;
    }

    if(option_length == 13) {
      option_length += current_option[0];
      ++current_option;
    } else if(option_length == 14) {
      option_length += 255;
      option_length += current_option[0] << 8;
      ++current_option;
      option_length += current_option[0];
      ++current_option;
    }

    if(current_option + option_length > data + data_len) {
      /* Malformed CoAP - out of bounds */
      PRINTF("BAD REQUEST: options outside data packet: %u > %u\n",
             (unsigned)(current_option + option_length - data), data_len);
      return BAD_REQUEST_4_00;
    }

    option_number += option_delta;

    if(option_number < COAP_OPTION_EXPERIMENTAL && option_number > COAP_OPTION_SIZE1) {
      /* Malformed CoAP - out of bounds */
      PRINTF("BAD REQUEST: option number too large: %u\n", option_number);
      return BAD_REQUEST_4_00;
    }

    PRINTF("OPTION %u (delta %u, len %zu): ", option_number, option_delta,
           option_length);

    if (option_number < COAP_OPTION_EXPERIMENTAL)
      SET_OPTION(coap_pkt, option_number);

    switch(option_number) {
    case COAP_OPTION_CONTENT_FORMAT:
      coap_pkt->content_format = coap_parse_int_option(current_option,
                                                       option_length);
      PRINTF("Content-Format [%u]\n", coap_pkt->content_format);
      break;
    case COAP_OPTION_MAX_AGE:
      coap_pkt->max_age = coap_parse_int_option(current_option,
                                                option_length);
      PRINTF("Max-Age [%lu]\n", (unsigned long)coap_pkt->max_age);
      break;
    case COAP_OPTION_ETAG:
      coap_pkt->etag_len = MIN(COAP_ETAG_LEN, option_length);
      memcpy(coap_pkt->etag, current_option, coap_pkt->etag_len);
      PRINTF("ETag %u [0x%02X%02X%02X%02X%02X%02X%02X%02X]\n",
             coap_pkt->etag_len, coap_pkt->etag[0], coap_pkt->etag[1],
             coap_pkt->etag[2], coap_pkt->etag[3], coap_pkt->etag[4],
             coap_pkt->etag[5], coap_pkt->etag[6], coap_pkt->etag[7]
             );                 /*FIXME always prints 8 bytes */
      break;
    case COAP_OPTION_ACCEPT:
      coap_pkt->accept = coap_parse_int_option(current_option, option_length);
      PRINTF("Accept [%u]\n", coap_pkt->accept);
      break;
    case COAP_OPTION_IF_MATCH:
      /* TODO support multiple ETags */
      coap_pkt->if_match_len = MIN(COAP_ETAG_LEN, option_length);
      memcpy(coap_pkt->if_match, current_option, coap_pkt->if_match_len);
      PRINTF("If-Match %u [0x%02X%02X%02X%02X%02X%02X%02X%02X]\n",
             coap_pkt->if_match_len, coap_pkt->if_match[0],
             coap_pkt->if_match[1], coap_pkt->if_match[2],
             coap_pkt->if_match[3], coap_pkt->if_match[4],
             coap_pkt->if_match[5], coap_pkt->if_match[6],
             coap_pkt->if_match[7]
             ); /* FIXME always prints 8 bytes */
      break;
    case COAP_OPTION_IF_NONE_MATCH:
      coap_pkt->if_none_match = 1;
      PRINTF("If-None-Match\n");
      break;

    case COAP_OPTION_PROXY_URI:
#if COAP_PROXY_OPTION_PROCESSING
      coap_pkt->proxy_uri = (char *)current_option;
      coap_pkt->proxy_uri_len = option_length;
#endif
      PRINTF("Proxy-Uri NOT IMPLEMENTED [%.*s]\n", (int)coap_pkt->proxy_uri_len,
             coap_pkt->proxy_uri);
      coap_error_message = "This is a constrained server (Contiki)";
      return PROXYING_NOT_SUPPORTED_5_05;
      break;
    case COAP_OPTION_PROXY_SCHEME:
#if COAP_PROXY_OPTION_PROCESSING
      coap_pkt->proxy_scheme = (char *)current_option;
      coap_pkt->proxy_scheme_len = option_length;
#endif
      PRINTF("Proxy-Scheme NOT IMPLEMENTED [%.*s]\n",
             (int)coap_pkt->proxy_scheme_len, coap_pkt->proxy_scheme);
      coap_error_message = "This is a constrained server (Contiki)";
      return PROXYING_NOT_SUPPORTED_5_05;
      break;

    case COAP_OPTION_URI_HOST:
      coap_pkt->uri_host = (char *)current_option;
      coap_pkt->uri_host_len = option_length;
      PRINTF("Uri-Host [%.*s]\n", (int)coap_pkt->uri_host_len,
             coap_pkt->uri_host);
      break;
    case COAP_OPTION_URI_PORT:
      coap_pkt->uri_port = coap_parse_int_option(current_option,
                                                 option_length);
      PRINTF("Uri-Port [%u]\n", coap_pkt->uri_port);
      break;
    case COAP_OPTION_URI_PATH:
      /* coap_merge_multi_option() operates in-place on the IPBUF, but final packet field should be const string -> cast to string */
      coap_merge_multi_option((char **)&(coap_pkt->uri_path),
                              &(coap_pkt->uri_path_len), current_option,
                              option_length, '/');
      PRINTF("Uri-Path [%.*s]\n", (int)coap_pkt->uri_path_len, coap_pkt->uri_path);
      break;
    case COAP_OPTION_URI_QUERY:
      /* coap_merge_multi_option() operates in-place on the IPBUF, but final packet field should be const string -> cast to string */
      coap_merge_multi_option((char **)&(coap_pkt->uri_query),
                              &(coap_pkt->uri_query_len), current_option,
                              option_length, '&');
      PRINTF("Uri-Query [%.*s]\n", (int)coap_pkt->uri_query_len,
             coap_pkt->uri_query);
      break;

    case COAP_OPTION_LOCATION_PATH:
      /* coap_merge_multi_option() operates in-place on the IPBUF, but final packet field should be const string -> cast to string */
      coap_merge_multi_option((char **)&(coap_pkt->location_path),
                              &(coap_pkt->location_path_len), current_option,
                              option_length, '/');
      PRINTF("Location-Path [%.*s]\n", (int)coap_pkt->location_path_len,
             coap_pkt->location_path);
      break;
    case COAP_OPTION_LOCATION_QUERY:
      /* coap_merge_multi_option() operates in-place on the IPBUF, but final packet field should be const string -> cast to string */
      coap_merge_multi_option((char **)&(coap_pkt->location_query),
                              &(coap_pkt->location_query_len), current_option,
                              option_length, '&');
      PRINTF("Location-Query [%.*s]\n", (int)coap_pkt->location_query_len,
             coap_pkt->location_query);
      break;

    case COAP_OPTION_OBSERVE:
      coap_pkt->observe = coap_parse_int_option(current_option,
                                                option_length);
      PRINTF("Observe [%lu]\n", (unsigned long)coap_pkt->observe);
      break;
    case COAP_OPTION_BLOCK2:
      coap_pkt->block2_num = coap_parse_int_option(current_option,
                                                   option_length);
      coap_pkt->block2_more = (coap_pkt->block2_num & 0x08) >> 3;
      coap_pkt->block2_size = 16 << (coap_pkt->block2_num & 0x07);
#if COAP_ENABLE_ENCRYPTION_SUPPORT == 1
        /* The encryption always adds padding, so that we need at least one byte for that. */
        coap_pkt->block2_size--;
#endif
      coap_pkt->block2_num >>= 4;
      coap_pkt->block2_offset = coap_pkt->block2_num * coap_pkt->block2_size;
      PRINTF("Block2 [%lu%s (%u B/blk)]\n",
             (unsigned long)coap_pkt->block2_num,
             coap_pkt->block2_more ? "+" : "", coap_pkt->block2_size);
      break;
    case COAP_OPTION_BLOCK1:
      coap_pkt->block1_num = coap_parse_int_option(current_option,
                                                   option_length);
      coap_pkt->block1_more = (coap_pkt->block1_num & 0x08) >> 3;
      coap_pkt->block1_size = 16 << (coap_pkt->block1_num & 0x07);
      coap_pkt->block1_offset = (coap_pkt->block1_num & ~0x0000000F)
        << (coap_pkt->block1_num & 0x07);
      coap_pkt->block1_num >>= 4;
      PRINTF("Block1 [%lu%s (%u B/blk)]\n",
             (unsigned long)coap_pkt->block1_num,
             coap_pkt->block1_more ? "+" : "", coap_pkt->block1_size);
      break;
    case COAP_OPTION_SIZE2:
      coap_pkt->size2 = coap_parse_int_option(current_option, option_length);
      PRINTF("Size2 [%lu]\n", (unsigned long)coap_pkt->size2);
      break;
    case COAP_OPTION_SIZE1:
      coap_pkt->size1 = coap_parse_int_option(current_option, option_length);
      PRINTF("Size1 [%lu]\n", (unsigned long)coap_pkt->size1);
      break;
    case COAP_OPTION_CLIENT_IDENTITY:
      coap_pkt->client_identity = (uint8_t) coap_parse_int_option(current_option, option_length);
      PRINTF("Client Identity [%u]\n", (uint8_t)coap_pkt->client_identity);
      break;
    case COAP_OPTION_BOOT_COUNTER:
      coap_pkt->boot_counter = (uint16_t) coap_parse_int_option(current_option, option_length);
      PRINTF("Boot Counter [%u]\n", (uint16_t)coap_pkt->boot_counter);
      break;
    case COAP_OPTION_RETRANSMISSION_COUNTER:
      coap_pkt->retransmission_counter = (uint8_t) coap_parse_int_option(current_option, option_length);
      PRINTF("Retransmission Counter [%u]\n", (uint8_t)coap_pkt->retransmission_counter);
      break;
    case COAP_OPTION_HMAC:
      /* coap_merge_multi_option() operates in-place on the IPBUF, but final packet field should be const string -> cast to string */
      coap_merge_multi_option((char **)&(coap_pkt->hmac),
                              &(coap_pkt->hmac_len), current_option,
                              option_length, '\0');
      hmac_position = current_option - data;
      PRINTF("HMAC [");
      for (uint8_t i = 0; i < coap_pkt->hmac_len; ++i){
        PRINTF("%02x ", coap_pkt->hmac[i]);
      }
      PRINTF("\b]\n");
      break;
    case COAP_OPTION_ENCR_ALG:
      coap_pkt->encr_alg = (uint8_t) coap_parse_int_option(current_option, option_length);
      PRINTF("Encryption Algorithm [%u]\n", (uint8_t)coap_pkt->encr_alg);
      break;
    default:
      PRINTF("unknown (%u)\n", option_number);
      /* check if critical (odd) */
      if(option_number & 1) {
        coap_error_message = "Unsupported critical option";
        return BAD_OPTION_4_02;
      }
    }

    current_option += option_length;
  }                             /* for */
  PRINTF("-Done parsing-------\n");



  bool hmac_valid = false;
  bool malware_free = false;
  bool packet_was_encrypted = false;

  hmac_valid = coap_is_valid_hmac(original_data, hmac_position, data_len);

  packet_was_encrypted = !(coap_pkt->encr_alg != 0x01 && coap_pkt->payload_len > 0);

  if (packet_was_encrypted) {
    coap_decrypt_payload(coap_pkt);
  }

  malware_free = coap_is_malware_free(coap_pkt);

  PRINTF("-Done verification: ");

  if (hmac_valid && malware_free && packet_was_encrypted) {
    PRINTF("NO_ERROR-------\n");
    return NO_ERROR;
  } else if (hmac_valid && malware_free && !packet_was_encrypted) {
    PRINTF("UNENCRYPTED-------\n");
    return UNENCRYPTED;
  } else if (hmac_valid && !malware_free && packet_was_encrypted) {
    PRINTF("ENCRYPTED_MALWARE-------\n");
    return ENCRYPTED_MALWARE;
  } else if (hmac_valid && !malware_free && !packet_was_encrypted) {
    PRINTF("UNENCRYPTED_MALWARE-------\n");
    return UNENCRYPTED_MALWARE;
  } else if (!hmac_valid && malware_free && packet_was_encrypted) {
    PRINTF("ENCRYPTED_HMAC_INVALID-------\n");
    return ENCRYPTED_HMAC_INVALID;
  } else if (!hmac_valid && malware_free && !packet_was_encrypted) {
    PRINTF("UNENCRYPTED_HMAC_INVALID-------\n");
    return UNENCRYPTED_HMAC_INVALID;
  } else if (!hmac_valid && !malware_free && packet_was_encrypted) {
    PRINTF("ENCRYPTED_MALWARE_WITH_HMAC_INVALID-------\n");
    return ENCRYPTED_MALWARE_WITH_HMAC_INVALID;
  } else if (!hmac_valid && !malware_free && !packet_was_encrypted) {
    PRINTF("UNENCRYPTED_MALWARE_WITH_HMAC_INVALID-------\n");
    return UNENCRYPTED_MALWARE_WITH_HMAC_INVALID;
  }

  // Should never reach execution here because one of the above statements should always be true.
  return INTERNAL_SERVER_ERROR_5_00;
}
/*---------------------------------------------------------------------------*/
/*- REST Engine API ---------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
int
coap_get_query_variable(void *packet, const char *name, const char **output)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(IS_OPTION(coap_pkt, COAP_OPTION_URI_QUERY)) {
    return coap_get_variable(coap_pkt->uri_query, coap_pkt->uri_query_len,
                             name, output);
  }
  return 0;
}
int
coap_get_post_variable(void *packet, const char *name, const char **output)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(coap_pkt->payload_len) {
    return coap_get_variable((const char *)coap_pkt->payload,
                             coap_pkt->payload_len, name, output);
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
int
coap_set_status_code(void *packet, unsigned int code)
{
  if(code <= 0xFF) {
    ((coap_packet_t *)packet)->code = (uint8_t)code;
    return 1;
  } else {
    return 0;
  }
}
/*---------------------------------------------------------------------------*/
int
coap_set_token(void *packet, const uint8_t *token, size_t token_len)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->token_len = MIN(COAP_TOKEN_LEN, token_len);
  memcpy(coap_pkt->token, token, coap_pkt->token_len);

  return coap_pkt->token_len;
}
/*---------------------------------------------------------------------------*/
/*- CoAP REST Implementation API --------------------------------------------*/
/*---------------------------------------------------------------------------*/
int
coap_get_header_content_format(void *packet, unsigned int *format)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_CONTENT_FORMAT)) {
    return 0;
  }
  *format = coap_pkt->content_format;
  return 1;
}
int
coap_set_header_content_format(void *packet, unsigned int format)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->content_format = format;
  SET_OPTION(coap_pkt, COAP_OPTION_CONTENT_FORMAT);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_accept(void *packet, unsigned int *accept)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_ACCEPT)) {
    return 0;
  }
  *accept = coap_pkt->accept;
  return 1;
}
int
coap_set_header_accept(void *packet, unsigned int accept)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->accept = accept;
  SET_OPTION(coap_pkt, COAP_OPTION_ACCEPT);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_max_age(void *packet, uint32_t *age)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_MAX_AGE)) {
    *age = COAP_DEFAULT_MAX_AGE;
  } else {
    *age = coap_pkt->max_age;
  } return 1;
}
int
coap_set_header_max_age(void *packet, uint32_t age)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->max_age = age;
  SET_OPTION(coap_pkt, COAP_OPTION_MAX_AGE);
  return 1;
}

/*---------------------------------------------------------------------------*/
int
coap_get_header_etag(void *packet, const uint8_t **etag)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_ETAG)) {
    return 0;
  }
  *etag = coap_pkt->etag;
  return coap_pkt->etag_len;
}
int
coap_set_header_etag(void *packet, const uint8_t *etag, size_t etag_len)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->etag_len = MIN(COAP_ETAG_LEN, etag_len);
  memcpy(coap_pkt->etag, etag, coap_pkt->etag_len);

  SET_OPTION(coap_pkt, COAP_OPTION_ETAG);
  return coap_pkt->etag_len;
}
/*---------------------------------------------------------------------------*/
/*FIXME support multiple ETags */
int
coap_get_header_if_match(void *packet, const uint8_t **etag)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_IF_MATCH)) {
    return 0;
  }
  *etag = coap_pkt->if_match;
  return coap_pkt->if_match_len;
}
int
coap_set_header_if_match(void *packet, const uint8_t *etag, size_t etag_len)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->if_match_len = MIN(COAP_ETAG_LEN, etag_len);
  memcpy(coap_pkt->if_match, etag, coap_pkt->if_match_len);

  SET_OPTION(coap_pkt, COAP_OPTION_IF_MATCH);
  return coap_pkt->if_match_len;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_if_none_match(void *packet)
{
  return IS_OPTION((coap_packet_t *)packet,
                   COAP_OPTION_IF_NONE_MATCH) ? 1 : 0;
}
int
coap_set_header_if_none_match(void *packet)
{
  SET_OPTION((coap_packet_t *)packet, COAP_OPTION_IF_NONE_MATCH);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_proxy_uri(void *packet, const char **uri)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_PROXY_URI)) {
    return 0;
  }
  *uri = coap_pkt->proxy_uri;
  return coap_pkt->proxy_uri_len;
}
int
coap_set_header_proxy_uri(void *packet, const char *uri)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  /*TODO Provide alternative that sets Proxy-Scheme and Uri-* options and provide er-coap-conf define */

  coap_pkt->proxy_uri = uri;
  coap_pkt->proxy_uri_len = strlen(uri);

  SET_OPTION(coap_pkt, COAP_OPTION_PROXY_URI);
  return coap_pkt->proxy_uri_len;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_uri_host(void *packet, const char **host)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_URI_HOST)) {
    return 0;
  }
  *host = coap_pkt->uri_host;
  return coap_pkt->uri_host_len;
}
int
coap_set_header_uri_host(void *packet, const char *host)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->uri_host = host;
  coap_pkt->uri_host_len = strlen(host);

  SET_OPTION(coap_pkt, COAP_OPTION_URI_HOST);
  return coap_pkt->uri_host_len;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_uri_path(void *packet, const char **path)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_URI_PATH)) {
    return 0;
  }
  *path = coap_pkt->uri_path;
  return coap_pkt->uri_path_len;
}
int
coap_set_header_uri_path(void *packet, const char *path)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  while(path[0] == '/')
    ++path;

  coap_pkt->uri_path = path;
  coap_pkt->uri_path_len = strlen(path);

  SET_OPTION(coap_pkt, COAP_OPTION_URI_PATH);
  return coap_pkt->uri_path_len;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_uri_query(void *packet, const char **query)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_URI_QUERY)) {
    return 0;
  }
  *query = coap_pkt->uri_query;
  return coap_pkt->uri_query_len;
}
int
coap_set_header_uri_query(void *packet, const char *query)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  while(query[0] == '?')
    ++query;

  coap_pkt->uri_query = query;
  coap_pkt->uri_query_len = strlen(query);

  SET_OPTION(coap_pkt, COAP_OPTION_URI_QUERY);
  return coap_pkt->uri_query_len;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_location_path(void *packet, const char **path)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_LOCATION_PATH)) {
    return 0;
  }
  *path = coap_pkt->location_path;
  return coap_pkt->location_path_len;
}
int
coap_set_header_location_path(void *packet, const char *path)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  char *query;

  while(path[0] == '/')
    ++path;

  if((query = strchr(path, '?'))) {
    coap_set_header_location_query(packet, query + 1);
    coap_pkt->location_path_len = query - path;
  } else {
    coap_pkt->location_path_len = strlen(path);
  } coap_pkt->location_path = path;

  if(coap_pkt->location_path_len > 0) {
    SET_OPTION(coap_pkt, COAP_OPTION_LOCATION_PATH);
  }
  return coap_pkt->location_path_len;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_location_query(void *packet, const char **query)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_LOCATION_QUERY)) {
    return 0;
  }
  *query = coap_pkt->location_query;
  return coap_pkt->location_query_len;
}
int
coap_set_header_location_query(void *packet, const char *query)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  while(query[0] == '?')
    ++query;

  coap_pkt->location_query = query;
  coap_pkt->location_query_len = strlen(query);

  SET_OPTION(coap_pkt, COAP_OPTION_LOCATION_QUERY);
  return coap_pkt->location_query_len;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_observe(void *packet, uint32_t *observe)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_OBSERVE)) {
    return 0;
  }
  *observe = coap_pkt->observe;
  return 1;
}
int
coap_set_header_observe(void *packet, uint32_t observe)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->observe = observe;
  SET_OPTION(coap_pkt, COAP_OPTION_OBSERVE);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_block2(void *packet, uint32_t *num, uint8_t *more,
                       uint16_t *size, uint32_t *offset)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_BLOCK2)) {
    return 0;
  }
  /* pointers may be NULL to get only specific block parameters */
  if(num != NULL) {
    *num = coap_pkt->block2_num;
  }
  if(more != NULL) {
    *more = coap_pkt->block2_more;
  }
  if(size != NULL) {
    *size = coap_pkt->block2_size;
  }
  if(offset != NULL) {
    *offset = coap_pkt->block2_offset;
  }
  return 1;
}
int
coap_set_header_block2(void *packet, uint32_t num, uint8_t more,
                       uint16_t size)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

#if COAP_ENABLE_ENCRYPTION_SUPPORT == 1
  /* Decrease by one due to added space for padding */
  if(size < 15) {
    return 0;
  }
#else
  if(size < 16) {
    return 0;
  }
#endif
  if(size > 2048) {
    return 0;
  }
  if(num > 0x0FFFFF) {
    return 0;
  }
  coap_pkt->block2_num = num;
  coap_pkt->block2_more = more ? 1 : 0;
  coap_pkt->block2_size = size;

  SET_OPTION(coap_pkt, COAP_OPTION_BLOCK2);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_block1(void *packet, uint32_t *num, uint8_t *more,
                       uint16_t *size, uint32_t *offset)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_BLOCK1)) {
    return 0;
  }
  /* pointers may be NULL to get only specific block parameters */
  if(num != NULL) {
    *num = coap_pkt->block1_num;
  }
  if(more != NULL) {
    *more = coap_pkt->block1_more;
  }
  if(size != NULL) {
    *size = coap_pkt->block1_size;
  }
  if(offset != NULL) {
    *offset = coap_pkt->block1_offset;
  }
  return 1;
}
int
coap_set_header_block1(void *packet, uint32_t num, uint8_t more,
                       uint16_t size)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(size < 16) {
    return 0;
  }
  if(size > 2048) {
    return 0;
  }
  if(num > 0x0FFFFF) {
    return 0;
  }
  coap_pkt->block1_num = num;
  coap_pkt->block1_more = more;
  coap_pkt->block1_size = size;

  SET_OPTION(coap_pkt, COAP_OPTION_BLOCK1);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_size2(void *packet, uint32_t *size)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_SIZE2)) {
    return 0;
  }
  *size = coap_pkt->size2;
  return 1;
}
int
coap_set_header_size2(void *packet, uint32_t size)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->size2 = size;
  SET_OPTION(coap_pkt, COAP_OPTION_SIZE2);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_get_header_size1(void *packet, uint32_t *size)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(!IS_OPTION(coap_pkt, COAP_OPTION_SIZE1)) {
    return 0;
  }
  *size = coap_pkt->size1;
  return 1;
}
int
coap_set_header_size1(void *packet, uint32_t size)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->size1 = size;
  SET_OPTION(coap_pkt, COAP_OPTION_SIZE1);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_get_payload(void *packet, const uint8_t **payload)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  if(coap_pkt->payload) {
    *payload = coap_pkt->payload;
    return coap_pkt->payload_len;
  } else {
    *payload = NULL;
    return 0;
  }
}
int
coap_set_payload(void *packet, const void *payload, size_t length)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->payload = (uint8_t *)payload;
  coap_pkt->payload_len = MIN(REST_MAX_CHUNK_SIZE, length);

  return coap_pkt->payload_len;
}
/*---------------------------------------------------------------------------*/
int
coap_set_header_client_identity(void *packet, uint8_t value)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->client_identity = value;
  // Don't set option in map because this would exceed the FSRAM size
  // SET_OPTION(coap_pkt, COAP_OPTION_CLIENT_IDENTIY);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_set_header_boot_counter(void *packet, uint16_t value)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->boot_counter = value;
  // Don't set option in map because this would exceed the FSRAM size
  // SET_OPTION(coap_pkt, COAP_OPTION_BOOT_COUNTER);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_set_header_retransmission_counter(void *packet, uint8_t value)
{
  coap_packet_t *const coap_pkt = (coap_packet_t *)packet;

  coap_pkt->retransmission_counter = (uint8_t) (value + 1);
  // Don't set option in map because this would exceed the FSRAM size
  // SET_OPTION(coap_pkt, COAP_OPTION_RETRANSMISSION_COUNTER);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_calculate_hmac(uint8_t *hmac, uint8_t *data, size_t data_len)
{
  uint8_t *psk = presharedkeys[COAP_DEFAULT_CLIENT_IDENTITY];
  uint8_t psk_len = presharedkeys_len[COAP_DEFAULT_CLIENT_IDENTITY];

  PRINTF("Input data for HMAC: ");
  for (uint8_t i = 0; i < data_len; ++i){
    PRINTF("%02x ", data[i]);
  }
  PRINTF("\b\n");

  // Enable crypto processor
  if (!CRYPTO_IS_ENABLED())
    crypto_init();
  static uint8_t error_code = CRYPTO_SUCCESS;
  static sha256_state_t context;

  /*
   * HMAC implementation according to RFC 2104
   */
  uint8_t k_ipad[65];    // inner padding - key XORd with ipad
  uint8_t k_opad[65];    // outer padding - key XORd with opad
  uint8_t tk[32];

  // if psk is longer than 64 bytes reset it to psk=SHA256(psk)
  if (psk_len > 64) {
    static sha256_state_t tctx;

    error_code |= sha256_init(&tctx);
    error_code |= sha256_process(&tctx, psk, psk_len);
    error_code |= sha256_done(&tctx, tk);

    memmove(psk, tk, sizeof(tk));
    psk_len = 32;
  }

  /*
   * the HMAC_SHA256 transform looks like:
   *
   * SHA256(K XOR opad, SHA256(K XOR ipad, text))
   *
   * where K is an n byte psk
   * ipad is the byte 0x36 repeated 64 times
   * opad is the byte 0x5c repeated 64 times
   * and text is the data being protected
   */

  // start out by storing key in pads
  bzero(k_ipad, sizeof k_ipad);
  bzero(k_opad, sizeof k_opad);
  bcopy(psk, k_ipad, psk_len);
  bcopy(psk, k_opad, psk_len);

  // XOR psk with ipad and opad values
  for (uint8_t i = 0; i < 64; ++i) {
    k_ipad[i] ^= 0x36;
    k_opad[i] ^= 0x5c;
  }

  // perform inner SHA256
  error_code |= sha256_init(&context);                          // init context for 1st pass
  error_code |= sha256_process(&context, k_ipad, 64);           // start with inner pad
  error_code |= sha256_process(&context, data, (uint32_t) data_len);       // then text of datagram
  error_code |= sha256_done(&context, hmac);                    // finish up 1st pass

  // perform outer SHA256
  error_code |= sha256_init(&context);                          // init context for 2nd pass
  error_code |= sha256_process(&context, k_opad, 64);           // start with outer pad
  error_code |= sha256_process(&context, hmac, 32);             // then results of 1st hash
  error_code |= sha256_done(&context, hmac);                    // finish up 2nd pass

  if (error_code != CRYPTO_SUCCESS) {
    PRINTF("HMAC calculation failed!");
    return 0;
  }

  PRINTF("Calculated HMAC: ");
  for (uint8_t i = 0; i < 32; ++i){
    PRINTF("%02x ", hmac[i]);
  }
  PRINTF("\b\n");

  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_set_header_hmac(void *packet, const char *hmac, size_t hmac_length) {
  coap_packet_t *const coap_pkt = (coap_packet_t *) packet;
  coap_pkt->hmac = hmac;
  coap_pkt->hmac_len = hmac_length;

  // Don't set option in map because this would exceed the FSRAM size
  // SET_OPTION(coap_pkt, COAP_OPTION_HMAC);
  return 1;
}
/*---------------------------------------------------------------------------*/
uint8_t
coap_calculate_padding_len(void *packet) {
  coap_packet_t *const coap_pkt = (coap_packet_t *) packet;
  return (uint8_t) (16 - (coap_pkt->payload_len % 16));
}
/*---------------------------------------------------------------------------*/
int
coap_calculate_encrypted_payload(void *packet, char *encrypted_payload,
                                 uint16_t encrypted_payload_len, uint8_t padding_len) {
  coap_packet_t *const coap_pkt = (coap_packet_t *) packet;

  uint8_t *psk = presharedkeys[COAP_DEFAULT_CLIENT_IDENTITY];
  uint8_t cipher_block[encrypted_payload_len];

  memcpy(cipher_block, coap_pkt->payload, coap_pkt->payload_len);
  memset(cipher_block + coap_pkt->payload_len, padding_len, padding_len);

  PRINTF("plain input data for AES: ");
  for (uint8_t i = 0; i < sizeof(cipher_block); ++i){
    PRINTF("%02x ", cipher_block[i]);
  }
  PRINTF("\b, ");

  AES_128_GET_LOCK();
  AES_128.set_key(psk);
  for (uint16_t i = 0; i < encrypted_payload_len; i += 16) {
    AES_128.encrypt(&cipher_block[i]);
  }
  AES_128_RELEASE_LOCK();

  memcpy(encrypted_payload, cipher_block, encrypted_payload_len);

  return 1;
}
/*---------------------------------------------------------------------------*/
/**
 * @return the decrypted payload length (always smaller than the encrpted payload length due to padding).
 *         In case of an error, -1 is returned.
 */
int32_t
coap_calculate_decrypted_payload(void *packet, char *decrypted_payload) {
  coap_packet_t *const coap_pkt = (coap_packet_t *) packet;

  if ((coap_pkt->payload_len % 16) != 0) {
    return -1;
  }

  uint8_t *psk = presharedkeys[COAP_DEFAULT_CLIENT_IDENTITY];
  uint8_t cipher_block[coap_pkt->payload_len];
  memcpy(cipher_block, coap_pkt->payload, coap_pkt->payload_len);

  PRINTF("encrypted input data for AES: ");
  for (uint8_t i = 0; i < sizeof(cipher_block); ++i){
    PRINTF("%02x ", cipher_block[i]);
  }
  PRINTF("\b\n");

  AES_128_GET_LOCK();
  AES_128.set_key(psk);
  for (uint16_t i = 0; i < coap_pkt->payload_len; i += 16) {
    AES_128.decrypt(&cipher_block[i]);
  }
  AES_128_RELEASE_LOCK();

  uint8_t padding_len = cipher_block[sizeof(cipher_block) - 1];

  // Check for correct padding
  bool decryption_padding_error = false;
  for (uint8_t i = padding_len; i > 0; --i) {
    if (cipher_block[sizeof(cipher_block) - i] != padding_len) {
      decryption_padding_error |= true;
    }
  }

  if (decryption_padding_error) {
    return -1;
  }

  uint16_t decrypted_payload_len = sizeof(cipher_block) - padding_len;
  memcpy(decrypted_payload, cipher_block, decrypted_payload_len);

  return decrypted_payload_len;
}
/*---------------------------------------------------------------------------*/
int
coap_set_header_encr_alg(void *packet, uint8_t value) {
  coap_packet_t *const coap_pkt = (coap_packet_t *) packet;
  coap_pkt->encr_alg = value;

  // Don't set option in map because this would exceed the FSRAM size
  // SET_OPTION(coap_pkt, COAP_OPTION_HMAC);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_update_hmac(void *packet, uint8_t* byte_after_hmac, size_t packet_len) {
  coap_packet_t *const coap_pkt = (coap_packet_t *) packet;

  if (coap_pkt->hmac_len == 0) {
    // No HMAC found to update
    return 1;
  }

  uint8_t* hmac_position = byte_after_hmac - COAP_HEADER_HMAC_LENGTH;

  size_t hex_all_len = packet_len - COAP_HEADER_HMAC_LENGTH;
  uint8_t hex_all[hex_all_len];
  size_t packet_len_before_hmac_value = hmac_position - coap_pkt->buffer;
  size_t packet_len_after_hmac_value = packet_len - (packet_len_before_hmac_value + COAP_HEADER_HMAC_LENGTH);
  memcpy(hex_all, coap_pkt->buffer, packet_len_before_hmac_value);
  memcpy(hex_all + packet_len_before_hmac_value, byte_after_hmac, packet_len_after_hmac_value);

  uint8_t full_hmac[32];
  coap_calculate_hmac(full_hmac, hex_all, hex_all_len);
  memcpy(hmac_position, full_hmac, COAP_HEADER_HMAC_LENGTH);
  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_enable_integrity_check(void *packet, uint8_t retransmission_counter) {
#if COAP_ENABLE_HMAC_SUPPORT == 1
  coap_set_header_client_identity(packet, COAP_DEFAULT_CLIENT_IDENTITY);
  coap_set_header_boot_counter(packet, coap_read_persistent_boot_counter(false));
  coap_set_header_retransmission_counter(packet, retransmission_counter);

  // Set a dummy value to reserve space for later update
  static uint8_t hmac[COAP_HEADER_HMAC_LENGTH];
  coap_set_header_hmac(packet, (const char *) hmac, sizeof(hmac));
#endif

  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_encrypt_payload(void *packet) {
#if COAP_ENABLE_ENCRYPTION_SUPPORT == 1
  coap_packet_t *const coap_pkt = (coap_packet_t *) packet;

  if (coap_pkt->payload_len == 0) {
    return 1;
  }

  uint8_t padding_len = coap_calculate_padding_len(coap_pkt);
  uint16_t encrypted_payload_len = (uint16_t) (coap_pkt->payload_len + padding_len);
  static uint8_t *encrypted_payload = NULL;

  void *new_ptr = realloc(encrypted_payload, encrypted_payload_len * sizeof(uint8_t));
  if (new_ptr != NULL) {
    encrypted_payload = (uint8_t *) new_ptr;
  } else { // realloc failed - probably out of memory
    free(encrypted_payload);
    return 0;
  }

  coap_calculate_encrypted_payload(packet, (char *) encrypted_payload, encrypted_payload_len, padding_len);
  coap_set_header_client_identity(packet, COAP_DEFAULT_CLIENT_IDENTITY);
  coap_set_header_encr_alg(packet, 0x01);

  uint32_t num = 0;
  uint8_t more = 0;
  uint16_t size = 0;

  coap_get_header_block2(coap_pkt, &num, &more, &size, NULL);
  PRINTF("block2 size: %u", size);
  size++;
  coap_set_header_block2(coap_pkt, num, more, size);
  coap_set_payload(packet, encrypted_payload, encrypted_payload_len);

  // encrypted_payload is not freed by design because otherwise the pointer to the payload would become invalid
#endif

  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_decrypt_payload(void *packet) {
#if COAP_ENABLE_ENCRYPTION_SUPPORT == 1
  coap_packet_t *const coap_pkt = (coap_packet_t *) packet;

  if (coap_pkt->payload_len == 0) {
    return 1;
  }

  static uint8_t *decrypted_payload = NULL;

  void *new_ptr = realloc(decrypted_payload, coap_pkt->payload_len * sizeof(uint8_t));
  if (new_ptr != NULL) {
    decrypted_payload = (uint8_t *) new_ptr;
  } else { // realloc failed - probably out of memory
    free(decrypted_payload);
    return 0;
  }

  int32_t decrypted_payload_len = coap_calculate_decrypted_payload(packet, (char *) decrypted_payload);
  if (decrypted_payload_len == -1) {
    PRINTF("DECRYPTION FAILED! Check the payload length (multiple of 16), the payload itself and the PSK\n");
    return 0;
  }

  new_ptr = realloc(decrypted_payload, decrypted_payload_len * sizeof(uint8_t));
  if (new_ptr != NULL) {
    decrypted_payload = (uint8_t *) new_ptr;
  } else { // realloc failed - probably out of memory
    free(decrypted_payload);
    return 0;
  }

  coap_set_header_encr_alg(packet, 0x00);
  coap_set_payload(packet, decrypted_payload, (uint16_t) decrypted_payload_len);
#endif

  return 1;
}
/*---------------------------------------------------------------------------*/
int
coap_enable_integrity_check_and_encrypt_payload(void *packet, uint8_t retransmission_counter) {
  coap_enable_integrity_check(packet, retransmission_counter);
  coap_encrypt_payload(packet);
  return 1;
}
/*---------------------------------------------------------------------------*/
bool
coap_is_valid_hmac(uint8_t *original_packet, uint32_t relative_hmac_position, size_t packet_len) {
#if COAP_ENABLE_HMAC_SUPPORT == 1
  if (relative_hmac_position == 0 || original_packet == NULL) {
    return false;
  }

  uint8_t packet[packet_len];
  uint8_t *hmac_position = packet + relative_hmac_position;
  uint8_t *original_hmac_position = original_packet + relative_hmac_position;
  uint8_t *byte_after_hmac = hmac_position + COAP_HEADER_HMAC_LENGTH;

  memcpy(packet, original_packet, packet_len);

  size_t hex_all_len = packet_len - COAP_HEADER_HMAC_LENGTH;
  uint8_t hex_all[hex_all_len];
  size_t packet_len_before_hmac_value = hmac_position - packet;
  size_t packet_len_after_hmac_value = packet_len - (packet_len_before_hmac_value + COAP_HEADER_HMAC_LENGTH);

  memcpy(hex_all, packet, packet_len_before_hmac_value);
  memcpy(hex_all + packet_len_before_hmac_value, byte_after_hmac, packet_len_after_hmac_value);

  coap_calculate_hmac(hmac_position, hex_all, hex_all_len);
  uint8_t hmac_comparison = (uint8_t) memcmp(hmac_position, original_hmac_position, COAP_HEADER_HMAC_LENGTH);

  PRINTF("HMAC comparison (0 indicates the HMACs are equal): %i\n", hmac_comparison);

  if (hmac_comparison == 0) {
    PRINTF("HMAC is valid!\n");
    return true;
  } else {
    PRINTF("Hash is invalid!!! FILTER packet\n");
    return false;
  }
#else
  return true;
#endif
}
/*---------------------------------------------------------------------------*/
bool
coap_is_malware_free(void *packet) {
#if COAP_ENABLE_PAYLOAD_INSPECTION == 1
  coap_packet_t *const coap_pkt = (coap_packet_t *) packet;

  if (coap_pkt->encr_alg != 0 && coap_pkt->payload_len > 0) {
    PRINTF("Packet is encrypted, no payload inspection possible!!! FILTER packet\n");
    return false;
  }

  PRINTF("Payload was unencrypted or encryption successful. SCANNING...\n");
  if (strstr((const char *)coap_pkt->payload, "EICAR") != NULL) {
    PRINTF("Malware found!!! FILTER packet\n");
    return false;
  } else {
    PRINTF("Result: No malware found.\n");
    return true;
  }
#else
  return true;
#endif
}
/*---------------------------------------------------------------------------*/
uint16_t
coap_read_persistent_boot_counter(bool disable_caching) {
  static uint16_t boot_counter = 0x0000;
  static uint16_t cache_read_counter = 0;

  if (disable_caching || cache_read_counter == 0) {
    int filedescriptor;
    uint8_t buf[sizeof(uint16_t)];

    filedescriptor = cfs_open(COAP_BOOT_COUNTER_FILENAME, CFS_READ);
    if (filedescriptor >= 0) {
      cfs_seek(filedescriptor, 0, CFS_SEEK_SET);
      cfs_read(filedescriptor, buf, sizeof(buf));
      cfs_close(filedescriptor);
      memcpy(&boot_counter, buf, sizeof(uint16_t));
    }

    PRINTF("Boot counter read from file system: 0x%04x, ", boot_counter);
    cache_read_counter = 0;
  } else if (cache_read_counter == COAP_MAX_BOOT_COUNTER_CACHE_READS) {
    boot_counter++;
    coap_write_persistent_boot_counter(boot_counter);
    PRINTF("\b\b (auto-increment), ");
    cache_read_counter = 0;
  }

  if (!disable_caching) {
    cache_read_counter++;
  }
  return boot_counter;
}
/*---------------------------------------------------------------------------*/
int
coap_write_persistent_boot_counter(uint16_t value) {
  PRINTF("Boot counter to write to file system: 0x%04x, ", value);

  int filedescriptor;
  uint8_t buf[sizeof(uint16_t)];
  memcpy(buf, &value, sizeof(uint16_t));

  cfs_remove(COAP_BOOT_COUNTER_FILENAME);
  filedescriptor = cfs_open(COAP_BOOT_COUNTER_FILENAME, CFS_WRITE);
  if(filedescriptor >= 0) {
    cfs_seek(filedescriptor, 0, CFS_SEEK_SET);
    cfs_write(filedescriptor, buf, sizeof(buf));
    cfs_close(filedescriptor);
  }
  return 1;
}
/*---------------------------------------------------------------------------*/
