#ifndef _LIME_CACHE_PUBLIC_H
#define _LIME_CACHE_PUBLIC_H

#define CACHE_F_RAPX 0b00
#define CACHE_F_RAND 0b01
#define CACHE_F_PROB 0b10

#define CACHE_M_LRU  0b000100
#define CACHE_M_DIST 0b001000
#define CACHE_M_TIME 0b010000
#define CACHE_M_HITN 0b100000

#define CACHE_MASK_F 0b000011
#define CACHE_MASK_M 0b111100

void cache_stats_print(void);
int lime_cache_set(int mem_max, int strategy);

#endif