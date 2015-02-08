/*
 * Copyright (C) 2014 Hendrik Siedelmann <hendrik.siedelmann@googlemail.com>
 *
 * This file is part of lime.
 * 
 * Lime is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Lime is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Lime.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "filters.h"

Eina_Hash *lime_filters;

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
#include "filter_pretend.h"
#include "filter_crop.h"
#include "filter_simplerotate.h"
#include "filter_interleave.h"
#include "filter_savejpeg.h"
#include "filter_rotate.h"
#include "filter_curves.h"
#include "filter_lrdeconv.h"
#include "filter_lensfun.h"

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
  eina_hash_add(lime_filters, filter_core_pretend.shortname, &filter_core_pretend);
  //eina_hash_add(lime_filters, filter_core_crop.shortname, &filter_core_crop);
  eina_hash_add(lime_filters, filter_core_simplerotate.shortname, &filter_core_simplerotate);
  eina_hash_add(lime_filters, filter_core_rotate.shortname, &filter_core_rotate);
  eina_hash_add(lime_filters, filter_core_savejpeg.shortname, &filter_core_savejpeg);
  eina_hash_add(lime_filters, filter_core_curves.shortname, &filter_core_curves);
  eina_hash_add(lime_filters, filter_core_lrdeconv.shortname, &filter_core_lrdeconv);
  eina_hash_add(lime_filters, filter_core_lensfun.shortname, &filter_core_lensfun);
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

Filter_Core *lime_filtercore_find(const char *name)
{
  return eina_hash_find(lime_filters, name);
}

char *string_escape_colon(const char *str)
{
  const char *str_ptr = str;
  char esc[EINA_PATH_MAX];
  char *esc_ptr = esc;
  
  while (*str_ptr) {
    esc_ptr[0] = str_ptr[0];
    //create double colon
    if (str_ptr[0] == ':') {
      esc_ptr++;
      esc_ptr[0] = ':';
    }
    esc_ptr++;
    str_ptr++;
  }
  esc_ptr[0] = '\0';
 
  return strdup(esc);
}

char *string_unescape_colon(const char *esc)
{
  const char *esc_ptr = esc;
  char str[EINA_PATH_MAX];
  char *str_ptr = str;
  
  while (*esc_ptr) {
    *str_ptr = *esc_ptr;
    //skip any double colon
    if (esc_ptr[0] == ':' && esc_ptr[1] == ':')
      esc_ptr++;
    esc_ptr++;
    str_ptr++;
  }
  
  str_ptr[0] = '\0';
  
  return strdup(str);
}

char *next_single_colon(char *str)
{
  char *found = strchr(str, ':');
  
  while (found) {
    if (found[1] != ':')
      return found;
    found = strchr(found+2, ':');
  }
  
  return NULL;
}


char *lime_filter_chain_serialize(Filter *f)
{
  int i;
  //FIXME handle large settings
  char *buf = malloc(4096);
  char *str = buf;
  Meta *m;
  
  while (f) {
    str += sprintf(str, "%s", f->fc->shortname);
    for(i=0;i<ea_count(f->settings);i++) {
      m = ea_data(f->settings, i);
      if (m->data)
        switch (m->type) {
          case MT_INT :
            str += sprintf(str, ":%s=%d", m->name, *(int*)m->data);
            break;
          case MT_FLOAT : 
            str += sprintf(str, ":%s=%f", m->name, *(float*)m->data);
            break;
          case MT_STRING : 
            assert(!strchr((char*)m->data, ':'));
            str += sprintf(str, ":%s=%s", m->name, string_escape_colon((char*)m->data));
            break;
      }
    else
      printf("no data for %s\n", m->name);
    }
    
    if (f->node_orig->con_trees_out &&  ea_count(f->node_orig->con_trees_out))
      f = ((Con*)ea_data(f->node_orig->con_trees_out, 0))->sink->filter;
    else
      f = NULL;

    /* FIXME
    if (f->out && ea_count(f->out))
      f = eina_array_data_get(f->out, 0);
    else
      f = NULL;*/
    
    if (f)
      str += sprintf(str, ",");
  }
  
  return buf;
}

