#include "elm_lime_image.h"

Evas_Object *grid, *clipper;

Evas_Object *elm_lime_image_add(Evas_Object *parent)
{
  grid = elm_grid_add(win);
  clipper = evas_object_rectangle_add(evas_object_evas_get(win));
  
  return grid;
}
