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

#include "filter_lensfun.h"
#include "exif_helpers.h"

#include <lensfun.h>
#include <float.h>

const int bord = 16;

typedef struct {
  Meta *dim_in_meta;
  Meta *exif;
  const lfLens *lens;
  lfDatabase *db;
  Dim out_dim;
  float f, f_dist, f_num;
  const char *lens_model;
  int exif_loaded;
} _Common;

typedef struct {
  _Common *common;
  void *blur_b, *estimate_b, *fac_b;
} _Data;


static int check_exif(_Data *data)
{
  const lfLens **lenses;
  const lfLens *lens;
  lime_exif *exif = data->common->exif->data;
  
  if (data->common->exif_loaded == 1)
    return 0;
  else if (data->common->exif_loaded == -1)
    return -1;
  
  data->common->lens_model = lime_exif_handle_find_str_by_tagname(exif, "LensType");
  if (!data->common->lens_model)
    data->common->lens_model = lime_exif_handle_find_str_by_tagname(exif, "LensModel");
  if (!data->common->lens_model) {
    data->common->exif_loaded = -1;
    return -1;
  }
  data->common->lens_model = strdup(data->common->lens_model);
  data->common->f = lime_exif_handle_find_float_by_tagname(exif, "FocalLength");
  data->common->f_dist = lime_exif_handle_find_float_by_tagname(exif, "FocusDistance");
  if (data->common->f_dist == -1.0)
    data->common->f_dist = 1000;
  data->common->f_num = lime_exif_handle_find_float_by_tagname(exif, "FNumber");
  
  
  //FIXME check for errors...
  if (!data->common->db) {
    data->common->db = lf_db_new();
    lf_db_load(data->common->db);
  }
  
  lenses = lf_db_find_lenses_hd(data->common->db, NULL, NULL, data->common->lens_model, 0);
  
  if (!lenses) {
    printf("lensfun could not find lens model %s!\n", data->common->lens_model);
    data->common->exif_loaded = -1;
    return -1;
  }
  
  lens = lenses[0];
  data->common->lens = lens;
  lf_free(lenses);
  
  data->common->exif_loaded = 1;
  
  return 0;
}


static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{ 
  int i, j, ch;
  int w, h;
  uint16_t *output;
  uint16_t *input;
  uint16_t *pos;
  int scale;
  _Data *data = ea_data(f->data, 0);
  Tiledata *in_td, *out_td;
  Rect in_area;
  lfModifier *mod;
  float *coords;
  float x, y;
  float fx, fy;
  
  assert(in && ea_count(in) == 1);
  assert(out && ea_count(out) == 1);
  
  hack_tiledata_fixsize_mt(6, ea_data(out, 0));
  
  in_td = ((Tiledata*)ea_data(in, 0));
  in_area = in_td->area;
  input = in_td->data;
  out_td = ((Tiledata*)ea_data(out, 0));
  output = out_td->data;  
  
  //has been executed by area calc! (hacky?)
  assert(data->common->exif_loaded);
  
  if (check_exif(data)) {
    printf("FIXME: lensfun pass through if lens not identified! %d\n", data->common->exif_loaded);
    return;
  }
  
  if (area->corner.scale) {
    scale = area->corner.scale-1;
    w = ((Dim*)data->common->dim_in_meta->data)->width;
    h = ((Dim*)data->common->dim_in_meta->data)->height;
    
    mod = lf_modifier_new(data->common->lens, data->common->lens->CropFactor, w>>scale, h>>scale);
    lf_modifier_initialize(mod, data->common->lens, LF_PF_U16, data->common->f, data->common->f_num, data->common->f_dist, 0.0, LF_RECTILINEAR, LF_MODIFY_ALL, 0);
    
    coords = malloc(2*3*sizeof(float)*area->width);
    
    //FIXME fix negative y and x rounding (->fx)
    
    for(j=0;j<area->height;j++) {
      lf_modifier_apply_subpixel_geometry_distortion(mod, area->corner.x, area->corner.y+j, area->width, 1, coords);
      for(i=0;i<area->width;i++)
        for(ch=0;ch<3;ch++) {
          //FIXME can we realiably catch this in area_calc?
          x = coords[i*2*3+ch*2];
          y = coords[i*2*3+ch*2+1];
          if (x < in_area.corner.x || x >= in_area.corner.x+in_area.width-1)
            continue;
          if (y < in_area.corner.y || y >= in_area.corner.y+in_area.height-1)
            continue;
          fx = x-(int)x;
          fy = y-(int)y;
          pos = tileptr16_3(in_td, x, y) + ch;
          tileptr16_3(out_td, area->corner.x+i, area->corner.y+j)[ch] = 
            pos[0]*(1.0-fx)*(1.0-fy)
            +pos[3]*(fx)*(1.0-fy)
            +pos[in_area.width*3]*(1.0-fx)*(fy)
            +pos[in_area.width*3+3]*(fx)*(fy);
        }
    }
    
    lf_modifier_destroy(mod);
    free(coords);
  }
  else {
    w = ((Dim*)data->common->dim_in_meta->data)->width*2;
    h = ((Dim*)data->common->dim_in_meta->data)->height*2;
    
    mod = lf_modifier_new(data->common->lens, data->common->lens->CropFactor, w, h);
    lf_modifier_initialize(mod, data->common->lens, LF_PF_U16, data->common->f, data->common->f_num, data->common->f_dist, 0.0, LF_RECTILINEAR, LF_MODIFY_ALL, 0);
    
    coords = malloc(2*3*sizeof(float)*area->width);
    
    //FIXME fix negative y and x rounding (->fx)
    
    for(j=0;j<area->height;j++) {
      lf_modifier_apply_subpixel_geometry_distortion(mod, area->corner.x, area->corner.y+j, area->width, 1, coords);
      for(i=0;i<area->width;i++)
        for(ch=0;ch<3;ch++) {
          //FIXME can we realiably catch this in area_calc?
          x = coords[i*2*3+ch*2]*0.5;
          y = coords[i*2*3+ch*2+1]*0.5;
          if (x < in_area.corner.x || x >= in_area.corner.x+in_area.width-1)
            continue;
          if (y < in_area.corner.y || y >= in_area.corner.y+in_area.height-1)
            continue;
          fx = x-(int)x;
          fy = y-(int)y;
          pos = tileptr16_3(in_td, x, y) + ch;
          tileptr16_3(out_td, area->corner.x+i, area->corner.y+j)[ch] = 
            pos[0]*(1.0-fx)*(1.0-fy)
            +pos[3]*(fx)*(1.0-fy)
            +pos[in_area.width*3]*(1.0-fx)*(fy)
            +pos[in_area.width*3+3]*(fx)*(fy);
        }
    }
    
    lf_modifier_destroy(mod);
    free(coords);
  }
}

