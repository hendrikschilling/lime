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