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

#include "filter_rotate.h"

//TODO manipulate exif orientation to remove the extra rotation step

typedef struct {
  Meta *dim_in_meta;
  Dim *out_dim;
  Meta *rotate;
  float *rot;
  float sin_c, cos_c;
  int offx_src,offx_tgt, offy_src,offy_tgt;
  Meta *bd;
} _Data;

int imin(int a, int b)
{
    if (a <= b) return a;
    return b;
}

int imax(int a, int b)
{
    if (a >= b) return a;
    return b;
}

float inline rx(_Data *data, int x, int y, int scale)
{
    x -= data->offx_tgt >> scale;
    y -= data->offy_tgt >> scale;
    return data->cos_c*x-data->sin_c*y+(data->offx_src >> scale);
}

float inline ry(_Data *data, int x, int y, int scale)
{
    x -= data->offx_tgt >> scale;
    y -= data->offy_tgt >> scale;
    return data->cos_c*y+data->sin_c*x+(data->offy_src >> scale);
}

void calc_rot_src_area(_Data *data, Rect *target, Rect *src)
{
    int minx,miny,maxx,maxy;
    int x, y;
    
    minx = rx(data, target->corner.x, target->corner.y, target->corner.scale);
    maxx = minx;
    
    x = rx(data, target->corner.x+target->width, target->corner.y, target->corner.scale);
    minx = imin(x, minx);
    maxx = imax(x, maxx);
    
    x = rx(data, target->corner.x, target->corner.y+target->height, target->corner.scale);
    minx = imin(x, minx);
    maxx = imax(x, maxx);
    
    x = rx(data, target->corner.x+target->width, target->corner.y+target->height, target->corner.scale);
    minx = imin(x, minx);
    maxx = imax(x, maxx);
    
    
    miny = ry(data, target->corner.x, target->corner.y, target->corner.scale);
    maxy = miny;
    
    y = ry(data, target->corner.x+target->width, target->corner.y, target->corner.scale);
    miny = imin(y, miny);
    maxy = imax(y, maxy);
    
    y = ry(data, target->corner.x, target->corner.y+target->height, target->corner.scale);
    miny = imin(y, miny);
    maxy = imax(y, maxy);
    
    y = ry(data, target->corner.x+target->width, target->corner.y+target->height, target->corner.scale);
    miny = imin(y, miny);
    maxy = imax(y, maxy);
    
    src->corner.x = minx;
    src->corner.y = miny;
    src->width =maxx-minx;
    src->height = maxy-miny;
}

void lrec(float rad, int ow, int oh, int *w, int *h)
{
    int l, s;
    if (ow >= oh) {
        l = ow;
        s = oh;
    }
    else {
        l = oh;
        s = ow;
    }

    float sina = fabs(sin(rad));
    float cosa = fabs(cos(rad));
    
    if (s <= 2*sina*cosa*l) {
        float x = 0.5*s;
        if (ow >= oh) {
            *w = x/sina;
            *h = x/cosa;
        }
        else {
            *w = x/cosa;
            *h = x/sina;
        }
    }
  else{
    float cos2a = cosa*cosa - sina*sina;
    *w = (ow*cosa - oh*sina)/cos2a;
    *h = (oh*cosa - ow*sina)/cos2a;
  }
}

static int _input_fixed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  Dim *in_dim = data->dim_in_meta->data;
  Rect src, target;
  int sw, sh;
  
  data->sin_c = sin(-*data->rot/180*M_PI);
  data->cos_c = cos(-*data->rot/180*M_PI);
  
  src.corner.x = in_dim->x;
  src.corner.y = in_dim->y;
  src.corner.scale = 0;
  src.width = in_dim->width;
  src.height = in_dim->height;
  
  calc_rot_src_area(data, &src, &target);
  
  data->offx_src = in_dim->width/2;
  data->offy_src = in_dim->height/2;
  lrec(-*data->rot/180.0*M_PI, in_dim->width, in_dim->height, &sw, &sh);
  data->offx_tgt = target.width/2-(target.width-sw)/2;
  data->offy_tgt = target.height/2-(target.height-sh)/2;
  
  *data->out_dim = *in_dim;
  data->out_dim->width = sw;
  data->out_dim->height = sh;
  data->out_dim->scaledown_max = in_dim->scaledown_max;
    
  return 0;
}

static void _area_calc(Filter *f, Rect *in, Rect *out)
{
    _Data *data = ea_data(f->data, 0);
    
    calc_rot_src_area(data, in, out);
    out->corner.scale = in->corner.scale;
    out->width += 2;
    out->height += 2;
}

static inline uint8_t interpolate(Tiledata *tile, float x, float y)
{ 
    int ix = x;
    int iy = y;
    if (x < 0) ix--;
    if (y < 0) iy--;
    float fx = x - ix;
    float fy = y - iy;
    uint8_t *ptr = tileptr8(tile,ix,iy);

    
  return ptr[0]*(1.0-fx)*(1.0-fy)
        +ptr[1]*(fx)*(1.0-fy)
        +ptr[tile->area.width]*(1.0-fx)*(fy)
        +ptr[tile->area.width+1]*(fx)*(fy);
}

