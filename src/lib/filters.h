#ifndef _LIME_FILTERS_H
#define _LIME_FILTERS_H

#include "filters_public.h"

#include "filter_convert.h"
#include "filter_gauss.h"
#include "filter_contrast.h"
#include "filter_downscale.h"
#include "filter_memsink.h"
#include "filter_loadtiff.h"
#include "filter_load.h"
#include "filter_savetiff.h"
#include "filter_comparator.h"
#include "filter_sharpen.h"
#include "filter_denoise.h"
#include "filter_assert.h"
#include "filter_crop.h"
#include "filter_simplerotate.h"

void lime_filters_init(void);
void lime_filter_add(Filter_Core *fc);

#endif