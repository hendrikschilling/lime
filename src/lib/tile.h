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

#ifndef _TILE_H
#define _TILE_H

#define DEFAULT_TILE_SIZE 256
#define DEFAULT_TILE_AREA (DEFAULT_TILE_SIZE*DEFAULT_TILE_SIZE)

struct _Tiledata;
typedef struct _Tiledata Tiledata;

struct _Tile;
typedef struct _Tile Tile;

#include "filter.h"

struct _Tiledata {
  int size; //pixel size in bytes
  void *data; //actual pixel (or whatever) data
  Rect *area; //ref to parent tiles, area
  Tile *parent;
};

//tiles wissen selber überhaupt nicht was sie speichern, das wissen nur die filter die mit ihnen Arbeiten, tiles werden über den hash identifiziert
struct _Tile {
  Tilehash hash; //or uint32_t???
  Rect area;
  Eina_Array *channels; //wenn channel NULL muss *want elemente enthalten. und umgekehrt!
  //Eina_Array *want; //tiles die diesen tile benötigen
  //Eina_Array *want_ch; //tiles die diesen tile benötigen
  int refs;
  //int need; //Anzahl an tiles die noch benötigt werden um diesen Tile zu rendern
  uint64_t time; //time needed to create this tile from existing input
  Filter_Core *fc;	//FIXME use tile hash or something like that
  Filter_Core *fc_req;
  uint32_t filterhash; //for exact filter identification
  Eina_Array *want; //render_nodes that need this tile when it's finished
  uint64_t generation;
};

Tiledata *tiledata_new(Rect *area, int size, Tile *parent);
void hack_tiledata_fixsize(int size, Tiledata *tile);
Tile *tile_new(Rect *area, Tilehash hash, Filter *f, Filter *f_req);
void tile_del(Tile *tile);
void tiledata_del(Tiledata *td);
int tile_wanted(Tile *tile);
void tiledata_save(Tiledata *tile, const char *path);

#endif