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

#include "meta_array.h"

int ma_count(Meta_Array *ar)
{
  if (ar)
    return ar->count;
  return 0;
}

Meta *ma_data(Meta_Array *ar, int pos)
{
  return ar->data[pos];
}

Meta_Array *meta_array_new(void)
{
  Meta_Array *ar = malloc(sizeof(Meta_Array));
  
  if (!ar) return NULL;
  
  ar->count = 0;
  ar->max = 1;
  
  ar->data = malloc(sizeof(Meta*)*ar->max);
  
  return ar;
}


void meta_array_del(Meta_Array *ar)
{
  free(ar->data);
  free(ar);
}

//TODO check for ar == NULL, realloc fail
int meta_array_append(Meta_Array *ar, Meta *meta)
{
  assert(ar);
  
  if (ar->count == ar->max) {
    if (ar->max == 1)
	ar->max = 4;
    else
      	ar->max *= 2;
    ar->data = realloc(ar->data, sizeof(Meta*)*ar->max);
    if (!ar->data)
      abort();
  }
  
  //TODO mark array as sorted/unsorted!
  /*if (ar->count && ar->data[ar->count-1]->type > meta->type)
    return -1;*/
    
  ar->data[ar->count++] = meta;
  
  return 0;
}

/*
//TODO failures
Meta *meta_array_lookup(Meta_Array *ar, int *remain, int type)
{
  int i;
  
  for(i=0;i<ar->count;i++)
    if (ar->data[i]->type == type) {
      *remain = ar->count - i;
      return ar->data[i];
    }
    
  return NULL;
}

//TODO failures
Meta *meta_array_lookup_tree(Meta_Array *ar, int *types, int depth)
{
  Meta *meta, *sub;
  int count, c;
  
  meta = meta_array_lookup(ar, &count, types[0]);
  
  if (!meta)
    return NULL;
  
  if (!depth)
    return meta;
  
  for(c=0;c<count;c++) {
    sub = meta_array_lookup_tree(meta->childs, types++, depth-1);
    
    if (sub)
      return sub;
    
    
    meta++;
    if (meta->type != types[0])
      return NULL;
  }
  
  return NULL;
}*/