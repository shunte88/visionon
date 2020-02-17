#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <sys/types.h>
#include <fcntl.h>

#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <time.h>

#include "vision.h"
#include "vovu.h"
#include "log.h"

#include "cdata.h"
#include "vcomms.h"
#include "cio.h"
#include "timer.h"

#define METER_DELAY  1000
#define PAYLOADMAX   6*1024

struct vissy_settings visset;
struct vissy_stats    vistats;
char   *input = NULL;
time_t last_cli_ttl_check = 0;
int    epoll_descriptor;
char   log_timebuf[80];
size_t ztimer;

void beforeExit(void)
{
	pthread_exit(NULL);
    timer_stop(ztimer);
    timer_finalize();
}

void sigint_handler(int sig)
{
    beforeExit();
    toLog(0,"Received INT signal, Exiting...\n");
    exit(0);
}

void attach_signal_handler(void)
{
	struct sigaction sa;
	sigset_t nss;

	// Set signal mask - signals to block
	sigemptyset(&nss);
	sigaddset(&nss, SIGCHLD);  			/* ignore child - i.e. we don't need to wait for it */
	sigaddset(&nss, SIGTSTP);  			/* ignore Tty stop signals */
	sigaddset(&nss, SIGTTOU);  			/* ignore Tty background writes */
	sigaddset(&nss, SIGTTIN);  			/* ignore Tty background reads */
	sigprocmask(SIG_BLOCK, &nss, NULL); /* Block the above specified signals */

	// Set up a signal handler
	sa.sa_handler = sigint_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sigaction(SIGHUP, &nss, NULL);      /* catch hangup signal */
	sigaction(SIGTERM, &nss, NULL);     /* catch term signal */
	sigaction(SIGINT, &nss, NULL);      /* catch interrupt signal */

}

int vissy_cio_high_read(struct CliConn *client,char *buffer)
{
    if(strlen(buffer) > 0)
    {
        toLog(0,"Received from %s <%d>:\n",client->info,client->descr);
        vcomms_received(client,buffer,visset.endpoint);
    }
    return 0;
}

int vissy_cio_low_write(struct CliConn *client,char *buffer,int length)
{
    int s;
    if(client->err)
        return 1;

    s=write(client->descr,buffer,length);
    if(s != length)
    {
        client->err = 1;
        return 1;
    }
    return 0;
}

void checkTimeouts(void)
{
    time_t t = time(NULL);
    if(last_cli_ttl_check + CLIENT_CHK_TIME_INTERVAL >= t)
        return;
    toLog(0,"- Client timeout check...\n");
    last_cli_ttl_check = t;
    client_start();
    while(client_next() != NULL)
        while(client_current() != NULL &&
                ((client_current()->status == STATUS_COMM && 
                    client_current()->created + CLIENT_HANDSHAKED_TIMEOUT < t) 
                  || 
                 (client_current()->status != STATUS_COMM && 
                    client_current()->created + CLIENT_NOTHSHAKED_TIMEOUT < t)))
        {
            toLog(0,"--- Client %s reach timeout, closing...\n",client_current()->info);
            close_client(client_current()->descr);
        }
}

int get_reinit_allowed(void)
{
    return visset.reinit_allowed;
}

void diffsec_to_str(int diff_sec,char *buffer,int max)
{
    int x,d,h,m,s;
    s = diff_sec%86400;
    d = (diff_sec-s)/86400;
    x = s;
    s = x%3600;
    h = (x-s)/3600;
    x = s;
    s = x%60;
    m = (x-s)/60;
    if(d > 0)
        snprintf(buffer,max,"%dday %02d:%02d:%02d",d,h,m,s);
    else
        snprintf(buffer,max,"%02d:%02d:%02d",h,m,s);
}

