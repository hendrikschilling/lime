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

#include "filter_lrdeconv.h"

#include <math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_spline.h>

#include <cv.h>

#include "opencv_helpers.h"

const int bord = 8;

typedef struct {
  float clip, compress, exp;
  int iterations;
  float radius;
  void *blur_b, *estimate_b, *fac_b;
  uint8_t lut[65536];
} _Data;

static void simplegauss(Rect area, uint16_t *in, uint16_t *out, float rad)
{ 
  cv_gauss(area.width, area.height, CV_16UC3, in, out, rad);
}

static void lrdiv(Rect area, uint16_t *observed, uint16_t *blur, uint16_t *out)
{
  int i, j;
  //h blur
  for(j=0;j<area.height;j++)
    for(i=0;i<area.width*3;i++) {
      int pos = j*area.width*3+i;
      if (blur[pos])
        out[pos] = imin((int)observed[pos]*16384/blur[pos], 65535);
      else
        out[pos] = 0;
    }
}

static void lrmul(Rect area, uint16_t *a, uint16_t *b, uint16_t *out)
{
  int i, j;
  //h blur
  for(j=bord/2;j<area.height-bord/2;j++)
    for(i=bord/2;i<area.width*3-3*bord/2;i++) {
      int pos = j*area.width*3+i;
      out[pos] = imin(a[pos]*b[pos]/16384, 65535);
    }
}

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{ 
  int i, j;
  uint16_t *output;
  uint16_t *input;
  _Data *data = ea_data(f->data, 0);
  uint16_t *blur = data->blur_b,
           *estimate = data->estimate_b,
           *fac = data->fac_b;
  Tiledata *in_td;
  Rect in_area;

  assert(in && ea_count(in) == 1);
  assert(out && ea_count(out) == 1);
  
  hack_tiledata_fixsize(6, ea_data(out, 0));
  
  in_td = ((Tiledata*)ea_data(in, 0));
  in_area = in_td->area;
  input = in_td->data;
  output = ((Tiledata*)ea_data(out, 0))->data;
  
  if (area->corner.scale) {
    memcpy(output,input,3*sizeof(uint16_t)*in_td->area.width*in_td->area.height);
    return;
  }
  
  memcpy(estimate, input, 3*sizeof(uint16_t)*in_area.width*in_area.height);
  
  for(i=0;i<data->iterations;i++) {
    simplegauss(in_area, estimate, blur, data->radius);
    lrdiv(in_area, input, blur, fac);
    simplegauss(in_area, fac, fac, data->radius);
    lrmul(in_area, estimate, fac, estimate);
  }
  
  for(j=0;j<area->height;j++)
    for(i=0;i<area->width*3;i++) {
	output[j*area->width*3+i] = estimate[(j+bord)*in_area.width*3+i+3*bord];
    }
}

static double lin2gamma(double lin)
{
  if (lin <= 0.0031308)
    return 12.92*lin;
  else
    return (1+0.055)*pow(lin,1/2.4)-0.055;
}

static void _area_calc(Filter *f, Rect *in, Rect *out)
{ 
  if (in->corner.scale) {
    *out = *in;
  }
  else {
    out->corner.scale = in->corner.scale;
    out->corner.x = in->corner.x-bord;
    out->corner.y = in->corner.y-bord;
    out->width = in->width+2*bord;
    out->height = in->height+2*bord;
  }
}

static int _input_fixed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  
  const int size = 3*sizeof(uint16_t)*(DEFAULT_TILE_SIZE+2*bord)*(DEFAULT_TILE_SIZE+2*bord);
  
  if (!data->blur_b) {
    data->blur_b = malloc(size);
    data->estimate_b = malloc(size);
    data->fac_b = malloc(size);
  }
  
  return 0;
}

static int _del(Filter *f)
{
  int i;
  _Data *data;
  
  for(i=0;i<ea_count(f->data);i++) {
    data = ea_data(f->data, i);
    if (data->blur_b) {
      free(data->blur_b);
      free(data->estimate_b);
      free(data->fac_b);
    }
    free(data);
  }
  
  return 0;
}

static Filter *_new(void)
{
  Filter *f = filter_new(&filter_core_lrdeconv);
  
  Meta *in, *out, *setting, *bd_in, *bd_out, *tune_color, *bound;
  _Data *data = calloc(sizeof(_Data), 1);
  f->mode_buffer = filter_mode_buffer_new();
  f->mode_buffer->worker = _worker;
  f->mode_buffer->area_calc = _area_calc;
  f->mode_buffer->threadsafe = 0;
  f->input_fixed = _input_fixed;
  f->del = _del;
  ea_push(f->data, data);
  f->fixme_outcount = 1;
  
  data->iterations = 30;
  data->radius = 1.0;
  
  //tune color-space
  tune_color = meta_new_select(MT_COLOR, f, eina_array_new(3));
  pushint(tune_color->select, CS_INT_RGB);
  tune_color->replace = tune_color;
  tune_color->dep = tune_color;
  eina_array_push(f->tune, tune_color);
  
  //tune bitdepth
  bd_out = meta_new_select(MT_BITDEPTH, f, eina_array_new(2));
  //pushint(bd_out->select, BD_U16);
  pushint(bd_out->select, BD_U16);
  bd_out->dep = bd_out;
  eina_array_push(f->tune, bd_out);
  
  bd_in = meta_new_select(MT_BITDEPTH, f, eina_array_new(2));
  pushint(bd_in->select, BD_U16);
  //pushint(bd_in->select, BD_U8);
  bd_in->replace = bd_out;
  bd_in->dep = bd_in;
  eina_array_push(f->tune, bd_in);
  
  //output
  out = meta_new_channel(f, 1);
  meta_attach(out, tune_color);
  meta_attach(out, bd_out);
  eina_array_push(f->out, out);
  
  //input
  in = meta_new_channel(f, 1);
  meta_attach(in, tune_color);
  meta_attach(in, bd_in);
  eina_array_push(f->in, in);
  in->replace = out;
  
  //setting
  setting = meta_new_data(MT_FLOAT, f, &data->radius);
  meta_name_set(setting, "radius");
  eina_array_push(f->settings, setting);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 0.0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 5.0;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  //setting
  setting = meta_new_data(MT_FLOAT, f, &data->compress);
  meta_name_set(setting, "highlight compression");
  eina_array_push(f->settings, setting);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 0.0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 1.0;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  //setting
  setting = meta_new_data(MT_INT, f, &data->iterations);
  meta_name_set(setting, "iterations");
  eina_array_push(f->settings, setting);
  
  bound = meta_new_data(MT_INT, f, malloc(sizeof(int)));
  *(int*)bound->data = 1;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_INT, f, malloc(sizeof(int)));
  *(int*)bound->data = 100;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  return f;
}

Filter_Core filter_core_lrdeconv = {
  "Lucy–Richardson Deconvolution",
  "deconvolution",
  "Deconvolve/sharpen using Lucy–Richardson Deconvolution",
  &_new
};
