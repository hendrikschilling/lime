#ifndef _LIME_GLOBAL_H
#define _LIME_GLOBAL_H

#include <stdint.h>
#include <Eina.h>
#include "common.h"

uint16_t lime_g2l[256];
uint16_t lime_l2g[65536];

void lime_lock(void);
void lime_unlock(void);

int lime_init(void);
void lime_shutdown(void);

#endif