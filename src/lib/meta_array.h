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
int meta_array_append(Meta_Array *ar, Meta *meta);

#endif