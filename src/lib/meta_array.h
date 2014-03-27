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

#ifndef _META_ARRAY_H
#define _META_ARRAY_H

struct _Meta_Array;
typedef struct _Meta_Array Meta_Array;

#include "meta.h"

struct _Meta_Array
{
  int count;
  int max;
  Meta **data;
};

int ma_count(Meta_Array *ar);
Meta *ma_data(Meta_Array *ar, int pos);
Meta_Array *meta_array_new(void);
void meta_array_del(Meta_Array *ar);
int meta_array_append(Meta_Array *ar, Meta *meta);

#endif