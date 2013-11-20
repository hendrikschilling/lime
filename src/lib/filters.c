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
  
  if (!f) {
    printf("%d -%s--%s-\n",strcmp(shortname, filter_core_load.shortname), shortname, filter_core_load.shortname);
    return NULL;
  }
  
  assert(f->filter_new_f);
  
  printf("have filter %s\n", shortname);
  
  return f->filter_new_f();
}

void lime_filter_add(Filter_Core *fc)
{
  eina_hash_add(lime_filters, fc->shortname, fc);
}

Filter_Core *lime_filtercore_find(const char *name)
{
  return eina_hash_find(lime_filters, name);
}

Eina_List *lime_filter_chain_deserialize(char *str)
{
  Filter_Core *fc;
  Filter *f, *last_f = NULL;
  Eina_List *filters = NULL;
  
  str = strdup(str);
  
  char *last = str + strlen(str);
  char *next = str;
  char *cur = str;
  char *setting;
  while (cur) {
    next = strchr(cur, ':');
    if (next)
      *next = '\0';
    
    printf("add %s\n", cur);
    
    f = lime_filter_new(cur);
    
    if (!f) {
      printf("no filter for %s\n", cur);
      abort();
    }
    
    //FIXME
    if (next+1 < last)
      cur = next+1;
    else
      cur = NULL;
    
    printf("cur %s\n", cur);
    
    
    //f = fc->filter_new_f();
    
    if (last_f)
      lime_filter_connect(last_f, f);
    
    last_f = f;
    
    filters = eina_list_append(filters, f);
    
    //settings
    if (cur) {
      
    printf("cur1 %s\n", cur);
      next = strchr(cur, '=');
      if (strchr(cur, ',') && next > strchr(cur, ','))
        break;
      while (next) {
        *next = '\0';
        printf("set %s\n", cur);
        
          setting = cur;
          
    printf("cur2 %s\n", cur);
          assert(next+1 < last);
          cur = next+1;
          
          
    printf("cur2,5 %s\n", cur);
          
        if (lime_setting_type_get(f, setting) == MT_INT) {
          
    printf("cur3 %s\n", cur);
          lime_setting_int_set(f, setting, atoi(cur));
          }
          
          next = strchr(cur, ':');
          if (next && next+1 < last && (!strchr(cur, ',') || next < strchr(cur, ','))) {
            cur = next+1;
            next = strchr(cur, '=');
          }
          else
            next = NULL;
          
          
    printf("cur4 %s\n", cur);
        
        
      }
      
    }
      
    cur = strchr(cur, ',');
    if (cur) {
      cur++;
      if (cur >= last)
        cur = NULL;
    }
    
    printf("cure %s\n", cur);
  }
  
  free(str);
  
  return filters;
}