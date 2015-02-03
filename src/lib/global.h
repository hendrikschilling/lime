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

void *global_meta_check;

#endif