#ifndef _RENDER_H
#define _RENDER_h

//entspricht dem Fortschritt an einem Knoten
#include "common.h"
#include "filter_public.h"

void lime_render(Filter *f);
void lime_render_area(Rect *area, Filter *f, int thread_id);

#endif