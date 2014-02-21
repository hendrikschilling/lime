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

#include "filter_downscale.h"

typedef struct {
  Meta *color[3];
  int colorspace;
} _Data;

static void _area_calc(Filter *f, Rect *in, Rect *out)
{
  if (in->corner.scale) {
    out->corner.scale = in->corner.scale-1;
    out->corner.x = in->corner.x*2;
    out->corner.y = in->corner.y*2;
    out->width = in->width*2;
    out->height = in->height*2;
  }
  else {
    *out = *in;
  }
}

static uint8_t *tileptr8(Tiledata *tile, int x, int y)
{ 
  return &((uint8_t*)tile->data)[(y-tile->area->corner.y)*tile->area->width + x-tile->area->corner.x];
}

static void _worker_gamma(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int ch;
  int i, j;
  Tiledata *in_td, *out_td;
  Rect *in_area;
  
  assert(in && ea_count(in) == 3);
  assert(out && ea_count(out) == 3);
  
  in_area = ((Tiledata*)ea_data(in, 0))->area;
  
  if (area->corner.scale)
    for(ch=0;ch<3;ch++) {
      in_td = (Tiledata*)ea_data(in, ch);
      out_td = (Tiledata*)ea_data(out, ch);
      
      for(j=0;j<DEFAULT_TILE_SIZE*2;j+=2)
	for(i=0;i<DEFAULT_TILE_SIZE*2;i+=2)
	  tileptr8(out_td, i/2+area->corner.x, j/2+area->corner.y)[0] = lime_l2g[
	  (lime_g2l[tileptr8(in_td, i+in_area->corner.x, j+in_area->corner.y)[0]]
	  +lime_g2l[tileptr8(in_td, i+1+in_area->corner.x, j+in_area->corner.y)[0]]
	  +lime_g2l[tileptr8(in_td, i+in_area->corner.x, j+1+in_area->corner.y)[0]]
	  +lime_g2l[tileptr8(in_td, i+1+in_area->corner.x, j+1+in_area->corner.y)[0]]
	  +2
	  ) / 4];
    }
    else
      for(ch=0;ch<3;ch++) {
	in_td = (Tiledata*)ea_data(in, ch);
	out_td = (Tiledata*)ea_data(out, ch);
	assert(in_td->area->width == out_td->area->width);
	assert(in_td->area->height == out_td->area->height);
	memcpy(out_td->data, in_td->data, out_td->area->width*out_td->area->height);
      }
      
}


static void _worker_linear(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int ch;
  int i, j;
  Tiledata *in_td, *out_td;
  Rect *in_area;
  
  assert(in && ea_count(in) == 3);
  assert(out && ea_count(out) == 3);
  
  in_area = ((Tiledata*)ea_data(in, 0))->area;
  
  if (area->corner.scale)
    for(ch=0;ch<3;ch++) {
      in_td = (Tiledata*)ea_data(in, ch);
      out_td = (Tiledata*)ea_data(out, ch);
      
      for(j=0;j<DEFAULT_TILE_SIZE*2;j+=2)
	for(i=0;i<DEFAULT_TILE_SIZE*2;i+=2)
	  tileptr8(out_td, i/2+area->corner.x, j/2+area->corner.y)[0] = 
	  (tileptr8(in_td, i+in_area->corner.x, j+in_area->corner.y)[0]
	  +tileptr8(in_td, i+1+in_area->corner.x, j+in_area->corner.y)[0]
	  +tileptr8(in_td, i+in_area->corner.x, j+1+in_area->corner.y)[0]
	  +tileptr8(in_td, i+1+in_area->corner.x, j+1+in_area->corner.y)[0]
	  +2
	  ) / 4;
    }
    else
      for(ch=0;ch<3;ch++) {
	in_td = (Tiledata*)ea_data(in, ch);
	out_td = (Tiledata*)ea_data(out, ch);
	assert(in_td->area->width == out_td->area->width);
	assert(in_td->area->height == out_td->area->height);
	memcpy(out_td->data, in_td->data, out_td->area->width*out_td->area->height);
      }
      
}

static int _setting_changed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  
  switch (data->colorspace) {
    case 0 : 
      *(int*)(data->color[0]->data) = CS_RGB_R;
      *(int*)(data->color[1]->data) = CS_RGB_G;
      *(int*)(data->color[2]->data) = CS_RGB_B;
      f->mode_buffer->worker = &_worker_gamma;
      break;
    case 1 : 
      *(int*)(data->color[0]->data) = CS_LAB_L;
      *(int*)(data->color[1]->data) = CS_LAB_A;
      *(int*)(data->color[2]->data) = CS_LAB_B;
      f->mode_buffer->worker = &_worker_linear;
      break;
    case 2 : 
      *(int*)(data->color[0]->data) = CS_YUV_Y;
      *(int*)(data->color[1]->data) = CS_YUV_U;
      *(int*)(data->color[2]->data) = CS_YUV_V;
      f->mode_buffer->worker = &_worker_linear;
      break;
    default :
      abort();    
  }
  
  return 0;
}

Filter *filter_down_new(void)
{
  Filter *filter = filter_new(&filter_core_down);
  Meta *in, *out, *channel, *bitdepth, *setting, *bound;
  Meta *ch_out[3];
  _Data *data = calloc(sizeof(_Data), 1);
  filter->fixme_outcount = 3;
  ea_push(filter->data, data);
  
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = &_worker_linear;
  filter->mode_buffer->area_calc = &_area_calc;
  filter->setting_changed = &_setting_changed;
  
  bitdepth = meta_new_data(MT_BITDEPTH, filter, malloc(sizeof(int)));
  *(int*)(bitdepth->data) = BD_U8;
  bitdepth->replace = bitdepth;
  
  out = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->out, out);
  
  channel = meta_new_channel(filter, 1);
  data->color[0] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color[0]->data) = CS_LAB_L;
  meta_attach(channel, data->color[0]);
  meta_attach(channel, bitdepth);
  meta_attach(out, channel);
  ch_out[0] = channel;
  
  channel = meta_new_channel(filter, 2);
  data->color[1] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color[1]->data) = CS_LAB_A;
  meta_attach(channel, data->color[1]);
  meta_attach(channel, bitdepth);
  meta_attach(out, channel);
  ch_out[1] = channel;
  
  channel = meta_new_channel(filter, 3);
  data->color[2] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color[2]->data) = CS_LAB_B;
  meta_attach(channel, data->color[2]);
  meta_attach(channel, bitdepth);
  meta_attach(out, channel);
  ch_out[2] = channel;
  
  in = meta_new(MT_BUNDLE, filter);
  in->replace = out;
  eina_array_push(filter->in, in);
  
  channel = meta_new_channel(filter, 1);
  data->color[0]->replace = data->color[0];
  channel->replace = ch_out[0];
  meta_attach(channel, data->color[0]);
  meta_attach(channel, bitdepth);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 2);
  data->color[1]->replace = data->color[1];
  channel->replace = ch_out[1];
  meta_attach(channel, data->color[1]);
  meta_attach(channel, bitdepth);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 3);
  data->color[2]->replace = data->color[2];
  channel->replace = ch_out[2];
  meta_attach(channel, data->color[2]);
  meta_attach(channel, bitdepth);
  meta_attach(in, channel);
    
  setting = meta_new_data(MT_INT, filter, &data->colorspace);
  meta_name_set(setting, "colorspace");
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

Filter_Core filter_core_down = {
  "Downscaling filter",
  "down",
  "uses input one scale higher than its output to calculate it",
  &filter_down_new
};