
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#include "log.h"
#include "vovu.h"

time_t log_oldrawtime=1;
char   log_timebuf[80];

struct vissy_settings   *pvisset;

void logInit(struct vissy_settings *se)
{
   pvisset = se;
}

void toLog(int level, const char * format, ...)
{
    if(level <= pvisset->loglevel)
    {
        if (pvisset->daemon)
        {
            FILE *logf;

            logf = fopen(pvisset->logfile,"a");
            if(logf == NULL)
            {
                fprintf(stderr,"Error opening log file: %s\n",pvisset->logfile);
                exit(1);
            }

            time_t rawtime;
            time(&rawtime);
            if(log_oldrawtime != rawtime)
            {
                struct tm * timeinfo;
                timeinfo = localtime (&rawtime);
                strftime(log_timebuf,80,"%F %T visionon: ",timeinfo);
                log_oldrawtime = rawtime;
            }

            va_list args;
            va_start(args,format);
            fputs(log_timebuf,logf);
            vfprintf(logf,format,args);
            va_end(args);
            fclose(logf);
        }
        else
        {
            va_list args;
            va_start(args,format);
            vfprintf(stdout,format,args);
            va_end(args);
            fflush(stdout);
        }
    }
}
