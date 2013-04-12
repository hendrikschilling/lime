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