int create_and_bind(int port)
{
    char portstr[10];
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd,optval=1;

    toLog(0,"Create/bind listening socket (%d)...\n",port);

    memset(&hints, 0, sizeof (struct addrinfo));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    snprintf(portstr,10,"%d",port);
    s = getaddrinfo (NULL,portstr, &hints, &result);
    if(s != 0)
    {
        toLog(0,"Error in getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        s = setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR,&optval,sizeof(optval));
        if(s != 0)
        {
            toLog(0,"Error, setsockopt() failure\n");
            return -1;
        }

        s = bind (sfd, rp->ai_addr, rp->ai_addrlen);
        if (s == 0)
        {
            /* We managed to bind successfully! */
            toLog(0,"Bind to port:%s <%d>\n",portstr,sfd);
            break;
        }
        close(sfd);
    }

    if (rp == NULL)
    {
        toLog(0,"Error, could not bind socket on port %d\n",port);
        return -1;
    }
    freeaddrinfo(result);
    toLog(2,"Socket created and bound (on %d)\n",port);

    return sfd;

}

int make_socket_non_blocking(int sfd)
{
    int flags, s;

    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1)
    {
      toLog(0,"Error, fcntl() failure in make_socket_non_blocking (1)\n");
      return 1;
    }

    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if (s == -1)
    {
        toLog(0,"Error, fcntl() in make_socket_non_blocking (2)\n");
        return 1;
    }
    return 0;
}

int close_client(int d)
{

    toLog(0,"close client %d\n",d);
    /* Closing the descriptor will make epoll remove it
       from the set of descriptors which are monitored.
       this below may unnecessary */
    epoll_ctl(epoll_descriptor,EPOLL_CTL_DEL,d,NULL);

    cio_client_close(client_get(d));
    close(d);

    // Remove from the list
    client_del(d);
    toLog(0,"Closed connection (Remaining clients: %d) <%d>\n",client_count(),d);
    return 0;
}

int printhelp(void)
{
    printf("VisionOn SSE (EventServer) Visualization Service\n"
           "Usage:\n visionon -p=<SSE_PORT> -uri=<ENDPOINT_URI>\n"
                   "        [-quiet | -debug] [-l=<LOGFILE>]\n"
                   "        [-ra] [-nodaemon]\n\n");
    return 0;

}

void publish( char *subevent, char *payload )
{
	char fm[PAYLOADMAX+3]; 
    strncpy( fm, "", PAYLOADMAX+3 );
    strcat( fm, subevent );
    strcat( fm, "=" );
    strcat( fm, payload );
    sendmessages(fm);

}

char* jints(const int list[], const char *delimiters, int* sz)
{
    char *ret;
    char ints[32];
    int i = 1;
    sprintf(ints,"%d", list[0]);
    ret = realloc(NULL, strlen(ints)+1);
    strcpy(ret, ints);
    while (list[i]){
        sprintf(ints,"%d", list[i]);
        ret = (char*)realloc(ret, strlen(ret) + strlen(ints) + strlen(delimiters) + 1);
        strcat(ret, delimiters);
        strcat(ret, ints);
        i++;
    }
    *sz = i;
    return ret;
}