static inline uint16_t interpolate16(Tiledata *tile, float x, float y)
{ 
    int ix = x;
    int iy = y;
    if (x < 0) ix--;
    if (y < 0) iy--;
    float fx = x - ix;
    float fy = y - iy;
    uint16_t *ptr = tileptr16(tile,ix,iy);

    
  return ptr[0]*(1.0-fx)*(1.0-fy)
        +ptr[1]*(fx)*(1.0-fy)
        +ptr[tile->area.width]*(1.0-fx)*(fy)
        +ptr[tile->area.width+1]*(fx)*(fy);
}

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
    int ch;
    int i, j;
    Tiledata *in_td, *out_td;
    _Data *data = ea_data(f->data, 0);
  
    assert(in && ea_count(in) == 3);
    assert(out && ea_count(out) == 3);
    
    if (*(int*)data->bd->data == BD_U8) {
      for(ch=0;ch<3;ch++) {
          in_td = (Tiledata*)ea_data(in, ch);
          out_td = (Tiledata*)ea_data(out, ch);
          for(j=0;j<out_td->area.height;j++)
              for(i=0;i<out_td->area.width;i++)
                  *tileptr8(out_td, out_td->area.corner.x+i, out_td->area.corner.y+j) = interpolate(in_td, rx(data,out_td->area.corner.x+i,out_td->area.corner.y+j,out_td->area.corner.scale), ry(data,out_td->area.corner.x+i,out_td->area.corner.y+j,out_td->area.corner.scale));
      }
    }
    else {
      for(ch=0;ch<3;ch++) {
        hack_tiledata_fixsize_mt(3, ea_data(out, ch));
        in_td = (Tiledata*)ea_data(in, ch);
        out_td = (Tiledata*)ea_data(out, ch);
        for(j=0;j<out_td->area.height;j++)
          for(i=0;i<out_td->area.width;i++)
            *tileptr16(out_td, out_td->area.corner.x+i, out_td->area.corner.y+j) = interpolate16(in_td, rx(data,out_td->area.corner.x+i,out_td->area.corner.y+j,out_td->area.corner.scale), ry(data,out_td->area.corner.x+i,out_td->area.corner.y+j,out_td->area.corner.scale));
      }
    }
}

static int _del(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  
  free(data->out_dim);
  free(data->rot);
  free(data);
  
  return 0;
}

static Filter *_new(void)
{
  Filter *filter = filter_new(&filter_core_rotate);
  Meta *in, *out, *channel, *color[3], *size_in, *size_out, *bound, *rotate, *bitdepth;
  Meta *ch_out[3];
  _Data *data = calloc(sizeof(_Data), 1);
  data->out_dim = calloc(sizeof(Dim), 1);
  data->rot = calloc(sizeof(float), 1);

  filter->del = &_del;
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->worker = &_worker;
  filter->mode_buffer->area_calc = &_area_calc;
  filter->fixme_outcount = 3;
  filter->input_fixed = &_input_fixed;
  ea_push(filter->data, data);
  
  bitdepth = meta_new_select(MT_BITDEPTH, filter, eina_array_new(2));
  pushint(bitdepth->select, BD_U16);
  pushint(bitdepth->select, BD_U8);
  bitdepth->replace = bitdepth;
  bitdepth->dep = bitdepth;
  eina_array_push(filter->tune, bitdepth);
  data->bd = bitdepth;
  
  size_in = meta_new(MT_IMGSIZE, filter);
  size_out = meta_new_data(MT_IMGSIZE, filter, data->out_dim);
  data->dim_in_meta = size_in;
  size_in->replace = size_out;
  
  out = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->out, out);
  
  channel = meta_new_channel(filter, 1);
  color[0] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[0]->data) = CS_RGB_R;
  meta_attach(channel, color[0]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size_out);
  meta_attach(out, channel);
  ch_out[0] = channel;
  
  channel = meta_new_channel(filter, 2);
  color[1] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[1]->data) = CS_RGB_G;
  meta_attach(channel, color[1]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size_out);
  meta_attach(out, channel);
  ch_out[1] = channel;
  
  channel = meta_new_channel(filter, 3);
  color[2] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[2]->data) = CS_RGB_B;
  meta_attach(channel, color[2]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size_out);
  meta_attach(out, channel);
  ch_out[2] = channel;
  
  in = meta_new(MT_BUNDLE, filter);
  in->replace = out;
  eina_array_push(filter->in, in);
  
  rotate = meta_new_data(MT_FLOAT, filter, data->rot);
  meta_name_set(rotate, "rotation angle");
  eina_array_push(filter->settings, rotate);
  
  bound = meta_new_data(MT_FLOAT, filter, malloc(sizeof(float)));
  *(float*)bound->data = -360.0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(rotate, bound);
  
  bound = meta_new_data(MT_FLOAT, filter, malloc(sizeof(float)));
  *(float*)bound->data = 360.0;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(rotate, bound);
  
  channel = meta_new_channel(filter, 1);
  color[0]->replace = color[0];
  channel->replace = ch_out[0];
  meta_attach(channel, color[0]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size_in);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 2);
  color[1]->replace = color[1];
  channel->replace = ch_out[1];
  meta_attach(channel, color[1]);
  meta_attach(channel, size_in);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 3);
  color[2]->replace = color[2];
  channel->replace = ch_out[2];
  meta_attach(channel, color[2]);
  meta_attach(channel, size_in);
  meta_attach(in, channel);
  
  return filter;
}

Filter_Core filter_core_rotate = {
  "Rotate Angle",
  "rotate angle",
  "rotate image about an angle",
  &_new
};