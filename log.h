#ifndef VISSLOG_H
#define VISSLOG_H

#include <stdarg.h>

struct vissy_settings;

void logInit(struct vissy_settings *se);
void toLog(int level, const char * format, ...);

#endif