void *initServer( void *x_voidptr )
{
    struct epoll_event event;
    struct epoll_event *events;

    int s;
    int sfd = -1;

    input = (char *)malloc(sizeof(char) * MAX_READ_SIZE);

    cio_high_read_SET(vissy_cio_high_read);
    cio_low_write_SET(vissy_cio_low_write);

    client_init();
    vcomms_init( &visset, &vistats );

    char noop[] = "";
    if (cio_init(false, noop, noop))
    {
        toLog(0,"Exiting due to previous error...\n");
        beforeExit();
        exit(1);
    }

    toLog(2,"Open SSE port to listen...\n");
    sfd = create_and_bind(visset.port);
    if (sfd == -1)
    {
        toLog(0,"Exiting due to previous error...\n");
        beforeExit();
        exit(1);
    }
    s = make_socket_non_blocking(sfd);
    if(s == -1)
    {
        toLog(0,"Exiting due to previous error...\n");
        beforeExit();
        exit(1);
    }
    s = listen (sfd, SOMAXCONN);
    if(s == -1)
    {
        toLog(0,"Error, listen() failure. Exiting...\n");
        beforeExit();
        exit(1);
    }

    toLog(2,"Creating epoll...\n");
    epoll_descriptor = epoll_create1(0);
    if (epoll_descriptor == -1)
    {
        toLog(0,"Error, epoll_create1() failure, Exiting...\n");
        beforeExit();
        exit(1);
    }
    event.data.fd = sfd;
    event.events = EPOLLIN | EPOLLET; 
    s = epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, sfd, &event);
    if(s == -1)
    {
        toLog(0,"Error, epoll_ctl() add sse socket failure (2) Exiting...\n");
        beforeExit();
        exit(1);
    }

    toLog(2,"Epoll created.\n");

    events = calloc (MAXEVENTS, sizeof event);

    toLog(2,"Starting main event loop...\n");

    while(true)
    {
        int n,i;

        checkTimeouts();
        n = epoll_wait (epoll_descriptor, events, MAXEVENTS, -1);
        for (i = 0; i < n; i++)
        {
            if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN)))
            {
                /* An error has occured on this fd, or the socket is not
                   ready for reading (why were we notified then?) */
                toLog(0, "SSE client HUP/ERR or shutdown, Closing...\n");
                close_client(events[i].data.fd);
                continue;
            }
            else if (sfd == events[i].data.fd)
            {
                /* We have a notification on the listening socket, which
                   means one or more incoming connections. */
                while (true)
                {

                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof(in_addr);
                    infd = accept(sfd, &in_addr, &in_len);
                    if(infd == -1)
                    {
                        if ((errno == EAGAIN) ||
                            (errno == EWOULDBLOCK))
                        {
                            /* We have processed all incoming connections. */
                            break;
                        }
                        else
                        {
                            toLog(1, "Error, accept() failure!\n");
                            break;
                        }
                    }

                    s = getnameinfo (&in_addr, in_len,
                                     hbuf, sizeof hbuf,
                                     sbuf, sizeof sbuf,
                                     NI_NUMERICHOST | NI_NUMERICSERV);
                    if(s == 0)
                        toLog(1,"Connect from %s on port %s as <%d>\n",hbuf,sbuf,infd);
                    else
                    {
                        toLog(1,"Error in getnameinfo() <%d>\n",infd);
                    }

                    /* Make the incoming socket non-blocking and add it to the
                       list of fds to monitor. */
                    s = make_socket_non_blocking(infd);
                    if (s == -1)
                    {
                        toLog(1,"Cannot set new accepted socket to non blocking. Closing socket!\n");
                        close(infd);
                        break;
                    }

                    event.data.fd = infd;
                    event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, infd, &event);
                    if (s == -1)
                    {
                        toLog(0,"Error, epoll_ctl() add failure (3)\n");
                        beforeExit();
                        exit(1);
                    }

                    //Add to my list
                    int ccount;
                    client_add(infd);
                    snprintf(client_current()->info,30,"%s:%s",hbuf,sbuf);
                    client_current()->status = STATUS_NEW;

                    ccount = client_count();
                    toLog(0,"Added to the list (%d).\n",ccount);
                    if(vistats.maxclients < ccount)
                        vistats.maxclients = ccount;

                }
                continue;
            }
            else
            {
                /* We have data on the fd waiting to be read. Read and
                   display it. We must read whatever data is available
                   completely, as we are running in edge-triggered mode
                   and won't get a notification again for the same
                   data. */
                bool done = false;
                input[0] = '\0';
                char *input_p = input;

                ssize_t fullcount=0;
                ssize_t count;
                while(true)
                {
                    count = read(events[i].data.fd,input_p, MAX_READ_SIZE-strlen(input));
                    if(count == -1)
                    {
                        /* If errno == EAGAIN, that means we have read all
                           data. So go back to the main loop. */
                        if (errno != EAGAIN)
                        {
                            toLog(1,"Error, read() failure (2) closing client...\n");
                            done = true;
                        }
                        break;
                    }
                    else if (count == 0)
                    {
                        done = true;
                        break;
                    }
                    fullcount += count;
                    input_p = input_p + count;
                }
                input[fullcount]='\0';

                if(done)
                {
                    toLog(2,"Connection closed by peer:\n");
                    close_client(events[i].data.fd);
                }
                else
                {
                    cio_low_read(client_get(events[i].data.fd),input,fullcount);
                }
            }
        }
    }

    free (events);
    close (sfd);

    return NULL;

}

void construct_payload(const struct vissy_meter_t vissy_meter, char * meter, char * payload)
{
    int numFFT = 0;
    char delim[1] = "";	
    char scratch[PAYLOADMAX];

    // construct the JSON payload

    strncpy( payload, "", PAYLOADMAX );
    sprintf( scratch, "{\"type\":\"%s\",\"channel\":[",meter);
    strcat( payload, scratch );

    for ( int channel = 0; channel < METER_CHANNELS; channel++ )
    {

        // simple JSON format - non-too complex (as yet) so we roll oour own
        strcat( payload, delim );
        sprintf( scratch, "{\"name\":\"%c\",\"accumulated\":%lld,",
            vissy_meter.channel_name[channel][0],
            vissy_meter.sample_accum[channel]);
        strcat( payload, scratch );
        sprintf( scratch, "\"scaled\":%d,\"dBfs\":%lld,\"dB\":%lld,",
            vissy_meter.rms_bar[channel],
            vissy_meter.dBfs[channel],
            vissy_meter.dB[channel]);
        strcat( payload, scratch );
        sprintf( scratch, "\"linear\":%lld,",
            vissy_meter.linear[channel]);
        strcat( payload, scratch );
        sprintf( scratch, "\"FFT\":[%s],",
            jints(vissy_meter.sample_bin_chan[channel], ",", &numFFT));
        strcat( payload, scratch );
        sprintf( scratch, "\"numFFT\":%d}", numFFT);
        strcat( payload, scratch );
        delim[0] = ',';

    }
    strcat( payload, "]}");

}

