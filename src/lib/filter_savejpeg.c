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

#include "filter_savejpeg.h"

#include <jpeglib.h>

typedef struct {
  Meta *filename;
} _Data;

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  FILE *file;
  int i, j;
  JSAMPROW row_pointer[1];
  _Data *data = ea_data(f->data, 0);
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
    
  file = fopen(data->filename->data, "w");
  
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, file);

  cinfo.image_width      = area->width;
  cinfo.image_height     = area->height;
  cinfo.input_components = 3;
  cinfo.in_color_space   = JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality (&cinfo, 75, TRUE);
  jpeg_start_compress(&cinfo, TRUE);
    
  while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer[0] = (uint8_t*)((Tiledata*)ea_data(in, 0))->data + cinfo.next_scanline*area->width*3;
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }
  jpeg_finish_compress(&cinfo);

  fclose(file);
}

Filter *filter_savejpeg_new(void)
{
  Filter *filter = filter_new(&filter_core_savejpeg);
  Meta *in, *channel, *bitdepth, *color, *size, *setting, *fliprot;
  _Data *data = calloc(sizeof(_Data), 1);
  ea_push(filter->data, data);
  
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = &_worker;
  filter->mode_buffer->threadsafe = 0;
  
  bitdepth = meta_new_data(MT_BITDEPTH, filter, malloc(sizeof(int)));
  *(int*)(bitdepth->data) = BD_U8;
  
  size = meta_new(MT_IMGSIZE, filter);
  ea_push(filter->core, size);
  
  in = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->in, in);
  
  fliprot = meta_new_data(MT_FLIPROT, filter, malloc(sizeof(int)));
  *(int*)fliprot->data = 1;
  meta_attach(in, fliprot);
  
  channel = meta_new_channel(filter, 1);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_INT_RGB;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size);
  meta_attach(in, channel);
  
  data->filename = meta_new(MT_STRING, filter);
  meta_name_set(data->filename, "filename");
  eina_array_push(filter->settings, data->filename);
  
  return filter;
}

Filter_Core filter_core_savejpeg = {
  "Save jpeg file",
  "savejpeg",
  "Stores input as a jpeg file",
  &filter_savejpeg_new
};