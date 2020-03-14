#ifndef VISSY_VIZ_H
#define VISSY_VIZ_H

#include <stdint.h>
#include <stdbool.h>
#include "visdata.h"

void vissy_close(void);
void vissy_check(void);
uint32_t vissy_get_rate(void);
void vissy_meter_init(struct vissy_meter_t *vissy_meter);
bool vissy_meter_calc(struct vissy_meter_t *vissy_meter, bool samode);

void get_dBfs(struct peak_meter_t *peak_meter);
void get_dB_indices(struct peak_meter_t *peak_meter);

#endif