//FIXME check if setting does exist, give some debug info if parsing fails!
Eina_List *lime_filter_chain_deserialize(char *str)
{
  int i;
  int checksettings;
  Meta *m;
  Filter *f, *last_f = NULL;
  Eina_List *filters = NULL;
  
  str = strdup(str);
  
  char *last = str + strlen(str);
  char *next = str;
  char *next_comma;
  char *cur = str;
  const char *setting;
  char *tmp;
  while (cur) {
    next = next_single_colon(cur);
    next_comma = strchr(cur, ',');
    if (next || next_comma) {
      if (next && (next < next_comma || !next_comma)) {
        printf("%s with settings\n", cur);
        checksettings = 1;
        *next = '\0';
        f = lime_filter_new(cur);
      }
      else if (next_comma && (next > next_comma || !next)) {
        printf("%s no settings\n", cur);
        checksettings = 0;
        *next_comma = '\0';
        f = lime_filter_new(cur);
        cur = next_comma+1;
        next = NULL;
      }
      else {
        //FIXME  check rethink?
        abort();
      }
    }
    else {
      printf("%s last filter no settings\n", cur);
      checksettings = 0;
      f = lime_filter_new(cur);
      next = NULL;
      cur = NULL;
    }
    
    if (!f) {
      printf("no filter for %s\n", cur);
      return NULL;
    }
    
    if (next && next+1 < last)
      cur = next+1;
    else
      checksettings = 0;
    
    //f = fc->filter_new_f();
    
    if (last_f)
      lime_filter_connect(last_f, f);
    
    last_f = f;
    
    filters = eina_list_append(filters, f);
    
    //settings
    if (cur && checksettings) {
      next = strchr(cur, '=');
      if (strchr(cur, ',') && next > strchr(cur, ','))
        break;
      while (next) {
        *next = '\0';
        
          setting = cur;
          assert(next+1 < last);
          cur = next+1;
  
        if (!ea_count(f->settings)) {
          printf("no settings for %s\n", f->fc->name);
          return NULL;
        }
  
        for(i=0;i<ea_count(f->settings);i++) {
          m = ea_data(f->settings, i);
          if (!strncmp(setting, m->name, strlen(setting))) {
            setting = m->name;
            break;
          }
        }
            
        switch (lime_setting_type_get(f, setting)) {
          case MT_INT :
            lime_setting_int_set(f, setting, atoi(cur));
            break;
          case MT_FLOAT :
            lime_setting_float_set(f, setting, atof(cur));
            break;
	  case MT_STRING :
            tmp = strdup(cur);
            if (next_single_colon(tmp))
              *next_single_colon(tmp) = '\0';
            if (strchr(tmp, ','))
              *strchr(tmp, ',') = '\0';
            lime_setting_string_set(f, setting, string_unescape_colon(tmp));
	    printf("set %s to %s\n", setting, string_unescape_colon(tmp));
            free(tmp);
            break;
          default :
            if (lime_setting_type_get(f, setting) == -1)
              printf("setting %s for filter %s not known (filter implementation changed?)\n", setting, f->fc->name);
            else
              printf("FIXME implement type %s settings parsing\n", mt_type_str(lime_setting_type_get(f, setting)));
        }
          
          next = next_single_colon(cur);
          if (next && next+1 < last && (!strchr(cur, ',') || next < strchr(cur, ','))) {
            cur = next+1;
            next = strchr(cur, '=');
          }
          else
            next = NULL;
        
        
      }
      
      if (cur)
        cur = strchr(cur, ',');
      if (cur)
        cur++;
    }
    
    if (cur >= last)
      cur = NULL;
      
  }
  
  free(str);
  
  return filters;
}