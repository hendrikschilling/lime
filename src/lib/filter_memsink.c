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

#include "filter_memsink.h"

typedef struct {
  uint8_t *data;
  int *use_alpha;
} _Data;

void *_memsink_data_new(Filter *f, void *data)
{
  _Data *new = calloc(sizeof(_Data), 1);
  
  new->use_alpha = ((_Data*)data)->use_alpha;
  
  return new;
}

void filter_memsink_buffer_set(Filter *f, uint8_t *raw_data, int thread_id)
{
    _Data *data;
    
    filter_fill_thread_data(f, thread_id);
    
    data = ea_data(f->data, thread_id);
    
    data->data = raw_data;
}

static void _memsink_worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int i, j;
  uint8_t *buf, *rgb;
  _Data *data = ea_data(f->data, thread_id);
  buf = data->data;
  
  assert(in && ea_count(in) == 1);
  
  if (!buf)
    return;
  
  rgb = ((Tiledata*)ea_data(in, 0))->data;
  
  area = &((Tiledata*)ea_data(in, 0))->area;
  
  if (*data->use_alpha)
    memcpy(buf, rgb, 4*area->width*area->height);
  else
    for(j=0;j<area->height;j++)
      for(i=0;i<area->width;i++) {
        abort();
      }
}

static int _del(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  int i;
  
  free(data->use_alpha);
  
  for(i=0;i<ea_count(f->data);i++) {
    data = ea_data(f->data, i);
    free(data);
  }
  
  return 0;
}

Filter *filter_memsink_new(void)
{
  Filter *filter = filter_new(&filter_core_memsink);
  Meta *in, *channel, *bitdepth, *color, *size, *setting, *bound, *fliprot;
  _Data *data = calloc(sizeof(_Data), 1);
  data->use_alpha = malloc(sizeof(int));
  *data->use_alpha = 0;
  ea_push(filter->data, data);
  
  filter->del = &_del;
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = &_memsink_worker;
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->data_new = &_memsink_data_new;
  
  bitdepth = meta_new_data(MT_BITDEPTH, filter, malloc(sizeof(int)));
  *(int*)(bitdepth->data) = BD_U8;
  
  size = meta_new(MT_IMGSIZE, filter);
  ea_push(filter->core, size);
  
  in = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->in, in);
  
  fliprot = meta_new_data(MT_FLIPROT, filter, malloc(sizeof(int)));
  *(int*)fliprot->data = 1;
  meta_attach(in, fliprot);
  
  channel = meta_new_channel(filter, 1);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_INT_ABGR;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size);
  meta_attach(in, channel);
  
  setting = meta_new_data(MT_INT, filter, data->use_alpha);
  meta_name_set(setting, "add alpha");
  eina_array_push(filter->settings, setting);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 1;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  return filter;
}

Filter_Core filter_core_memsink = {
  "Memory sink",
  "memsink",
  "Stores input in application provided buffer",
  &filter_memsink_new
};