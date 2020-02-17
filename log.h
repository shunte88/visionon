#ifndef VISSLOG_H
#define VISSLOG_H

#include <stdarg.h>

struct Hasses_Settings;

void logInit(struct Hasses_Settings *se);
void toLog(int level, const char * format, ...);

#endif