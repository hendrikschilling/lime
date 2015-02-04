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
#include "libraw_helpers.h"

#include <libexif/exif-data.h>
#include <jpeglib.h>
#include <setjmp.h>

#define JPEG_TILE_WIDTH 256
#define JPEG_TILE_HEIGHT 256

#include <libraw.h>
#include "jpeglib.h"
#include "jerror.h"
#include <libexif/exif-loader.h>

#define RAW_TILING_BORDER 8


/* Expanded data source object for stdio input */

typedef struct {
  libraw_data_t *raw;
  int unpacked;
  pthread_mutex_t lock;
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
  Meta *fliprot;
  int thumb_len;
  uint8_t *thumb_data;
  libraw_data_t *raw;
} _Data;

static inline uint8_t *tileptr8(Tiledata *tile, int x, int y)
{ 
   return &((uint8_t*)tile->data)[((y-tile->area.corner.y)*tile->area.width + x-tile->area.corner.x)*3];
}

static int imin(a, b) 
{
  if (a<=b) return a;
  return b;
}

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int ch, i, j;
  int tb;
  int offx, offy;
  int w, h;
  _Data *data = ea_data(f->data, thread_id);
  _Data *tdata;
  Tiledata *out_td;
  libraw_processed_image_t *img;
  int errcode;
  
  if (!data->common->unpacked) {
    pthread_mutex_lock(&data->common->lock);
    if (!data->common->unpacked) {
      
      data->common->raw->params.user_qual = 10;
      data->common->raw->params.adjust_maximum_thr = 0.0;
      data->common->raw->params.no_auto_bright = 1;
      data->common->raw->params.no_auto_scale = 0;
      data->common->raw->params.use_auto_wb = 0;
      data->common->raw->params.use_camera_matrix = 0;
      data->common->raw->params.output_bps = 8;
      
      //exposure
      data->common->raw->params.exp_correc = 0.0;
      data->common->raw->params.exp_shift = 0.0;
      data->common->raw->params.exp_preser = 0.0;
      
      //sRGB
      data->common->raw->params.output_color = 1;
      //BT709
      //data->common->raw->params.gamm[0]=1.0/2.222;
      //data->common->raw->params.gamm[1]=4.5;
      //sRGB
      //data->common->raw->params.gamm[0]=1.0/2.4;
      //data->common->raw->params.gamm[1]=12.92;
        
      data->common->raw->params.gamm[0]=1.0;
      data->common->raw->params.gamm[1]=0.0;
      
      libraw_unpack(data->common->raw);
      data->common->unpacked = 1;
      
      for(i=0;i<ea_count(f->data);i++) {
        tdata = ea_data(f->data, i);
        tdata->raw = libraw_data_copy(data->common->raw);
      }
    }
    pthread_mutex_unlock(&data->common->lock);
  }
  
  assert(out && ea_count(out) == 3);
  data->raw->params.half_size = area->corner.scale;
  
  if (!area->corner.scale)
    tb = RAW_TILING_BORDER;
  else
    tb = 0;

  data->raw->params.cropbox[0] = imax((area->corner.x<<area->corner.scale)-tb, 0);
  data->raw->params.cropbox[1] = imax((area->corner.y<<area->corner.scale)-tb, 0);
  data->raw->params.cropbox[2] = (area->width<<area->corner.scale)+2*tb;
  data->raw->params.cropbox[3] = (area->height<<area->corner.scale)+2*tb;
  
  offx = (area->corner.x<<area->corner.scale) - data->raw->params.cropbox[0];
  offy = (area->corner.y<<area->corner.scale) - data->raw->params.cropbox[1];
  
  libraw_dcraw_process(data->raw);
  assert(data->raw->image);
  img = libraw_dcraw_make_mem_image(data->raw, &errcode);
  
  w = imin(area->width, data->raw->sizes.width-offx);
  h = imin(area->height, data->raw->sizes.height-offy);
  
  hack_tiledata_fixsize(3, ea_data(out, 0));
  out_td = (Tiledata*)ea_data(out, 0);
  
  for(j=0;j<h;j++)
    for(i=0;i<w;i++) {
      uint8_t *ptr = tileptr8(out_td, out_td->area.corner.x+i, out_td->area.corner.y+j);
      for(ch=0;ch<3;ch++)
        ptr[ch] = img->data[((j+offy)*img->width+i+offx)*3+ch];
    }
}

static int _input_fixed(Filter *f)
{
  int rot;
  _Data *data = ea_data(f->data, 0);
  
  data->common->raw->params.use_camera_matrix = 0;
  data->common->raw->params.use_camera_wb = 1;
  data->common->raw->params.user_flip = 0;
  data->common->raw->params.use_rawspeed = 0;
  
  data->common->raw->params.camera_profile = "/usr/share/rawtherapee/dcpprofiles/Olympus E-M5.dcp";
  
  if (libraw_open_file(data->common->raw, data->input->data))
    return -1;
  
  printf("raw profile: %s\n", data->common->raw->params.camera_profile);
  
  //default
  data->rot = 1;
  switch (data->common->raw->sizes.flip) {
    case 0 : data->rot = 1; break;
    case 5 : data->rot = 8; break;
    case 6 : data->rot = 6; break;
    default :
      printf("FIXME implemente rotation %d in raw!\n", data->common->raw->sizes.flip);
  }
  
  data->w = data->common->raw->sizes.width;
  data->h = data->common->raw->sizes.height;
  
  ((Dim*)data->dim)->scaledown_max = 1;
  ((Dim*)data->dim)->width = data->w;
  ((Dim*)data->dim)->height = data->h;
  
  f->tw_s = realloc(f->tw_s, sizeof(int)*(((Dim*)data->dim)->scaledown_max+1));
  f->th_s = realloc(f->th_s, sizeof(int)*(((Dim*)data->dim)->scaledown_max+1));
  
  f->tw_s[0] = DEFAULT_TILE_SIZE;
  f->th_s[0] = 2*DEFAULT_TILE_SIZE;
  f->tw_s[1] = DEFAULT_TILE_SIZE;
  f->th_s[1] = 2*DEFAULT_TILE_SIZE;
  
  data->common->unpacked = 0;

  return 0;
}

//FIXME uhh ohh free thread stuff (c++ class instance!)
static int _del(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  int i;
  
  free(data->thumb_data);
  free(data->size_pos);  
  free(data->common);
  
  for(i=0;i<ea_count(f->data);i++) {
    data = ea_data(f->data, i);
    free(data);
  }
  
  
  return 0;
}

static void *_data_new(Filter *f, void *data)
{
  _Data *newdata = calloc(sizeof(_Data), 1);
  
  *newdata = *(_Data*)data;
  newdata->raw = libraw_data_copy(newdata->common->raw);
  
  return newdata;
}

static Filter *_new(void)
{
  Filter *filter = filter_new(&filter_core_loadraw);
  Meta *in, *out, *channel, *bitdepth, *color, *dim, *fliprot;
  _Data *data = calloc(sizeof(_Data), 1);
  data->common = calloc(sizeof(_Common), 1);
  data->size_pos = calloc(sizeof(int*), 1);
  data->dim = calloc(sizeof(Dim), 1);
  data->common->raw = libraw_init(0);
  pthread_mutex_init(&data->common->lock, NULL);
  
  filter->del = &_del;
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = &_worker;
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->data_new = &_data_new;
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
  *(int*)(color->data) = CS_INT_RGB;
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
