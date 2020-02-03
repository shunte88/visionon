
#include <stdbool.h>

#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdint.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>

#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "vision.h"

#define VUMETER_DEFAULT_SAMPLE_WINDOW 1024 * 2

static struct vis_t
{
	pthread_rwlock_t rwlock;
	uint32_t buf_size;
	uint32_t buf_index;
	bool	 running;
	uint32_t rate;
	time_t   updated;
	int16_t  buffer[VIS_BUF_SIZE];
}  *vis_mmap = NULL;


static bool running = false;
static int  vis_fd = -1;
static char *mac_address = NULL;

static char *get_mac_address()
{
	struct  ifconf ifc;
	struct  ifreq  *ifr, *ifend;
	struct  ifreq  ifreq;
	struct  ifreq  ifs[3];

	uint8_t mac[6] = { 0, 0, 0, 0, 0, 0 };

	int sd = socket( AF_INET, SOCK_DGRAM, 0 );
	if ( sd < 0 ) return mac_address;

	ifc.ifc_len = sizeof( ifs );
	ifc.ifc_req = ifs;

	if ( ioctl( sd, SIOCGIFCONF, &ifc ) == 0 )
	{
		// Get last interface.
		ifend = ifs + ( ifc.ifc_len / sizeof( struct ifreq ));

		// Loop through interfaces.
		for ( ifr = ifc.ifc_req; ifr < ifend; ifr++ )
		{
			if ( ifr->ifr_addr.sa_family == AF_INET )
			{
				strncpy( ifreq.ifr_name, ifr->ifr_name, sizeof( ifreq.ifr_name ));
				if ( ioctl( sd, SIOCGIFHWADDR, &ifreq ) == 0 )
				{
					memcpy( mac, ifreq.ifr_hwaddr.sa_data, 6 );
					// Leave on first valid address.
					if ( mac[0]+mac[1]+mac[2] != 0 ) ifr = ifend;
				}
			}
		}
	}

	close( sd );

	char *macaddr = malloc( 18 );

	sprintf( macaddr, "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );

	return macaddr;
}

static void vissy_reopen( void )
{
	char shm_path[40];

	// Unmap memory if it is already mapped.
	if ( vis_mmap )
	{
		munmap( vis_mmap, sizeof( struct vis_t ));
		vis_mmap = NULL;
	}

	// Close file access if it exists.
	if ( vis_fd != -1 )
	{
		close( vis_fd );
		vis_fd = -1;
	}

	// Get MAC adddress if not already determined.
	if ( !mac_address )
	{
		mac_address = get_mac_address();
	}

	/*
		The shared memory object is defined by Squeezelite and is identified
		by a name made up from the MAC address.
	*/
	sprintf( shm_path, "/squeezelite-%s", mac_address ? mac_address : "" );

	// Open shared memory.
	vis_fd = shm_open( shm_path, O_RDWR, 0666 );
	if ( vis_fd > 0 )
	{
		// Map memory.
		vis_mmap = mmap( NULL, sizeof( struct vis_t ),
						 PROT_READ | PROT_WRITE,
						 MAP_SHARED, vis_fd, 0 );

		if ( vis_mmap == MAP_FAILED )
		{
			close( vis_fd );
			vis_fd = -1;
			vis_mmap = NULL;
		}
	}
}

void vissy_close(void)
{
    if ( vis_fd != -1 )
    {
        close( vis_fd );
        vis_fd = -1;
        vis_mmap = NULL;

    }

}

void vissy_check( void )
{

	static time_t lastopen = 0;
	time_t now = time( NULL );

	if ( !vis_mmap )
	{
		if ( now - lastopen > 5 )
		{
			vissy_reopen();
			lastopen = now;
		}
		if ( !vis_mmap ) return;
	}

	pthread_rwlock_rdlock( &vis_mmap->rwlock );

	running = vis_mmap->running;

	if ( running && now - vis_mmap->updated > 5 )
	{
		pthread_rwlock_unlock(&vis_mmap->rwlock );
		vissy_reopen();
		lastopen = now;
	} else
	{
		pthread_rwlock_unlock( &vis_mmap->rwlock );
	}
}

static void vissy_lock( void )
{
	if ( !vis_mmap ) return;
	pthread_rwlock_rdlock( &vis_mmap->rwlock );
}

static void vissy_unlock( void )
{
	if ( !vis_mmap ) return;
	pthread_rwlock_unlock( &vis_mmap->rwlock );
}

static bool vissy_is_playing( void )
{
	if ( !vis_mmap ) return false;
	return running;
}

uint32_t vissy_get_rate( void )
{
	if ( !vis_mmap ) return 0;
	return vis_mmap->rate;
}

static int16_t *vissy_get_buffer( void )
{
	if ( !vis_mmap ) return NULL;
	return vis_mmap->buffer;
}

static uint32_t vissy_get_buffer_len( void )
{
	if ( !vis_mmap ) return 0;
	return vis_mmap->buf_size;
}

static uint32_t vissy_get_buffer_idx( void )
{
	if ( !vis_mmap ) return 0;
	return vis_mmap->buf_index;
}

//  ---------------------------------------------------------------------------
//  Calculates peak dBfs values (L & R) of a number of stream samples.
//  ---------------------------------------------------------------------------
void get_dBfs( struct peak_meter_t *peak_meter )
{
	int16_t  *ptr;
	int16_t  sample;
	uint64_t sample_squared[METER_CHANNELS];
	uint16_t sample_rms[METER_CHANNELS];
	uint8_t  channel;
	size_t   i, wrap;
	int	 offs;

	vissy_check();

	for ( channel = 0; channel < METER_CHANNELS; channel++ )
		sample_squared[channel] = 0;

	if ( vissy_is_playing() )
	{
		vissy_lock();


		offs = vissy_get_buffer_idx() - ( peak_meter->samples * METER_CHANNELS );
		while ( offs < 0 ) offs += vissy_get_buffer_len();

		ptr = vissy_get_buffer() + offs;
		wrap = vissy_get_buffer_len() - offs;

		for ( i = 0; i < peak_meter->samples; i++ )
		{
			for ( channel = 0; channel < METER_CHANNELS; channel++ )
			{
				sample = *ptr++;
				sample_squared[channel] += sample * sample;
			}
			// Check for buffer wrap and refresh if necessary.
			wrap -= 2;
			if ( wrap <= 0 )
			{
				ptr = vissy_get_buffer();
				wrap = vissy_get_buffer_len();
			}
		}
		vissy_unlock();
	}

	for ( channel = 0; channel < METER_CHANNELS; channel++ )
	{
		sample_rms[channel] = round( sqrt( sample_squared[channel] ));
		peak_meter->dBfs[channel] = 20 * log10( (float) sample_rms[channel] /
							(float) peak_meter->reference );
		if ( peak_meter->dBfs[channel] < peak_meter->floor )
		{
			 peak_meter->dBfs[channel] = peak_meter->floor;
		}
	}
}

void get_dB_indices( struct peak_meter_t *peak_meter )
{
	uint8_t		 channel;
	uint8_t		 i;
	static bool	 falling = false;
	static uint16_t hold_inc[METER_CHANNELS] = { 0, 0 };
	static uint16_t fall_inc[METER_CHANNELS] = { 0, 0 };
	static uint16_t over_inc[METER_CHANNELS] = { 0, 0 };
	static uint8_t  over_cnt[METER_CHANNELS] = { 0, 0 };

	for ( channel = 0; channel < METER_CHANNELS; channel++ )
	{
		// Overload check;
		if ( peak_meter->dBfs[channel] == 0 )
		{
			over_cnt[channel]++;
			if ( over_cnt[channel] > peak_meter->over_peaks )
			{
				peak_meter->overload[channel] = true;
				over_inc[channel] = 0;
			}
		}
		else over_cnt[channel] = 0;

		// Countdown for overload reset.
		if ( peak_meter->overload[channel] )
		{
			if ( over_inc[channel] > peak_meter->over_incs )
			{
				peak_meter->overload[channel] = false;
				over_inc[channel] = 0;
			}
			else over_inc[channel]++;
		}
		// Concatenate output meter string.
		for ( i = 0; i < peak_meter->num_levels; i++ )
		{
			if ( peak_meter->dBfs[channel] <= peak_meter->scale[i] )
			{
				peak_meter->bar_index[channel] = i;
				if ( i > peak_meter->dot_index[channel] )
				{
					peak_meter->dot_index[channel] = i;
					peak_meter->elapsed[channel] = 0;
					falling = false;
					hold_inc[channel] = 0;
					fall_inc[channel] = 0;
				}
				else
				{
					i = peak_meter->num_levels;
				}
			}
		}

		// Rudimentary peak hold routine until proper timing is introduced.
		if ( falling )
		{
			fall_inc[channel]++;
			if ( fall_inc[channel] >= peak_meter->fall_incs )
			{
				fall_inc[channel] = 0;
				if ( peak_meter->dot_index[channel] > 0 )
					 peak_meter->dot_index[channel]--;
			}
		}
		else
		{
			hold_inc[channel]++;
			if ( hold_inc[channel] >= peak_meter->hold_incs )
			{
				hold_inc[channel] = 0;
				falling = true;
				if ( peak_meter->dot_index[channel] > 0 )
					 peak_meter->dot_index[channel]--;
			}
		}
	}
}

void vissy_vumeter( struct vu_meter_t *vu_meter ) {

        int16_t  *ptr;
	int16_t  sample;
	uint8_t  channel;
	//uint64_t sample_squared[METER_CHANNELS];
        uint64_t sample_sq;
        size_t i, num_samples, samples_until_wrap;

        int offs;

        num_samples = VUMETER_DEFAULT_SAMPLE_WINDOW;

	for ( channel = 0; channel < METER_CHANNELS; channel++ ) 
		vu_meter->sample_accumulator[channel] = 0;
        //vu_meter->sample_accumulator[1] = 0;

        vissy_lock();
        if (vissy_is_playing()) {

                offs = vissy_get_buffer_idx() - (num_samples * 2);
                while (offs < 0) offs += vissy_get_buffer_len();

                ptr = vissy_get_buffer() + offs;
                samples_until_wrap = vissy_get_buffer_len() - offs;

                for (i=0; i<num_samples; i++) {


			for ( channel = 0; channel < METER_CHANNELS; channel++ )
			{
		                sample = (*ptr++) >> 7;
		                sample_sq = sample * sample;
		                vu_meter->sample_accumulator[channel] += sample_sq;

			}
                        //sample = (*ptr++) >> 7;
                        //sample_sq = sample * sample;
                        //vu_meter->sample_accumulator[1] += sample_sq;

                        samples_until_wrap -= 2;
                        if (samples_until_wrap <= 0) {
                                ptr = vissy_get_buffer();
                                samples_until_wrap = vissy_get_buffer_len();
                        }

                }
        }
        vissy_unlock();

        vu_meter->sample_accumulator[0] /= num_samples;
        vu_meter->sample_accumulator[1] /= num_samples;


}


