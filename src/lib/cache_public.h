/*
 * Copyright (C) 2014 Hendrik Siedelmann <hendrik.siedelmann@googlemail.com>
 *
 * This file is part of lime.
 * 
 * Lime is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Lime is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Lime.  If not, see <http://www.gnu.org/licenses/>.
 */

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