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

#include "filter_interleave.h"

typedef struct {
  Meta *colorspace;
  Eina_Array *select_color;
} _Data;

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int i, j;
  uint8_t *buf, *r, *g, *b;
  int *buf_int, *buf_max;
  _Data *data = ea_data(f->data, 0);
  
  assert(in && ea_count(in) == 3);
  
  r = ((Tiledata*)ea_data(in, 0))->data;
  g = ((Tiledata*)ea_data(in, 1))->data;
  b = ((Tiledata*)ea_data(in, 2))->data;
  
  area = &((Tiledata*)ea_data(in, 0))->area;
  
  if (*(int*)data->colorspace->data == CS_INT_ABGR) {
    hack_tiledata_fixsize(4, ea_data(out, 0));
    buf_int = ((Tiledata*)ea_data(out, 0))->data;
    buf_max = (int*)((Tiledata*)ea_data(out, 0))->data+area->width*area->height;
    for(;buf_int<buf_max;buf_int++)
      *buf_int = (255 << 24) | (*(r++) << 16) | (*(g++) << 8) | (*(b++));
  }
  else {
    hack_tiledata_fixsize(3, ea_data(out, 0));
    buf = ((Tiledata*)ea_data(out, 0))->data;
    
    for(j=0;j<area->height;j++)
      for(i=0;i<area->width;i++) {
	buf[(j*area->width+i)*3+0] = r[j*area->width+i];
	buf[(j*area->width+i)*3+1] = g[j*area->width+i];
	buf[(j*area->width+i)*3+2] = b[j*area->width+i];
      }
  }
}


static int _del(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  
  eina_array_free(data->select_color);
  free(data);
  
  return 0;
}

Filter *filter_interleave_new(void)
{
  Filter *filter = filter_new(&filter_core_interleave);
  Meta *in, *out, *channel, *color, *bitdepth;
  Meta *ch_out, *tune_color;
  _Data *data = calloc(sizeof(_Data), 1);
  ea_push(filter->data, data);
  
  filter->del = &_del;
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->worker = &_worker;
  //filter->mode_buffer->area_calc = &_area_calc;
  filter->fixme_outcount = 1;
  
  data->select_color = eina_array_new(2);
  pushint(data->select_color, CS_INT_ABGR);
  pushint(data->select_color, CS_INT_RGB);
  
  bitdepth = meta_new_data(MT_BITDEPTH, filter, malloc(sizeof(int)));
  *(int*)(bitdepth->data) = BD_U8;
  bitdepth->replace = bitdepth;
  
  out = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->out, out);
  
  tune_color = meta_new_select(MT_COLOR, filter, data->select_color);
  meta_name_set(tune_color, "Output Colorspace");
  tune_color->dep = tune_color;
  data->colorspace = tune_color;
  
  eina_array_push(filter->tune, tune_color);
  
  ch_out = meta_new_channel(filter, 1);
  meta_attach(ch_out, tune_color);
  meta_attach(ch_out, bitdepth);
  meta_attach(out, ch_out);
  
  in = meta_new(MT_BUNDLE, filter);
  in->replace = out;
  eina_array_push(filter->in, in);
  
  channel = meta_new_channel(filter, 1);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_R;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(in, channel);
  channel->replace = ch_out;
  color->replace = tune_color;
  
  channel = meta_new_channel(filter, 2);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_G;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(in, channel);
  channel->replace = ch_out;
  color->replace = tune_color;
  
  channel = meta_new_channel(filter, 3);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_B;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(in, channel);
  channel->replace = ch_out;
  color->replace = tune_color;
  
  return filter;
}

Filter_Core filter_core_interleave = {
  "interleave",
  "interleave",
  "converts from planar to packed/interleaved format",
  &filter_interleave_new
};