void fminmax(float *in, float *min, float *max)
{
  if (*in < *min) *min = *in;
  if (*in > *max) *max = *in;
}

void mod_min_max(lfModifier *mod, int x, int y, float *min, float *max)
{
  int ch;
  float coords[2*3];
  
  lf_modifier_apply_subpixel_geometry_distortion(mod, x, y, 1, 1, coords);
  for(ch=0;ch<3;ch++) {
    fminmax(coords+ch*2,min,max);
    fminmax(coords+ch*2+1,min+1,max+1);
  }
}

static void _area_calc(Filter *f, Rect *in, Rect *out)
{ 
  int w, h, ch;
  int rx, ry, rw, rh;
  int scale;
  lfModifier *mod;
  _Data *data = eina_array_data_get(f->data, 0);
  float min[2], max[2];
  
  if (check_exif(data)) {
    *out = *in;
    return;
  }
  
  min[0] = FLT_MAX;
  min[1] = FLT_MAX;
  max[0] = FLT_MIN;
  max[1] = FLT_MIN;
  
  rx = in->corner.x;
  ry = in->corner.y;
  rw = in->width;
  rh = in->height;
  
  if (in->corner.scale) {
    scale = in->corner.scale-1;
    w = ((Dim*)data->common->dim_in_meta->data)->width;
    h = ((Dim*)data->common->dim_in_meta->data)->height;

    mod = lf_modifier_new(data->common->lens, data->common->lens->CropFactor, w>>scale, h>>scale);
    lf_modifier_initialize(mod, data->common->lens, LF_PF_U16, data->common->f, data->common->f_num, data->common->f_dist, 0.0, LF_RECTILINEAR, LF_MODIFY_ALL, 0);
    
    mod_min_max(mod, in->corner.x, in->corner.y, min, max);
    mod_min_max(mod, in->corner.x+in->width, in->corner.y, min, max);
    mod_min_max(mod, in->corner.x, in->corner.y+in->height, min, max);
    mod_min_max(mod, in->corner.x+in->width, in->corner.y+in->height, min, max);

    out->corner.scale = in->corner.scale-1;
    out->corner.x = min[0]-bord;
    out->corner.y = min[1]-bord;
    out->width = max[0]-min[0]+2*bord;
    out->height = max[1]-min[1]+2*bord;

    lf_modifier_destroy(mod);
  }
  else {
    w = ((Dim*)data->common->dim_in_meta->data)->width*2;
    h = ((Dim*)data->common->dim_in_meta->data)->height*2;
    
    lfModifier *mod = lf_modifier_new(data->common->lens, data->common->lens->CropFactor, w, h);
    lf_modifier_initialize(mod, data->common->lens, LF_PF_U16, data->common->f, data->common->f_num, data->common->f_dist, 0.0, LF_RECTILINEAR, LF_MODIFY_ALL, 0);
    
    mod_min_max(mod, rx, ry, min, max);
    mod_min_max(mod, rx+rw, ry, min, max);
    mod_min_max(mod, rx, ry+rh, min, max);
    mod_min_max(mod, rx+rw, ry+rh, min, max);

    min[0] *= 0.5;
    max[0] *= 0.5;
    min[1] *= 0.5;
    max[1] *= 0.5;
    
    out->corner.scale = in->corner.scale;
    out->corner.x = min[0]-bord;
    out->corner.y = min[1]-bord;
    out->width = max[0]-min[0]+2*bord;
    out->height = max[1]-min[1]+2*bord;

    lf_modifier_destroy(mod);
  }
}

