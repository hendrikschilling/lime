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
  
  tile->data = calloc(size*area->width*area->height, 1);
  tile->area = area;
  tile->parent = parent;
  
  return tile;
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

  assert(tile->channels);
  
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
  
  fprintf(file, "P5\n%d %d\n255\n", tile->area->width, tile->area->height);
  fwrite(tile->data, tile->area->width*tile->area->height, 1, file);
  
  fclose(file);
}

Tile *tile_new(Rect *area, Tilehash hash, Filter *f, Filter *f_req)
{
  Tile *tile = calloc(sizeof(Tile), 1);
  
  assert(area != NULL);
  
  tile->area = *area;
  tile->hash = hash;
  tile->f = f;
  tile->f_req = f_req;
  tile->refs = 1;
  
  return tile;
}