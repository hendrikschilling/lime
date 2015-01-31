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

#include "filter_loadraw.h"

#include <libexif/exif-data.h>
#include <jpeglib.h>
#include <setjmp.h>

#define JPEG_TILE_WIDTH 256
#define JPEG_TILE_HEIGHT 256

#include <libraw.h>
#include "jpeglib.h"
#include "jerror.h"
#include <libexif/exif-loader.h>


/* Expanded data source object for stdio input */

typedef struct {
  int error;
  int *index;
} _Common;

typedef struct {
  _Common *common;
  Meta *input;
  Meta *dim;
  int rot;
  int seekable;
  int *size_pos;
  int file_pos;
  int rst_int;
  int mcu_w, mcu_h;
  int w, h;
  int comp_count;
  int serve_ix;
  int serve_iy;
  int serve_minx;
  int serve_miny;
  int serve_maxx;
  int serve_maxy;
  int serve_fakejpg;
  int serve_bytes;
  int serve_width;
  int serve_height;
  int rst_next;
  int iw, ih;
#ifdef USE_UJPEG
  ujImage uimg;
#endif
  char *filename;
  pthread_mutex_t *lock;
  Meta *fliprot;
  int thumb_len;
  uint8_t *thumb_data;
  libraw_data_t *raw;
} _Data;

static inline uint8_t *tileptr8(Tiledata *tile, int x, int y)
{ 
   return &((uint8_t*)tile->data)[(y-tile->area.corner.y)*tile->area.width + x-tile->area.corner.x];
}

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int ch, i, j;
  _Data *data = ea_data(f->data, 0);
  Tiledata *out_td;
  
  libraw_unpack(data->raw);
  printf("upack %s\n", data->input->data);
  //libraw_dcraw_process(data->raw);
  
  assert(out && ea_count(out) == 3);
    
    for(ch=0;ch<3;ch++) {
        out_td = (Tiledata*)ea_data(out, ch);
        for(j=0;j<out_td->area.height;j++)
            for(i=0;i<out_td->area.width;i++)
                *tileptr8(out_td, out_td->area.corner.x+i, out_td->area.corner.y+j) = (data->raw->rawdata.raw_image)[(j*data->raw->sizes.raw_width+i)] / ((data->raw->color.maximum+255)/256);
                
    }
  
}

static int min(a, b) 
{
  if (a<=b) return a;
  return b;
}

static int _input_fixed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  
  if (libraw_open_file(data->raw, data->input->data))
    return -1;
  
  printf("size of %s: %dx%d\n", data->input->data, data->raw->sizes.width, data->raw->sizes.height);

  //default
  data->rot = 1;
  
  data->w = data->raw->sizes.width;
  data->h = data->raw->sizes.height;
  
  ((Dim*)data->dim)->scaledown_max = 0;
  ((Dim*)data->dim)->width = data->w;
  ((Dim*)data->dim)->height = data->h;
  
  f->tw_s = realloc(f->tw_s, sizeof(int)*(((Dim*)data->dim)->scaledown_max+1));
  f->th_s = realloc(f->th_s, sizeof(int)*(((Dim*)data->dim)->scaledown_max+1));
  
  f->tw_s[0] = data->w;
  f->th_s[0] = data->h;

  return 0;
}

static int _del(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  int i;
  
  free(data->thumb_data);
  free(data->lock);
  free(data->size_pos);
  
  if (data->common->index)
    free(data->common->index);
  
  free(data->common);
  
  for(i=0;i<ea_count(f->data);i++) {
    data = ea_data(f->data, i);
    free(data);
  }
  
  
  return 0;
}

static Filter *_new(void)
{
  Filter *filter = filter_new(&filter_core_loadraw);
  Meta *in, *out, *channel, *bitdepth, *color, *dim, *fliprot;
  _Data *data = calloc(sizeof(_Data), 1);
  data->common = calloc(sizeof(_Common), 1);
  data->size_pos = calloc(sizeof(int*), 1);
  data->lock = calloc(sizeof(pthread_mutex_t), 1);
  assert(pthread_mutex_init(data->lock, NULL) == 0);
  data->dim = calloc(sizeof(Dim), 1);
  data->raw = libraw_init(0);
  
  filter->del = &_del;
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = &_worker;
  filter->mode_buffer->threadsafe = 1;
  filter->input_fixed = &_input_fixed;
  filter->fixme_outcount = 3;
  ea_push(filter->data, data);
  
  bitdepth = meta_new_data(MT_BITDEPTH, filter, malloc(sizeof(int)));
  *(int*)(bitdepth->data) = BD_U8;
  
  dim = meta_new_data(MT_IMGSIZE, filter, data->dim);
  eina_array_push(filter->core, dim);
  
  out = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->out, out);
  
  in = meta_new(MT_LOADIMG, filter);
  in->replace = out;
  eina_array_push(filter->in, in);
  data->input = in;
  
  fliprot = meta_new(MT_FLIPROT, filter);
  meta_attach(out, fliprot);
  data->fliprot = fliprot;
  data->fliprot->data = &data->rot;
  
  channel = meta_new_channel(filter, 1);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_R;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, dim);
  meta_attach(out, channel);
  
  channel = meta_new_channel(filter, 2);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_G;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, dim);
  meta_attach(out, channel);
  
  channel = meta_new_channel(filter, 3);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_B;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, dim);
  meta_attach(out, channel);
  
  return filter;
}

Filter_Core filter_core_loadraw = {
  "Raw loader",
  "loadraw",
  "Loads RAW camera images from a file",
  &_new
};
