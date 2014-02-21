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

#include "global.h"

#include <stdlib.h>
#include <pthread.h>
#include <math.h>

#include "filters.h"

static int inits = 0;

static pthread_mutex_t global_lock;

void lime_lock(void)
{
  pthread_mutex_lock(&global_lock);
}

void lime_unlock(void)
{
  pthread_mutex_unlock(&global_lock);
}

int lime_init(void)
{
  inits++;
  if (inits > 1)
    return 0;
  if (inits < 1)
    return -1;
  
  if (pthread_mutex_init(&global_lock, NULL))
    abort();
  
  eina_init();
  
  static const float GAMMA = 2.2;
  int result;
  int i;

  for (i = 0; i < 256; i++) {
    result = (int)(pow(i/255.0, GAMMA)*65535.0 + 0.5);
    lime_g2l[i] = (unsigned short)result;
  }

  for (i = 0; i < 65536; i++) {
    result = (int)(pow(i/65535.0, 1/GAMMA)*255.0 + 0.5);
    lime_l2g[i] = (unsigned char)result;
  }

  lime_filters_init();
  
  return 0;
}

void lime_shutdown(void)
{
  eina_shutdown();
  //TODO lime filters shutdown
}