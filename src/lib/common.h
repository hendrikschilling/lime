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

#ifndef _COMMON_H
#define _COMMON_H

#include <Eina.h>
#include <assert.h>

#define ea_count eina_array_count
#define ea_data eina_array_data_get
#define ea_set eina_array_data_set
#define ea_pop eina_array_pop
#define ea_push eina_array_push

typedef struct _Pos {
  int x; //in scaled coordinates
  int y; 
  int scale; //x*(2^scale) = original coordinates
} Pos;

typedef struct _Rect {
  Pos corner;
  int width;
  int height;
} Rect;

typedef struct _Dim {
  int x, y;
  uint32_t width;
  uint32_t height;
  int scaledown_max;
} Dim;

int clip_u8(int a);

#endif