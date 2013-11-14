#ifndef _LIME_FILTERS_PUBLIC_H
#define _LIME_FILTERS_PUBLIC_H

#include "filter_public.h"

Filter *lime_filter_new(const char *shortname);
Eina_List *lime_filter_chain_deserialize(char *str);

#endif