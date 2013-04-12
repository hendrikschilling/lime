#ifndef _FILTER_MEMSINK_H
#define _FILTER_MEMSINK_H

#include "Lime.h"

//TODO this as setting? Must not cause full unconfiguration: TODO track unconfiguration to individual filters!

void filter_memsink_buffer_set(Filter *f, uint8_t *raw_data, int thread_id);

Filter_Core filter_core_memsink;

#endif