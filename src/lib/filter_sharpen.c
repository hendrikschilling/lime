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

#include "filter_sharpen.h"

#include <math.h>

typedef struct {
  float val;
} _Data;

static void _area_calc(Filter *f, Rect *in, Rect *out)
{ 
  _Data *data = ea_data(f->data, 0);
  float s = data->val/100.0/pow(2.0, 2.45*in->corner.scale);
  
  if (s < 0.1) {
    *out = *in;
  }
  else {
    out->corner.scale = in->corner.scale;
    out->corner.x = in->corner.x-1;
    out->corner.y = in->corner.y-1;
    out->width = in->width+2;
    out->height = in->height+2;
  }
}

static uint8_t *tileptr8(Tiledata *tile, int x, int y)
{ 
  return &((uint8_t*)tile->data)[(y-tile->area->corner.y)*tile->area->width + x-tile->area->corner.x];
}

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int ch;
  int i, j;
  Tiledata *in_td, *out_td;
  uint8_t *buf_out;
  uint8_t *buf_in1, *buf_in2, *buf_in3;
  _Data *data = ea_data(f->data, 0);
  float s = data->val/100.0/pow(2.0, 2.45*area->corner.scale);
  
  /*
   red.factor with max diff at scale 1:
   no reduction:  108
   1.0   109   96   53   37    0    0
   1.5   89   54   22   11    0    0
   2.0   64   38   20   10    0    0
   2.25  58   35   20   10    0    0
   2.4   56   35   20   10    0    0
   2.45  55   36   20   10    0    0  1xss 0   36   19   10    0    0  2x ss 0    1   15   10    0    0
   2.5   55   37   20   10    0    0
   2.55  56   38   20   10    0    0
   2.6   58   39   20   10    0    0
   2.75  63   41   20   10    0    0
   3.0   70   44   20   10    0    0
   */
  
  assert(in && ea_count(in) == 3);
  assert(out && ea_count(out) == 3);

  for(ch=0;ch<3;ch++) {
    in_td = (Tiledata*)ea_data(in, ch);
    out_td = (Tiledata*)ea_data(out, ch);
    
    if (s < 0.1) {
      assert(in_td->area->corner.x == area->corner.x);
      assert(in_td->area->corner.y == area->corner.y);
      assert(in_td->area->width == area->width);
      assert(in_td->area->height == area->height);
      
      memcpy(out_td->data, in_td->data, DEFAULT_TILE_SIZE*DEFAULT_TILE_SIZE);
    }
    else
      for(j=0;j<DEFAULT_TILE_SIZE;j++) {
	buf_out = tileptr8(out_td, area->corner.x, area->corner.y+j);
	buf_in1 = tileptr8(in_td, area->corner.x, area->corner.y+j-1);
	buf_in2 = tileptr8(in_td, area->corner.x, area->corner.y+j);
	buf_in3 = tileptr8(in_td, area->corner.x, area->corner.y+j+1);
	for(i=0;i<DEFAULT_TILE_SIZE;i++) {
	  *buf_out =  clip_u8(((1.0+4*s)*buf_in2[0] - s*(buf_in1[0] + buf_in2[-1] + buf_in2[1] + buf_in3[0])));
	  buf_out++;
	  buf_in1++;
	  buf_in2++;
	  buf_in3++;
	}
      }
  }

}


Filter *filter_sharpen_new(void)
{
  Filter *filter = filter_new(&filter_core_sharpen);
  Meta *in, *out, *channel, *color[3], *setting;
  Meta *ch_out[3];
  _Data *data = malloc(sizeof(_Data));
  data->val = 100.0;

  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->worker = &_worker;
  filter->mode_buffer->area_calc = &_area_calc;
  filter->fixme_outcount = 3;
  ea_push(filter->data, data);
  
  out = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->out, out);
  
  channel = meta_new_channel(filter, 1);
  color[0] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[0]->data) = CS_YUV_Y;
  meta_attach(channel, color[0]);
  meta_attach(out, channel);
  ch_out[0] = channel;
  
  channel = meta_new_channel(filter, 2);
  color[1] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[1]->data) = CS_YUV_U;
  meta_attach(channel, color[1]);
  meta_attach(out, channel);
  ch_out[1] = channel;
  
  channel = meta_new_channel(filter, 3);
  color[2] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[2]->data) = CS_YUV_V;
  meta_attach(channel, color[2]);
  meta_attach(out, channel);
  ch_out[2] = channel;
  
  in = meta_new(MT_BUNDLE, filter);
  in->replace = out;
  eina_array_push(filter->in, in);
  
  channel = meta_new_channel(filter, 1);
  color[0]->replace = color[0];
  channel->replace = ch_out[0];
  meta_attach(channel, color[0]);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 2);
  color[1]->replace = color[1];
  channel->replace = ch_out[1];
  meta_attach(channel, color[1]);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 3);
  color[2]->replace = color[2];
  color[2]->replace = color[2];
  channel->replace = ch_out[2];
  meta_attach(channel, color[2]);
  meta_attach(in, channel);
  
  setting = meta_new_data(MT_FLOAT, filter, &data->val);
  meta_name_set(setting, "strength");
  eina_array_push(filter->settings, setting);
  
  return filter;
}

Filter_Core filter_core_sharpen = {
  "Sharpen",
  "sharpen",
  "sharpen image",
  &filter_sharpen_new
};