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

#include "filter_curves.h"

#include <math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_spline.h>


typedef struct {
  float clip, compress, exp;
  float x1,x2,y1,y2;
  uint8_t *lut;
} _Data;

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{ 
  int i, j;
  uint8_t *output;
  uint16_t *input;
  _Data *data = ea_data(f->data, 0);

  assert(in && ea_count(in) == 1);
  assert(out && ea_count(out) == 1);
  
  hack_tiledata_fixsize(3, ea_data(out, 0));
  
  input = ((Tiledata*)ea_data(in, 0))->data;
  output = ((Tiledata*)ea_data(out, 0))->data;

  
  for(j=0;j<area->height;j++)
    for(i=0;i<area->width*3;i++) {
	output[j*area->width*3+i] = data->lut[input[j*area->width*3+i]];
    }
}

static double lin2gamma(double lin)
{
  if (lin <= 0.0031308)
    return 12.92*lin;
  else
    return (1+0.055)*pow(lin,1/2.4)-0.055;
}

static int _prepare(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  gsl_spline *spline_exp;
  int i;
  double ye, yc, x[4], y[4], ex[4], ey[4];  
  double exp;
  double clip;
  double compress;
  
  if (!data->lut)
    data->lut = malloc(65536);
  
  exp = pow(2.0, data->exp);
  compress = data->compress;
  clip = data->clip;
  
  x[0] = 0.0;
  y[0] = 0.0;
  x[3] = 1.0;
  y[3] = 1.0;
  
  /*x[1] = 0.0995671;
  y[1] = 0.034632;
  
  x[2] = 0.454545;
  y[2] = 0.493506;*/
  
  if (data->x1 == data->x2 || data->x1 == 0.0 || data->x1 == 1.0 || data->x2 == 0.0 || data->x2 == 1.0) {
    //TODO handle degenerate case more gracefully?
    x[1] = 0.25;
    y[1] = 0.25;
    x[2] = 0.75;
    y[2] = 0.75;
  }
  else {
    if (data->x2 > data->x1) {
      x[1] = data->x1;
      y[1] = data->y1;
      x[2] = data->x2;
      y[2] = data->y2;
    }
    else {
      x[2] = data->x1;
      y[2] = data->y1;
      x[1] = data->x2;
      y[1] = data->y2;
    }
  }
  
  ex[0] = 0.0;
  ey[0] = 0.0;
  ex[1] = 1.0/exp*(1.0-compress);
  ey[1] = 1.0-compress;
  ex[2] = ex[1]+(1.0-1.0/exp)*(1.0-clip);
  ey[2] = 1.0;
  ex[3] = 1.0;
  ey[3] = 1.0;

  gsl_interp_accel *acc = gsl_interp_accel_alloc();
  gsl_spline *spline = gsl_spline_alloc(gsl_interp_cspline, 4);
  gsl_spline_init(spline, x, y, 4);
  
  gsl_interp_accel *acc_exp = gsl_interp_accel_alloc();
  if (exp <= 1.0) {
    ex[1] = 1.0;
    ey[1] = exp;
    spline_exp = gsl_spline_alloc(gsl_interp_linear, 2);
    gsl_spline_init(spline_exp, ex, ey, 2);
  }
  else if (compress == 1.0) {
    ex[1] = 1.0;
    ey[1] = 1.0;
    spline_exp = gsl_spline_alloc(gsl_interp_linear, 2);
    gsl_spline_init(spline_exp, ex, ey, 2);
  }
  else if (clip == 1.0) {
    ex[1] = 1.0/exp;
    ey[1] = 1.0;
    ex[2] = 1.0;
    ey[2] = 1.0;
    spline_exp = gsl_spline_alloc(gsl_interp_linear, 3);
    gsl_spline_init(spline_exp, ex, ey, 3);
  }
  else if (clip == 0.0) {
    ex[2] = ex[3];
    ey[2] = ey[3];
    spline_exp = gsl_spline_alloc(gsl_interp_linear, 3);
    gsl_spline_init(spline_exp, ex, ey, 3);
  }
  else {
    spline_exp = gsl_spline_alloc(gsl_interp_linear, 4);
    gsl_spline_init(spline_exp, ex, ey, 4);
  }
  
  for (i = 0; i < 65536; i++)
  {
    ye = lin2gamma(gsl_spline_eval(spline_exp, i*(1.0/65536.0), acc_exp));
    //ye = lin2gamma(i*(1.0/65536.0));
    yc = gsl_spline_eval(spline, ye, acc);
    data->lut[i] = imin((int)(yc*256.0),255);
  }
  gsl_spline_free (spline);
  gsl_interp_accel_free (acc);
    
  return 0;
}


static int _del(Filter *f)
{  
  _Data *data = ea_data(f->data, 0);
  
  if (data->lut)
    free(data->lut);
  free(data);
  
  return 0;
}

static Filter *_new(void)
{
  Filter *f = filter_new(&filter_core_curves);
  
  Meta *in, *out, *setting, *bd_in, *bd_out, *tune_color, *bound;
  _Data *data = calloc(sizeof(_Data), 1);
  f->mode_buffer = filter_mode_buffer_new();
  f->mode_buffer->worker = _worker;
  f->mode_buffer->threadsafe = 1;
  f->prepare = &_prepare;
  f->del = &_del;
  ea_push(f->data, data);
  f->fixme_outcount = 1;
  
  //tune color-space
  tune_color = meta_new_select(MT_COLOR, f, eina_array_new(3));
  pushint(tune_color->select, CS_INT_RGB);
  tune_color->replace = tune_color;
  tune_color->dep = tune_color;
  eina_array_push(f->tune, tune_color);
  
  //tune bitdepth
  bd_out = meta_new_select(MT_BITDEPTH, f, eina_array_new(2));
  //pushint(bd_out->select, BD_U16);
  pushint(bd_out->select, BD_U8);
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
  setting = meta_new_data(MT_FLOAT, f, &data->exp);
  data->exp = 0.0;
  meta_name_set(setting, "exposure compensation");
  eina_array_push(f->settings, setting);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = -20.0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 20.0;
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
  setting = meta_new_data(MT_FLOAT, f, &data->clip);
  meta_name_set(setting, "highlight clipping");
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
  setting = meta_new_data(MT_FLOAT, f, &data->x1);
  meta_name_set(setting, "x1");
  eina_array_push(f->settings, setting);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 0.0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 1.0;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 0.01;
  meta_name_set(bound, "PARENT_SETTING_STEP");
  meta_attach(setting, bound);
  
  //setting
  setting = meta_new_data(MT_FLOAT, f, &data->y1);
  meta_name_set(setting, "y1");
  eina_array_push(f->settings, setting);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 0.0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 1.0;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 0.01;
  meta_name_set(bound, "PARENT_SETTING_STEP");
  meta_attach(setting, bound);
  
  //setting
  setting = meta_new_data(MT_FLOAT, f, &data->x2);
  meta_name_set(setting, "x2");
  eina_array_push(f->settings, setting);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 0.0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 1.0;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 0.01;
  meta_name_set(bound, "PARENT_SETTING_STEP");
  meta_attach(setting, bound);
  
  //setting
  setting = meta_new_data(MT_FLOAT, f, &data->y2);
  meta_name_set(setting, "y2");
  eina_array_push(f->settings, setting);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 0.0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 1.0;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 0.01;
  meta_name_set(bound, "PARENT_SETTING_STEP");
  meta_attach(setting, bound);
  
  return f;
}

Filter_Core filter_core_curves = {
  "Curves",
  "curves",
  "Curve Adjustment",
  &_new
};
