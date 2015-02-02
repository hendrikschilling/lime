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
  float c;
  uint8_t lut[256];
} _Data;

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{ 
  int i, j;
  uint8_t *input, *output;
  _Data *data = ea_data(f->data, 0);
  int mult = 1024*data->c;

  assert(in && ea_count(in) == 1);
  assert(out && ea_count(out) == 1);
  
  input = ((Tiledata*)ea_data(in, 0))->data;
  output = ((Tiledata*)ea_data(out, 0))->data;

  
  for(j=0;j<area->height;j++)
    for(i=0;i<area->width;i++) {
	output[j*area->width+i] = data->lut[input[j*area->width+i]];
    }
}

static int _input_fixed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  
  int i;
  double xi, yi, x[4], y[4];

  printf ("#m=0,S=2\n");

  /*for (i = 0; i < 10; i++)
    {
      x[i] = i + 0.5 * sin (i);
      y[i] = i + cos (i * i);
      printf ("%g %g\n", x[i], y[i]);
    }*/
  
  x[0] = 0.0;
  y[0] = 0.0;
  x[3] = 1.0;
  y[3] = 1.0;
  
  x[1] = 0.0995671;
  y[1] = 0.034632;
  
  x[2] = 0.454545;
  y[2] = 0.493506;
  printf ("#m=1,S=0\n");

  {
    gsl_interp_accel *acc 
      = gsl_interp_accel_alloc ();
    gsl_spline *spline 
      = gsl_spline_alloc (gsl_interp_cspline, 4);

    gsl_spline_init (spline, x, y, 4);

    for (xi = x[0]; xi <= x[3]; xi += 1.0/255.0)
      {
        yi = gsl_spline_eval (spline, xi, acc);
        printf ("%.0f %.1f\n", xi*255, 255*yi);
        data->lut[(int)(xi*255.0+0.001)] = yi*255.0+0.001;
      }
      int i;
    for (i = 0; i < 256; i++)
    {
      printf("%d %d\n", i, data->lut[i]);
    }
    gsl_spline_free (spline);
    gsl_interp_accel_free (acc);
  }
    
  return 0;
}

static Filter *_new(void)
{
  Filter *f = filter_new(&filter_core_curves);
  
  Meta *in, *out, *setting, *tune_bitdepth, *tune_color, *bound;
  _Data *data = calloc(sizeof(_Data), 1);
  f->mode_buffer = filter_mode_buffer_new();
  f->mode_buffer->worker = _worker;
  f->mode_buffer->threadsafe = 1;
  f->input_fixed = &_input_fixed;
  ea_push(f->data, data);
  data->c = 4.0;
  f->fixme_outcount = 1;
  
  //tune color-space
  tune_color = meta_new_select(MT_COLOR, f, eina_array_new(3));
  pushint(tune_color->select, CS_INT_RGB);
  pushint(tune_color->select, CS_LAB_L);
  pushint(tune_color->select, CS_YUV_Y);
  pushint(tune_color->select, CS_HSV_V);
  tune_color->replace = tune_color;
  tune_color->dep = tune_color;
  eina_array_push(f->tune, tune_color);
  
  //tune bitdepth
  tune_bitdepth = meta_new_select(MT_BITDEPTH, f, eina_array_new(2));
  pushint(tune_bitdepth->select, BD_U16);
  pushint(tune_bitdepth->select, BD_U8);
  tune_bitdepth->replace = tune_bitdepth;
  tune_bitdepth->dep = tune_bitdepth;
  eina_array_push(f->tune, tune_bitdepth);
  
  //output
  out = meta_new_channel(f, 1);
  meta_attach(out, tune_color);
  meta_attach(out, tune_bitdepth);
  eina_array_push(f->out, out);
  
  //input
  in = meta_new_channel(f, 1);
  meta_attach(in, tune_color);
  meta_attach(in, tune_bitdepth);
  eina_array_push(f->in, in);
  in->replace = out;
  
  //setting
  setting = meta_new_data(MT_FLOAT, f, &data->c);
  meta_name_set(setting, f->fc->shortname);
  eina_array_push(f->settings, setting);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = -10.0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, f, malloc(sizeof(float)));
  *(float*)bound->data = 10.0;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  return f;
}

Filter_Core filter_core_curves = {
  "Curves",
  "curves",
  "Curve Adjustment",
  &_new
};