void zero_payload( size_t timer_id, void * user_data )
{
	char meter[] = "VU";
	char payload[PAYLOADMAX];
	struct vissy_meter_t vissy_meter =
	{
        .channel_name       = {"L","R"},
		.sample_accum		= {0, 0},
		.rms_bar			= {0, 0},
		.dBfs				= {-1000, -1000},
		.dB 				= {-1000, -1000},
		.linear 			= {0, 0}
    };
    
    construct_payload( vissy_meter, meter, payload );
    publish(meter, payload);

}

int main( int argi, char **argc )
{

    strcpy(visset.service,"VisionOn");
	
    visset.port = 8022;
    visset.loglevel = 1;
    visset.reinit_allowed = false;
    visset.daemon = true;

    strcpy(visset.endpoint, "/visionon");
    strcpy(visset.logfile, "/var/log/visionon.log");

    vistats.startDaemon = 0;
    vistats.maxclients  = 0;
    vistats.allclient   = 0;
    vistats.allreinit   = 0;
    vistats.allmessage  = 0;
    vistats.allsmessage = 0;

    strcpy(log_timebuf,"error:");

    if ( argi <= 1 )
    {
        printhelp();
        return 0;
    }

    int p;
    for(p = 1 ; p < argi ; ++p)
    {
        if(!strcmp(argc[p],"-h") || !strcmp(argc[p],"-help"))
            return printhelp();

        if(!strcmp(argc[p],"-quiet"))
        {
            visset.loglevel = 0;
            continue;
        }
        if(!strcmp(argc[p],"-debug"))
        {
            visset.loglevel = 2;
            continue;
        }

        if(!strcmp(argc[p],"-ra"))
        {
            visset.reinit_allowed = true;
            continue;
        }

        if(!strcmp(argc[p],"-nodaemon"))
        {
            visset.daemon = false;
            continue;
        }

        if(!strncmp(argc[p],"-uri=",5))
        {
            strncpy(visset.endpoint,argc[p]+5,64);
            continue;
        }

        if(!strncmp(argc[p],"-p=",3))
        {
            if(sscanf(argc[p]+3,"%d",&visset.port) == 1 && visset.port > 0)
                continue;
        }

        if(!strncmp(argc[p],"-l=",3))
        {
            strncpy(visset.logfile,argc[p]+3,128);
            continue;
        }

        if(!strncmp(argc[p],"-",1))
        {
            fprintf(stderr,"Error, unknown switch: \"%s\"\n",argc[p]);
            return 1;
        }
    }

    if(strlen(visset.endpoint) == 0 ||
       visset.port == 0 )
    {
        printhelp();
        return 0;
    }

    if ( visset.logfile[0] != '/' )
    {
        fprintf(stderr,"WARNING: Use absolute path to specify log file!\n");
        return 0;
    }

    vistats.startDaemon = time(NULL);

	logInit(&visset);
    printf("\n=== daemon starting ===\n");

    if(visset.loglevel > 1)
    {

        toLog(0,"\n__      ___     _                ____\n");
        toLog(0,"\\ \\    / (_)   (_)              / __ \\\n");
        toLog(0," \\ \\  / / _ ___ _  ___  _ __   | |  | |_ __\n");
        toLog(0,"  \\ \\/ / | / __| |/ _ \\| '_ \\  | |  | | '_ \\\n");
        toLog(0,"   \\  /  | \\__ \\ | (_) | | | | | |__| | | | |\n");
        toLog(0,"    \\/   |_|___/_|\\___/|_| |_|  \\____/|_| |_|\n\n");

        toLog(0,"Parameters:\n");
        toLog(0," TCP Port (SSE) .: %d\n",visset.port);
        toLog(0," Endpoint .......: %s\n",visset.endpoint);
        toLog(0," Log Level ......: %d\n",visset.loglevel);
        toLog(0," Log file .......: %s\n",visset.logfile);

        printf("\n__      ___     _                ____\n");
        printf("\\ \\    / (_)   (_)              / __ \\\n");
        printf(" \\ \\  / / _ ___ _  ___  _ __   | |  | |_ __\n");
        printf("  \\ \\/ / | / __| |/ _ \\| '_ \\  | |  | | '_ \\\n");
        printf("   \\  /  | \\__ \\ | (_) | | | | | |__| | | | |\n");
        printf("    \\/   |_|___/_|\\___/|_| |_|  \\____/|_| |_|\n\n");

        printf("Parameters:\n");
        printf(" TCP Port (SSE) .: %d\n",visset.port);
        printf(" Endpoint .......: %s\n",visset.endpoint);
        printf(" Log Level ......: %d\n",visset.loglevel);
        printf(" Log file .......: %s\n",visset.logfile);

        fflush(stdout);
    }

    if (visset.daemon)
    {
        if(visset.loglevel > 0)
            toLog(0,"Started, entering daemon mode...\n");

        if(daemon(0,0) == -1)
        {
            toLog(0,"Error, daemon() failure, Exiting...\n");
            exit(1);
        }
        toLog(0,"Active daemon mode.\n");
    }
    else
    {
        printf("\nDaemon mode is disabled!\n"
                "All messages written to the standard output!\n"
                "Log file will not be used!\n\n");
    }

    attach_signal_handler();

	pthread_t thread_id;
	uint8_t channel;

	pthread_create(&thread_id, NULL, initServer, NULL);
	sleep(1); // bring server online

	struct vissy_meter_t vissy_meter =
	{
        .channel_name       = {"L","R"},
		.sample_accum		= {0, 0},
		.rms_bar			= {0, 0},
		.dBfs				= {-1000, -1000},
		.dB 				= {-1000, -1000},
		.linear 			= {0, 0},
		.rms_levels			= PEAK_METER_LEVELS_MAX,
		.floor				= -96,
		.reference			= 32768,
		.is_mono			= 0,
		.channel_width		= {192, 192},
		.channel_flipped	= {0, 0},
		.bar_size			= {6, 6},
		.clip_subbands		= {0, 0},
		.rms_scale			=
		{
			0, 2, 5, 7, 10, 21, 33, 45, 57, 82, 108, 133, 159, 200,
			242, 284, 326, 387, 448, 509, 570, 652, 735, 817, 900,
			1005, 1111, 1217, 1323, 1454, 1585, 1716, 1847, 2005,
			2163, 2321, 2480, 2666, 2853, 3040, 3227, 3414, 3601,
			3788, 3975, 4162, 4349, 4536
		},
		.power_map			=
		{
			0, 362, 2048, 5643, 11585, 20238, 31925, 46935, 65536, 87975, 114486, 
			145290, 180595, 220603, 265506, 315488, 370727, 431397, 497664, 
			569690, 647634, 731649, 821886, 918490, 1021605, 1131370, 1247924, 
			1371400, 1501931, 1639645, 1784670, 1937131
		}
	};

	vissy_check();
    vissy_meter_init( &vissy_meter );

	char meter[] = "VU";
	char payload[PAYLOADMAX];
	int  i;

    timer_initialize();
    ztimer = timer_start(100, zero_payload, TIMER_SINGLE_SHOT, NULL);

	while (true)
	{

        if ( vissy_meter_calc( &vissy_meter ) )
        {

            for ( channel = 0; channel < METER_CHANNELS; channel++ )
            {
                for ( i = 0; i < PEAK_METER_LEVELS_MAX; i++ )
                {
                    if ( vissy_meter.rms_scale[i] > vissy_meter.sample_accum[channel] )
                    {
                        vissy_meter.rms_bar[channel] = i;
                        break;
                    }
                    //vissy_meter.rms_charbar[channel][i] = '*';
                }
                //vissy_meter.rms_charbar[channel][i] = 0;
            }

            construct_payload(vissy_meter, meter, payload);
            publish(meter, payload); // construct, and now publish JSON

            // cleanup (zero meter) timer - when the toons stop we zero
            timer_stop(ztimer);
            ztimer = timer_start(5000, zero_payload, TIMER_SINGLE_SHOT, NULL);

        }

		usleep( METER_DELAY );

	}

	vissy_close();
	pthread_kill(thread_id, SIGUSR1);
    timer_finalize();

	return 0;

}


//end.
