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

//TODO check for ar == NULL, realloc fail
int meta_array_append(Meta_Array *ar, Meta *meta)
{
  if (!ar)
    return -1;
  
  if (ar->count == ar->max) {
    if (ar->max == 1)
	ar->max = 4;
    else
      	ar->max *= 2;
    ar->data = realloc(ar->data, sizeof(Meta*)*ar->max);
    if (!ar->data)
      return -1;
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