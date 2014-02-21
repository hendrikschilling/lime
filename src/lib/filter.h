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

#ifndef _LIME_FILTER_H
#define _LIME_FILTER_H

#include "global.h"
#include "filter_public.h"

struct _Filter_Core{
  const char *name;
  const char *shortname;
  const char *description;
  Filter *(*filter_new_f)(void);
};

//contains all filters, hashed by filters shortname
Eina_Hash *filters;
Filter *filter_new(Filter_Core *fc);
void filter_del(Filter *f);

#endif