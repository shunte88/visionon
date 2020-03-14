
#ifndef VISSY_TYPES_H
#define VISSY_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

struct vissy_settings {
  int loglevel;
  int port;
  bool reinit_allowed;
  bool daemon;
  bool resilience;
  char service[64];
  char endpoint[64];
  char logfile[128];
};

struct vissy_stats {
  time_t started; // calc uptime of daemon
  unsigned long maxclients;
  unsigned long allclient;
  unsigned long allreinit;
  unsigned long allmessage;
  unsigned long allsmessage;
};

#define METER_DELAY 2 * 1000 // 1000
#define PAYLOADMAX 6 * 1024

#endif
