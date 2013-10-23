#include "filters.h"

Eina_Hash *lime_filters;

void lime_filters_init(void)
{
  //TODO dynamic loading from filters as dynamic library! on-demand? (by short-name?)
  lime_filters = eina_hash_string_small_new(&free);

  eina_hash_add(lime_filters, filter_core_gauss.shortname, &filter_core_gauss);
  eina_hash_add(lime_filters, filter_core_compare.shortname, &filter_core_compare);
  eina_hash_add(lime_filters, filter_core_down.shortname, &filter_core_down);
  eina_hash_add(lime_filters, filter_core_memsink.shortname, &filter_core_memsink);
  eina_hash_add(lime_filters, filter_core_convert.shortname, &filter_core_convert);
  eina_hash_add(lime_filters, filter_core_loadtiff.shortname, &filter_core_loadtiff);
  eina_hash_add(lime_filters, filter_core_contrast.shortname, &filter_core_contrast);
  eina_hash_add(lime_filters, filter_core_exposure.shortname, &filter_core_exposure);
  eina_hash_add(lime_filters, filter_core_load.shortname, &filter_core_load);
  eina_hash_add(lime_filters, filter_core_savetiff.shortname, &filter_core_savetiff);
  eina_hash_add(lime_filters, filter_core_sharpen.shortname, &filter_core_sharpen);
  eina_hash_add(lime_filters, filter_core_denoise.shortname, &filter_core_denoise);
  eina_hash_add(lime_filters, filter_core_assert.shortname, &filter_core_assert);
  eina_hash_add(lime_filters, filter_core_crop.shortname, &filter_core_crop);
  eina_hash_add(lime_filters, filter_core_simplerotate.shortname, &filter_core_simplerotate);
}

Filter *lime_filter_new(const char *shortname)
{
  Filter_Core *f = eina_hash_find(lime_filters, shortname);
  
  if (!f)
    return NULL;
  
  assert(f->filter_new_f);
  
  return f->filter_new_f();
}

void lime_filter_add(Filter_Core *fc)
{
  eina_hash_add(lime_filters, fc->shortname, fc);
}