static int _input_fixed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  Dim *d;
  
  if (!data->common->exif->data)
    return -1;
  
  //data->common->out_dim = *(Dim*)data->common->dim_in_meta->data;
  
  d = &data->common->out_dim;
  *d = *(Dim*)data->common->dim_in_meta->data;
  d->scaledown_max++;
  d->width *= 2;
  d->height *= 2;
  
  return 0;
}

static int _del(Filter *f)
{
  int i;
  _Data *data = ea_data(f->data, 0);
  
  if (data->common->db)
    lf_db_destroy(data->common->db);
  
  if (data->common->lens_model)
    free(data->common->lens_model);
  
  for(i=0;i<ea_count(f->data);i++) {
    data = ea_data(f->data, i);
    free(data);
  }
  
  return 0;
}

static void *_data_new(Filter *f, void *data)
{
  _Data *newdata = calloc(sizeof(_Data), 1);
  
  newdata->common = ((_Data*)data)->common;
  
  return newdata;
}

static Filter *_new(void)
{
  Filter *f = filter_new(&filter_core_lensfun);
  
  Meta *in, *out, *in_ch, *out_ch, *setting, *bd_in, *bd_out, *tune_color, *bound, *size_in, *size_out, *exif;
  _Data *data = calloc(sizeof(_Data), 1);
  f->mode_buffer = filter_mode_buffer_new();
  f->mode_buffer->worker = _worker;
  f->mode_buffer->area_calc = _area_calc;
  f->mode_buffer->threadsafe = 1;
  f->mode_buffer->data_new = &_data_new;
  f->input_fixed = _input_fixed;
  f->del = _del;
  ea_push(f->data, data);
  f->fixme_outcount = 1;
  
  data->common = calloc(sizeof(_Common), 1);
  
  size_in = meta_new(MT_IMGSIZE, f);
  size_out = meta_new_data(MT_IMGSIZE, f, &data->common->out_dim);
  data->common->dim_in_meta = size_in;
  size_in->replace = size_out;
  
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
  
  exif = meta_new(MT_OBJ, f);
  meta_type_str_set(exif, "exif");
  exif->replace = exif;
  data->common->exif = exif;
  
  out = meta_new(MT_BUNDLE, f);
  meta_attach(out, exif);
  eina_array_push(f->out, out);
  
  in = meta_new(MT_BUNDLE, f);
  meta_attach(in, exif);
  in->replace = out;
  eina_array_push(f->in, in);
  
  //output
  out_ch = meta_new_channel(f, 1);
  meta_attach(out, out_ch);
  meta_attach(out_ch, tune_color);
  meta_attach(out_ch, bd_out);
  meta_attach(out_ch, size_out);
  
  //input
  in_ch = meta_new_channel(f, 1);
  meta_attach(in, in_ch);
  meta_attach(in_ch, tune_color);
  meta_attach(in_ch, bd_in);
  meta_attach(in_ch, size_in);
  in_ch->replace = out_ch;
  
  //setting
  /*setting = meta_new_data(MT_FLOAT, f, &data->common->radius);
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
  setting = meta_new_data(MT_INT, f, &data->common->iterations);
  meta_name_set(setting, "iterations");
  eina_array_push(f->settings, setting);
  
  bound = meta_new_data(MT_INT, f, malloc(sizeof(int)));
  *(int*)bound->data = 1;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_INT, f, malloc(sizeof(int)));
  *(int*)bound->data = 100;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);*/
  
  return f;
}

Filter_Core filter_core_lensfun = {
  "Automatic lens distortion correction",
  "lensfun",
  "Corrects lens distortion, vignetting and TCA using lensfun library",
  &_new
};
