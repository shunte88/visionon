#ifndef VISSY_H
#define VISSY_H

#define VIS_BUF_SIZE 16384 // Predefined in Squeezelite.
#define PEAK_METER_LEVELS_MAX 48 // Number of peak meter intervals / LEDs.
#define METER_CHANNELS 2 // Number of metered channels.
#define OVERLOAD_PEAKS 3 // Number of consecutive 0dBFS peaks for overload.

struct peak_meter_t
{
    uint16_t int_time;   // Integration time (ms).
    uint16_t samples;    // Samples for integration time.
    uint16_t hold_time;  // Peak hold time (ms).
    uint16_t hold_incs;  // Hold time counter.
    uint16_t fall_time;  // Fall time (ms).
    uint16_t fall_incs;  // Fall time counter.
    uint8_t  over_peaks; // Number of consecutive 0dBFS samples for overload.
    uint16_t over_time;  // Overload indicator time (ms).
    uint16_t over_incs;  // Overload indicator count.
    uint8_t  num_levels; // Number of display levels
    int8_t   floor;      // Noise floor for meter (dB).
    uint16_t reference;  // Reference level.
    bool     overload  [METER_CHANNELS]; // Overload flags.
    int8_t   dBfs      [METER_CHANNELS]; // dBfs values.
    uint8_t  bar_index [METER_CHANNELS]; // Index for bar display.
    uint8_t  dot_index [METER_CHANNELS]; // Index for dot display (peak hold).
    uint32_t elapsed   [METER_CHANNELS]; // Elapsed time (us).
    int16_t  scale     [PEAK_METER_LEVELS_MAX]; // Scale intervals.
};


struct vu_meter_t
{
	long long sample_accumulator [METER_CHANNELS]; // vu raw peak values.
    int8_t    floor;      // Noise floor for meter (dB).
    uint16_t  reference;  // Reference level.
    int8_t    dBfs               [METER_CHANNELS]; // dBfs values.
	uint8_t   rms_bar            [METER_CHANNELS];
	uint8_t   rms_levels;
	char	  rms_charbar	     [METER_CHANNELS][PEAK_METER_LEVELS_MAX + 1];
	int16_t   rms_scale          [PEAK_METER_LEVELS_MAX];
};

void vissy_close( void );
void vissy_check( void );
uint32_t vissy_get_rate( void );
void vissy_vumeter( struct vu_meter_t *vu_meter );

void get_dBfs( struct peak_meter_t *peak_meter );
void get_dB_indices( struct peak_meter_t *peak_meter );

#endif
