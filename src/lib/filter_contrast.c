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

#include "filter_contrast.h"

typedef struct {
  float c;
  Meta *bd;
} _Contrast_Data;

static void _worker_contrast(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int i, j;
  uint8_t *input, *o;
  uint16_t *input2, *o2;
  _Contrast_Data *data = ea_data(f->data, 0);

  assert(in && ea_count(in) == 1);
  assert(out && ea_count(out) == 1);
  
  if (*(int*)data->bd->data == BD_U16)
    hack_tiledata_fixsize_mt(2, ea_data(out, 0));
  
  input = ((Tiledata*)ea_data(in, 0))->data;
  o = ((Tiledata*)ea_data(out, 0))->data;
  input2 = (uint16_t*)input;
  o2 = (uint16_t*)o;

  if (*(int*)data->bd->data == BD_U8) {
    for(j=0;j<area->height;j++)
      for(i=0;i<area->width;i++) {
          o[j*area->width+i] = clip_u8(((input[j*area->width+i]-128)*data->c)+128);
      }
  }
  else {
    for(j=0;j<area->height;j++)
      for(i=0;i<area->width;i++) {
          o2[j*area->width+i] = clip_u16(((input2[j*area->width+i]-32768)*data->c)+32768);
      }
  }
}

static void _worker_exposure(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{ 
  int i, j;
  uint8_t *input, *o;
  uint16_t *input2, *o2;
  _Contrast_Data *data = ea_data(f->data, 0);

  assert(in && ea_count(in) == 1);
  assert(out && ea_count(out) == 1);
  
  if (*(int*)data->bd->data == BD_U16)
    hack_tiledata_fixsize_mt(2, ea_data(out, 0));
  
  input = ((Tiledata*)ea_data(in, 0))->data;
  o = ((Tiledata*)ea_data(out, 0))->data;
  input2 = (uint16_t*)input;
  o2 = (uint16_t*)o;

  if (*(int*)data->bd->data == BD_U8) {
    for(j=0;j<area->height;j++)
      for(i=0;i<area->width;i++) {
          o[j*area->width+i] = clip_u8((input[j*area->width+i])*data->c);
      }
  }
  else {
    for(j=0;j<area->height;j++)
      for(i=0;i<area->width;i++) {
          o2[j*area->width+i] = clip_u16(((input2[j*area->width+i])*data->c));
      }
  }
}

static Filter *_f_lightness_new(Filter_Core *fc, void worker_func(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id), float min, float max)
{
  Filter *filter = filter_new(fc);
  
  Meta *in, *out, *setting, *tune_bitdepth, *tune_color, *bound;
  _Contrast_Data *data = calloc(sizeof(_Contrast_Data), 1);
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = worker_func;
  filter->mode_buffer->threadsafe = 1;
  ea_push(filter->data, data);
  data->c = 1.0;
  filter->fixme_outcount = 1;
  
  //tune color-space
  tune_color = meta_new_select(MT_COLOR, filter, eina_array_new(3));
  pushint(tune_color->select, CS_LAB_L);
  pushint(tune_color->select, CS_YUV_Y);
  pushint(tune_color->select, CS_HSV_V);
  tune_color->replace = tune_color;
  tune_color->dep = tune_color;
  eina_array_push(filter->tune, tune_color);
  
  //tune bitdepth
  tune_bitdepth = meta_new_select(MT_BITDEPTH, filter, eina_array_new(2));
  pushint(tune_bitdepth->select, BD_U16);
  pushint(tune_bitdepth->select, BD_U8);
  tune_bitdepth->replace = tune_bitdepth;
  tune_bitdepth->dep = tune_bitdepth;
  eina_array_push(filter->tune, tune_bitdepth);
  data->bd = tune_bitdepth;
  
  //output
  out = meta_new_channel(filter, 1);
  meta_attach(out, tune_color);
  meta_attach(out, tune_bitdepth);
  eina_array_push(filter->out, out);
  
  //input
  in = meta_new_channel(filter, 1);
  meta_attach(in, tune_color);
  meta_attach(in, tune_bitdepth);
  eina_array_push(filter->in, in);
  in->replace = out;
  
  //setting
  setting = meta_new_data(MT_FLOAT, filter, &data->c);
  meta_name_set(setting, fc->shortname);
  eina_array_push(filter->settings, setting);
  
  bound = meta_new_data(MT_FLOAT, filter, malloc(sizeof(float)));
  *(float*)bound->data = min;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, filter, malloc(sizeof(float)));
  *(float*)bound->data = max;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  return filter;
}

Filter *filter_contrast_new(void)
{ 
  return _f_lightness_new(&filter_core_contrast, _worker_contrast, -10.0, 10.0);
}

Filter *filter_exposure_new(void)
{ 
  return _f_lightness_new(&filter_core_exposure, _worker_exposure, 0.0, 10.0);
}

Filter_Core filter_core_contrast = {
  "Contrast",
  "contrast",
  "Change contrast",
  &filter_contrast_new
};


Filter_Core filter_core_exposure = {
  "Exposure",
  "exposure",
  "Change exposure",
  &filter_exposure_new
};