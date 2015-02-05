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

#include "filter_gauss.h"
#include "opencv_helpers.h"

#include "math.h"

#define MAX_SIGMA 200
#define BLUR_MAX ((int)sqrt(MAX_SIGMA*MAX_SIGMA*12.0/3.0+1.0)+1)
#define BLURBUF ((DEFAULT_TILE_SIZE+2*BLUR_MAX*3)*(DEFAULT_TILE_SIZE+2*BLUR_MAX*3))
#define MULTIPLIER 1048576


typedef struct {
  float *sigma;
  uint8_t *buf1;
  uint8_t *buf2;
} _Data;

uint32_t r_calc(float sigma, int scale)
{
  double r;
  
  r = sqrt(sigma*sigma*12.0/3.0+1.0)*16.0/(pow(2.0,scale))-2*scale;
  
  if (r < 0)
    r = 0;
  
  /*switch (scale) {
    case 1 : r = 73; break;
    case 2 : r = 35; break;
    case 3 : r = 11; break;
    case 4 : r = 5; break;
  }*/
  
  return r;
}

void *_gauss_data_new(Filter *f, void *data)
{
  _Data *newdata = calloc(sizeof(_Data), 1);
  
  *newdata = *(_Data*)data;
  
  newdata->buf1 = malloc(BLURBUF);
  newdata->buf2 = malloc(BLURBUF);
  
  return newdata;
}

static void _area_calc(Filter *f, Rect *in, Rect *out)
{
  _Data *data = ea_data(f->data, 0);
  uint32_t render_r, actual_r;
  
  actual_r = r_calc(*data->sigma, in->corner.scale)/16;
  
  render_r = actual_r+1;
  
  out->corner.scale = in->corner.scale;
  out->corner.x = in->corner.x - render_r*3;
  out->corner.y = in->corner.y - render_r*3;
  out->width = in->width + 2*render_r*3;
  out->height = in->height + 2*render_r*3;
}

static void *tileptr8(Tiledata *tile, int x, int y)
{ 
  return &((uint8_t*)tile->data)[(y-tile->area.corner.y)*tile->area.width + x-tile->area.corner.x];
}

void _accu_blur(int x, int y, Tiledata *in, Tiledata *out, int rad, int extra, int in_step, int out_step, uint32_t frac)
{
  int i;
  int r;
  int acc;
  uint8_t *in_ptr, *out_ptr, *in_ptr_oneless, *in_ptr_next;
  uint32_t inv;
  uint32_t add_round = 0;//((2*rad+1)*16+2*frac)/2;
  
  inv = (MULTIPLIER+add_round) / ((2*rad+1)*16+2*frac);
  
  
  in_ptr = tileptr8(in, x, y)-rad*in_step;

  acc = 0;
  for(r=0;r<2*rad+1;r++) {
    acc += in_ptr[0];
    in_ptr += in_step;
  }
  
  in_ptr = tileptr8(in, x, y)-rad*in_step;
  out_ptr = tileptr8(out, x, y);
  in_ptr_oneless = in_ptr - in_step;
  in_ptr_next = in_ptr + (2*rad+1)*in_step;
  for(i=0;i<DEFAULT_TILE_SIZE+2*extra;i++) {
    out_ptr[0] = ((in_ptr_oneless[0] + in_ptr_next[0])*frac + acc*16)*inv / MULTIPLIER;
    acc += in_ptr_next[0];
    acc -= in_ptr[0];
    in_ptr  += in_step;
    in_ptr_next  += in_step;
    in_ptr_oneless  += in_step;
    out_ptr += out_step;
  }
}

