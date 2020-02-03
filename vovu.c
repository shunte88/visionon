#include <stdbool.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "vosses.h"
#include "vision.h"

#define METER_DELAY  5000

void beforeExit(void)
{
	printf("\033[?25h");
	pthread_exit(NULL);
}

void sigint_handler(int sig)
{
    beforeExit();
    printf("Received INT signal, Exiting...\n");
    exit(0);
}

void attach_signal_handler(void)
{
    struct sigaction sa;

    sa.sa_handler = sigint_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if(sigaction(SIGINT, &sa, NULL) == -1)
    {
        printf("Error, Cannot set signal handler: sigaction() failure. Exiting...\n");
        beforeExit();
        exit(1);
    }
}

void *visualizeServer( void *x_voidptr ) {
	Initialize();
	Serve();
	return NULL;
}

int main( void )
{

    attach_signal_handler();

	pthread_t thread_id;
	uint8_t channel;

	struct vu_meter_t vu_meter =
	{
	    .sample_accumulator	= {0, 0},
		.rms_bar			= {0, 0},
		.rms_levels			= PEAK_METER_LEVELS_MAX,
		.rms_scale			=
		{
			0, 2, 5, 7, 10, 21, 33, 45, 57, 82, 108, 133, 159, 200,
			242, 284, 326, 387, 448, 509, 570, 652, 735, 817, 900,
			1005, 1111, 1217, 1323, 1454, 1585, 1716, 1847, 2005,
			2163, 2321, 2480, 2666, 2853, 3040, 3227, 3414, 3601,
			3788, 3975, 4162, 4349, 4536
		}
	};


	pthread_create(&thread_id, NULL, visualizeServer, NULL);
	sleep(2); // bring server online

	vissy_check();

	printf("\033[1;1H\033[?25l");

	char payload[30];
	int  i;
	GoString meter = {"VU", 2};

	while (true)
	{

		vissy_vumeter( &vu_meter );

		for ( channel = 0; channel < METER_CHANNELS; channel++ )
		{
			for ( i = 0; i < PEAK_METER_LEVELS_MAX; i++ )
			{
				if ( vu_meter.rms_scale[i] > vu_meter.sample_accumulator[channel] )
				{
					vu_meter.rms_bar[channel] = i;
					break;
				}
				vu_meter.rms_charbar[channel][i] = '*';
			}
			vu_meter.rms_charbar[channel][i] = 0;
		}

		/*
		printf("\033[1;1H\033[34mL: (%.2d) %.5lld %s                               \n\033[31mR: (%.2d) %.5lld %s                               \n", 
			vu_meter.rms_bar[0],
			vu_meter.sample_accumulator[0],
			vu_meter.rms_charbar[0],
			vu_meter.rms_bar[1],
			vu_meter.sample_accumulator[1],
			vu_meter.rms_charbar[0]);
		*/

		// need to contruct JSON here
		sprintf( payload, "L:%.2d:%.5lld|R:%.2d:%.5lld",
			vu_meter.rms_bar[0],
			vu_meter.sample_accumulator[0],
			vu_meter.rms_bar[1],
			vu_meter.sample_accumulator[1]);

		GoString p = {payload,strlen(payload)};
		Publish(meter, p);

		usleep( METER_DELAY );

	}

	vissy_close();
	printf("\033[?25h");
	pthread_kill(thread_id, SIGUSR1);

	return 0;

}
