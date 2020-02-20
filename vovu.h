
#ifndef VISSY_VOVUM_H
#define VISSY_VOVUM_H

#include <stdbool.h>

#define VERSION "0.012"

#define CLIENT_CHK_TIME_INTERVAL 60
#define CLIENT_HANDSHAKED_TIMEOUT 3600
#define CLIENT_NOTHSHAKED_TIMEOUT 20
#define BANKSIZE 256
#define MAXEVENTS 10000
#define MAX_READ_SIZE 6144

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

char *payload_mode(bool samode);
void banner(void);
void check_timeouts(void);
int close_client(int d);
int get_reinit_allowed(void);
void diffsec_to_str(int diff_sec, char *buffer, int max);
void beforeExit(void);

struct CommCli {
  int fd;
  struct CommCli *next;
};

#endif
