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

#ifndef _FILTER_MEMSINK_H
#define _FILTER_MEMSINK_H

#include "Lime.h"

//TODO this as setting? Must not cause full unconfiguration: TODO track unconfiguration to individual filters!

void filter_memsink_buffer_set(Filter *f, uint8_t *raw_data, int thread_id);

extern Filter_Core filter_core_memsink;

#endif
