#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"

#include <stdio.h>
#include <string.h>
#include "apps/ntpd/ntpd.h"


PROCESS(ntpdemo_process, "NTPdemo");
AUTOSTART_PROCESSES(&ntpdemo_process, &ntpd_process);

PROCESS_THREAD(ntpdemo_process, ev, data)
{
  static struct etimer et;

  PROCESS_BEGIN();

  etimer_set(&et, CLOCK_SECOND);
  while(1) {
    PROCESS_YIELD();
    if(etimer_expired(&et)) {
      printf("NTP time in Unix seconds: %lu\n", getCurrTime());
      etimer_restart(&et);
    }
  }

  PROCESS_END();
}

