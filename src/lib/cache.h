#ifndef _LIME_CACHE_H
#define _LIME_CACHE_H

#include "cache_public.h"

#include <time.h>
#include "tile.h"

void cache_tile_add(Tile *tile);
Tile *cache_tile_get(Tilehash *hash);
void cache_stats_update(Filter *f, int hit, int miss, int time, int count);
void cache_tile_channelmem_add(Tile *tile);

#endif