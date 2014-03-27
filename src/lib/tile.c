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

#include "tile.h"

int tile_wanted(Tile *tile)
{
  assert(tile->refs >= 0);
  
  if (tile->refs)
    return 1;
  
  if (tile->want && ea_count(tile->want))
    return 1;
  
  return 0;
}

Tiledata *tiledata_new(Rect *area, int size, Tile *parent)
{
  Tiledata *tile = calloc(sizeof(Tiledata), 1);
  
  assert(size == 1);
  
  tile->size = size;
  tile->data = calloc(size*area->width*area->height, 1);
  tile->area = *area;
  tile->parent = parent;
  
  return tile;
}

void hack_tiledata_fixsize(int size, Tiledata *tile)
{
  if (tile->size == size)
    return;
  
  free(tile->data);
  
  tile->size = size;
  tile->data = calloc(size*tile->area.width*tile->area.height, 1);
}

void tiledata_del(Tiledata *td)
{
  free(td->data);
  free(td);
}

void tile_del(Tile *tile)
{
  int i;
  
  assert(!tile_wanted(tile));
  
  if (tile->channels)
    for(i=0;i<ea_count(tile->channels);i++) 
      tiledata_del(ea_data(tile->channels, i));
  
  eina_array_free(tile->channels);
  
  if (tile->want)
    eina_array_free(tile->want);

  free(tile);
}

void tiledata_save(Tiledata *tile, const char *path)
{
  FILE *file = fopen(path, "w");
  
  assert(file);
  
  fprintf(file, "P5\n%d %d\n255\n", tile->area.width, tile->area.height);
  fwrite(tile->data, tile->area.width*tile->area.height, 1, file);
  
  fclose(file);
}

Tile *tile_new(Rect *area, Tilehash hash, Filter *f, Filter *f_req)
{
  Tile *tile = calloc(sizeof(Tile), 1);
  
  assert(area != NULL);
  
  tile->area = *area;
  tile->hash = hash;
  tile->fc = f->fc;
  tile->filterhash = filter_hash_value_get(f);
  if (f_req)
    tile->fc_req = f_req->fc;
  tile->refs = 1;
  
  return tile;
}