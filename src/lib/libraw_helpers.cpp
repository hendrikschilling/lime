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

#include <libraw.h>

#include "libraw_helpers.h"

//ATTENTION here be dragons - this could break in so many ways...

libraw_data_t *libraw_data_copy(libraw_data_t *src)
{
  LibRaw *copy = (LibRaw*)malloc(sizeof(LibRaw));
  
  memcpy(copy, src->parent_class, sizeof(LibRaw));
  
  copy->imgdata.parent_class = copy;
  
  return &copy->imgdata;
}

void libraw_copy_del(libraw_data_t *copy)
{  
  free((LibRaw *)copy->parent_class);
}