void _accu_blur_x(int x, int y, Tiledata *in, Tiledata *out, int rad, int extra, uint32_t frac)
{
  int i;
  int r;
  int acc;
  uint8_t *in_ptr, *out_ptr, *in_ptr_oneless, *in_ptr_next;
  uint32_t inv;
  uint32_t add_round = ((2*rad+1)*16+2*frac + 1);
  
  inv = (MULTIPLIER+add_round) / ((2*rad+1)*16+2*frac);
  
  
  in_ptr = tileptr8(in, x, y)-rad;

  acc = 0;
  for(r=0;r<2*rad+1;r++) {
    acc += in_ptr[0];
    in_ptr ++;
  }
  
  in_ptr = tileptr8(in, x, y)-rad;
  out_ptr = tileptr8(out, x, y);
  in_ptr_oneless = in_ptr - 1;
  in_ptr_next = in_ptr + (2*rad+1);
  for(i=0;i<DEFAULT_TILE_SIZE+2*extra;i++) {
    out_ptr[0] = ((in_ptr_oneless[0] + in_ptr_next[0])*frac + acc*16)*inv / MULTIPLIER;
    acc += in_ptr_next[0];
    acc -= in_ptr[0];
    in_ptr  ++;
    in_ptr_next++;
    in_ptr_oneless++;
    out_ptr ++;
  }
}

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  _Data *data = ea_data(f->data, thread_id);
  uint32_t render_r, frac, actual_r;
  int ch;
  int i, j;
  int x, y;
  Rect *in_area;
  
  actual_r = r_calc(*data->sigma, area->corner.scale);
  
  frac = actual_r % 16;
  actual_r = actual_r / 16;
  
  render_r = actual_r+1;
  
  Tiledata buf_t1;
  Tiledata buf_t2;
  Rect buf_area;
  
  _area_calc(f, area, &buf_area);
  
  buf_t1.area = buf_area;
  buf_t2.area = buf_area;
  buf_t1.data = data->buf1;
  buf_t2.data = data->buf2;
   
  assert(in && ea_count(in) == 3);
  assert(out && ea_count(out) == 3);

  in_area = &((Tiledata*)ea_data(in, 0))->area;
    
  for(ch=0;ch<3;ch++) {
    if (!render_r) {
      assert(area->corner.x == in_area->corner.x);
      
      memcpy(((Tiledata*)ea_data(out, ch))->data,
	     ((Tiledata*)ea_data(in, ch))->data,
	     DEFAULT_TILE_AREA);
    }
    else {
    for(i=0;i<(DEFAULT_TILE_SIZE+2*render_r*3);i++) {
      x = i+area->corner.x-render_r*3;
      y = area->corner.y-2*render_r;
      _accu_blur(x, y, ea_data(in, ch), &buf_t1, actual_r, 2*render_r, in_area->width, buf_area.width, frac);
    }
    for(i=0;i<(DEFAULT_TILE_SIZE+2*render_r*3);i++) {
      x = i+area->corner.x-render_r*3;
      y = area->corner.y-render_r;
      _accu_blur(x, y, &buf_t1, &buf_t2, actual_r, render_r, buf_area.width, buf_area.width, frac);
    }
    for(i=0;i<(DEFAULT_TILE_SIZE+2*actual_r*3);i++) {
      x = i+area->corner.x-actual_r*3;
      y = area->corner.y;
      _accu_blur(x, y, &buf_t2, &buf_t1, actual_r, 0, buf_area.width, buf_area.width, frac);
    }
      
    for(j=0;j<DEFAULT_TILE_SIZE;j++) {
      x = area->corner.x-2*render_r;
      y = j+area->corner.y;
      
      _accu_blur_x(x, y, &buf_t1, &buf_t2, actual_r, 2*render_r, frac);
    }
    for(j=0;j<DEFAULT_TILE_SIZE;j++) {
      x = area->corner.x-render_r;
      y = j+area->corner.y;
      
      _accu_blur_x(x, y, &buf_t2, &buf_t1, actual_r, render_r, frac);
    }
    for(j=0;j<DEFAULT_TILE_SIZE;j++) {
      x = area->corner.x;
      y = j+area->corner.y;
      
      _accu_blur_x(x, y, &buf_t1, ea_data(out, ch), actual_r, 0, frac);
    }
    }
  }

}

Filter *filter_gauss_blur_new(void)
{
  Filter *filter = filter_new(&filter_core_gauss);
  Meta *in, *out, *channel, *bitdepth, *color[3], *setting, *bound;
  Meta *ch_out[3];
  _Data *data = calloc(sizeof(_Data), 1);
  data->sigma = calloc(sizeof(float), 1);
  data->buf1 = malloc(BLURBUF);
  data->buf2 = malloc(BLURBUF);
  filter->fixme_outcount = 3;
  ea_push(filter->data, data);
  
  *data->sigma = 5.0;
  
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = &_worker;
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->area_calc = &_area_calc;
  filter->mode_buffer->data_new = &_gauss_data_new;
  
  bitdepth = meta_new_data(MT_BITDEPTH, filter, malloc(sizeof(int)));
  *(int*)(bitdepth->data) = BD_U8;
  bitdepth->replace = bitdepth;
  
  out = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->out, out);
  
  channel = meta_new_channel(filter, 1);
  color[0] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[0]->data) = CS_LAB_L;
  meta_attach(channel, color[0]);
  meta_attach(channel, bitdepth);
  meta_attach(out, channel);
  ch_out[0] = channel;
  
  channel = meta_new_channel(filter, 2);
  color[1] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[1]->data) = CS_LAB_A;
  meta_attach(channel, color[1]);
  meta_attach(channel, bitdepth);
  meta_attach(out, channel);
  ch_out[1] = channel;
  
  channel = meta_new_channel(filter, 3);
  color[2] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[2]->data) = CS_LAB_B;
  meta_attach(channel, color[2]);
  meta_attach(channel, bitdepth);
  meta_attach(out, channel);
  ch_out[2] = channel;
  
  in = meta_new(MT_BUNDLE, filter);
  in->replace = out;
  eina_array_push(filter->in, in);
  
  channel = meta_new_channel(filter, 1);
  color[0]->replace = color[0];
  channel->replace = ch_out[0];
  meta_attach(channel, color[0]);
  meta_attach(channel, bitdepth);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 2);
  color[1]->replace = color[1];
  channel->replace = ch_out[1];
  meta_attach(channel, color[1]);
  meta_attach(channel, bitdepth);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 3);
  color[2]->replace = color[2];
  color[2]->replace = color[2];
  channel->replace = ch_out[2];
  meta_attach(channel, color[2]);
  meta_attach(channel, bitdepth);
  meta_attach(in, channel);
    
  setting = meta_new_data(MT_FLOAT, filter, data->sigma);
  meta_name_set(setting, "sigma");
  eina_array_push(filter->settings, setting);
  
  bound = meta_new_data(MT_FLOAT, filter, malloc(sizeof(int)));
  *(float*)bound->data = 0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, filter, malloc(sizeof(int)));
  *(float*)bound->data = MAX_SIGMA;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  return filter;
}

Filter_Core filter_core_gauss = {
  "Gaussian blur",
  "gauss",
  "Gaussian blur filter",
  &filter_gauss_blur_new
};
