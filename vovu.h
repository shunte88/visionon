
#ifndef VISSY_VOVUM_H
#define VISSY_VOVUM_H

#include <stdbool.h>
#include <sys/time.h>
#include <time.h>
#include "vissy.h"

#define VERSION "0.012"

#define CLIENT_CHK_TIME_INTERVAL 60
#define CLIENT_HANDSHAKED_TIMEOUT 3600
#define CLIENT_NOTHSHAKED_TIMEOUT 20
#define BANKSIZE 256
#define MAXEVENTS 10000
#define MAX_READ_SIZE 6144

char *payload_mode(bool samode);
void banner(void);
void check_timeouts(void);
int close_client(int d);
int get_reinit_allowed(void);
void before_exit(void);

struct CommCli {
  int fd;
  struct CommCli *next;
};

#endif
