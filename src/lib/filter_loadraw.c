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

#define RAW_TILE_WIDTH 1024
#define RAW_TILE_HEIGHT 256

#include <libraw.h>
#include "jpeglib.h"
#include "jerror.h"
#include <libexif/exif-loader.h>

#define RAW_TILING_BORDER 8

#define RAW_THREADSAFE_BUT_LEAK

/* Expanded data source object for stdio input */

typedef struct {
  libraw_data_t *raw;
  int unpacked;
  int opened;
#ifdef RAW_THREADSAFE_BUT_LEAK
  pthread_mutex_t lock;
#endif
  Meta *exif;
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
  libraw_data_t *raw;
} _Data;

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
#ifdef RAW_THREADSAFE_BUT_LEAK
  _Data *data = ea_data(f->data, thread_id);
  _Data *tdata;
#else
  _Data *data = ea_data(f->data, 0);
#endif
  Tiledata *out_td;
  libraw_processed_image_t *img;
  int errcode;
  
  if (!data->common->unpacked) {
#ifdef RAW_THREADSAFE_BUT_LEAK
    pthread_mutex_lock(&data->common->lock);
#endif
    if (!data->common->unpacked) {
      
      data->common->raw->params.user_qual = 10;
      data->common->raw->params.adjust_maximum_thr = 0.0;
      data->common->raw->params.no_auto_bright = 1;
      data->common->raw->params.no_auto_scale = 0;
      data->common->raw->params.use_auto_wb = 0;
      data->common->raw->params.use_camera_matrix = 0;
      data->common->raw->params.output_bps = 16;
      
      //exposure
      data->common->raw->params.exp_correc = 0.0;
      data->common->raw->params.exp_shift = 0.0;
      data->common->raw->params.exp_preser = 0.0;
      
      //sRGB
      data->common->raw->params.output_color = 4;
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
      
#ifdef RAW_THREADSAFE_BUT_LEAK
      for(i=0;i<ea_count(f->data);i++) {
        tdata = ea_data(f->data, i);
        if (tdata->raw)
          libraw_copy_del(tdata->raw);
        tdata->raw = libraw_data_copy(data->common->raw);
      }
#else
    data->raw = data->common->raw;
#endif
    }
#ifdef RAW_THREADSAFE_BUT_LEAK
    pthread_mutex_unlock(&data->common->lock);
#endif
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
  
#ifdef RAW_THREADSAFE_BUT_LEAK
  hack_tiledata_fixsize_mt(6, ea_data(out, 0));
#else
  hack_tiledata_fixsize(6, ea_data(out, 0));
#endif
  out_td = (Tiledata*)ea_data(out, 0);
  
  for(j=0;j<h;j++)
    for(i=0;i<w;i++) {
      uint16_t *ptr = tileptr16_3(out_td, out_td->area.corner.x+i, out_td->area.corner.y+j);
      for(ch=0;ch<3;ch++)
        ptr[ch] = ((uint16_t*)img->data)[((j+offy)*img->width+i+offx)*3+ch];
    }
  
  libraw_dcraw_clear_mem(img);
  libraw_free_image(data->raw);
}

static int _input_fixed(Filter *f)
{
  int i;
  int rot;
  _Data *data = ea_data(f->data, 0);
  
  if (!data->common->raw)
    data->common->raw = libraw_init(0);
  
  data->common->raw->params.use_camera_matrix = 0;
  data->common->raw->params.use_camera_wb = 1;
  data->common->raw->params.user_flip = 0;
  data->common->raw->params.use_rawspeed = 1;
  
  //data->common->raw->params.camera_profile = "/usr/share/rawtherapee/dcpprofiles/Olympus E-M5.dcp";
  
  if (data->common->opened) {
    libraw_recycle(data->common->raw);
    libraw_recycle_datastream(data->common->raw);
    //libraw_close(data->common->raw);
  }
  
  if (libraw_open_file(data->common->raw, data->input->data)) {
    libraw_close(data->common->raw);
    data->common->raw = NULL;
    return -1;
  }
  
  data->common->opened = 1;
  
  //printf("raw profile: %s\n", data->common->raw->params.camera_profile);
  
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
  
  f->tw_s[0] = RAW_TILE_WIDTH;
  f->th_s[0] = RAW_TILE_HEIGHT;
  f->tw_s[1] = RAW_TILE_WIDTH;
  f->th_s[1] = RAW_TILE_HEIGHT;
  
  data->common->unpacked = 0;
  
  data->common->exif->data = lime_exif_handle_new_from_file(data->input->data);
  assert(data->common->exif->data);
  
#ifdef RAW_THREADSAFE_BUT_LEAK
  for(i=0;i<ea_count(f->data);i++) {
    data = ea_data(f->data, i);
    if (data->raw)
      libraw_copy_del(data->raw);
    data->raw = NULL;
  }
#endif

  return 0;
}

//FIXME uhh ohh free thread stuff (c++ class instance!)
static int _del(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  int i;
  
  if (data->common->opened) {
    libraw_recycle(data->common->raw);
    libraw_recycle_datastream(data->common->raw);
  }
  
  if (data->common->raw)
    libraw_close(data->common->raw);
  
  if (data->common->exif->data)
    lime_exif_handle_destroy(data->common->exif->data);
  
  free(data->common);
  free(data->dim);
  
  for(i=0;i<ea_count(f->data);i++) {
    data = ea_data(f->data, i);
    if (data->raw)
      libraw_copy_del(data->raw);
    free(data);
  }
  
  
  return 0;
}

#ifdef RAW_THREADSAFE_BUT_LEAK
static void *_data_new(Filter *f, void *data)
{
  _Data *newdata = calloc(sizeof(_Data), 1);
  
  *newdata = *(_Data*)data;
  newdata->raw = libraw_data_copy(newdata->common->raw);
  
  return newdata;
}
#endif

static Filter *_new(void)
{
  Filter *filter = filter_new(&filter_core_loadraw);
  Meta *in, *out, *channel, *bitdepth, *color, *dim, *fliprot, *exif;
  _Data *data = calloc(sizeof(_Data), 1);
  data->common = calloc(sizeof(_Common), 1);
  data->dim = calloc(sizeof(Dim), 1);
#ifdef RAW_THREADSAFE_BUT_LEAK
  pthread_mutex_init(&data->common->lock, NULL);
#endif
  
  filter->del = &_del;
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = &_worker;
#ifdef RAW_THREADSAFE_BUT_LEAK
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->data_new = &_data_new;
#endif
  filter->input_fixed = &_input_fixed;
  filter->fixme_outcount = 3;
  ea_push(filter->data, data);
  
  bitdepth = meta_new_data(MT_BITDEPTH, filter, malloc(sizeof(int)));
  *(int*)(bitdepth->data) = BD_U16;
  
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
  
  exif = meta_new(MT_OBJ, filter);
  meta_type_str_set(exif, "exif");
  meta_attach(out, exif);
  data->common->exif = exif;
  
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
