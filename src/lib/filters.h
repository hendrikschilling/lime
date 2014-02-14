#ifndef _LIME_FILTERS_H
#define _LIME_FILTERS_H

#include "filters_public.h"

void lime_filters_init(void);
void lime_filter_add(Filter_Core *fc);
Filter_Core *lime_filtercore_find(const char *name);

#endif