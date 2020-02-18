#ifndef VISSY_VIZ_H
#define VISSY_VIZ_H

#include "kiss_fft.h"

#define VIS_BUF_SIZE 16384 // Predefined in Squeezelite.
#define PEAK_METER_LEVELS_MAX 48 // Number of peak meter intervals
#define SPECTRUM_POWER_MAP_MAX 32 // Number of spectrum bands
#define METER_CHANNELS 2 // Number of metered channels.
#define OVERLOAD_PEAKS 3 // Number of consecutive 0dBFS peaks for overload.
#define X_SCALE_LOG 20
#define MAX_SAMPLE_WINDOW 1024 * X_SCALE_LOG
#define MAX_SUBBANDS MAX_SAMPLE_WINDOW / 2 / X_SCALE_LOG
#define MIN_SUBBANDS 16
#define MIN_FFT_INPUT_SAMPLES 128

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

struct vissy_meter_t
{
    char         channel_name    [METER_CHANNELS][2];
    int          is_mono;
    long long    sample_accum    [METER_CHANNELS]; // vu raw peak values.
    int8_t       floor;       // Noise floor for meter (dB).
    uint16_t     reference;   // Reference level.
    long long    dBfs            [METER_CHANNELS]; // dBfs values.
    long long    dB              [METER_CHANNELS]; // dB values.
    long long    linear          [METER_CHANNELS]; // linear dB (min->max)
    uint8_t      rms_bar         [METER_CHANNELS];
    uint8_t      rms_levels;
    char	     rms_charbar	 [METER_CHANNELS][PEAK_METER_LEVELS_MAX + 1];
    int16_t      rms_scale       [PEAK_METER_LEVELS_MAX];
    int32_t      power_map       [SPECTRUM_POWER_MAP_MAX];
    int          channel_width   [METER_CHANNELS];
    int          bar_size        [METER_CHANNELS];
    int          subbands_in_bar [METER_CHANNELS];
    int          num_bars        [METER_CHANNELS];
    int          channel_flipped [METER_CHANNELS];
    int          clip_subbands   [METER_CHANNELS];
    int          num_subbands;
    int          sample_window;
    int          num_windows;
    double       filter_window   [MAX_SAMPLE_WINDOW];
    double       preemphasis     [MAX_SUBBANDS];
    int          decade_idx      [MAX_SUBBANDS];
    int          decade_len      [MAX_SUBBANDS];
    int          sample_bin_chan [METER_CHANNELS][MAX_SUBBANDS];
    float        avg_power       [2 * MAX_SUBBANDS];
    kiss_fft_cfg cfg;
}VMT;

void vissy_close( void );
void vissy_check( void );
uint32_t vissy_get_rate( void );
void vissy_meter_init( struct vissy_meter_t *vissy_meter );
bool vissy_meter_calc( struct vissy_meter_t *vissy_meter, bool samode );

void get_dBfs( struct peak_meter_t *peak_meter );
void get_dB_indices( struct peak_meter_t *peak_meter );

#endif
