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

#include "filter_loadtiff.h"
#include "tiffio.h"

typedef struct {
  Meta *input;
  Meta *dim;
  TIFF* file;
  int directory;
  Meta *color[3];
} _Data;

static void *_loadtiff_data_new(Filter *f, void *data)
{
  _Data *newdata = calloc(sizeof(_Data), 1);
  
  newdata->input = ((_Data*)data)->input;
  newdata->directory = 0;
  newdata->dim = ((_Data*)data)->dim;
  
  newdata->file = TIFFOpen((char*)newdata->input->data, "r");
  assert(newdata->file);
  
  return newdata;
}

void _loadtiff_worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  _Data *data = ea_data(f->data, thread_id);

  uint8_t *r, *g, *b;
  uint32 width, height;
  uint32 twidth, tlenght;
  uint32 x, y;
  uint8_t *buf;
  int i, j;
  int16_t bitspersample, samplesperpixel, planarconfig;
  assert(out && ea_count(out) == 3);
  
  r = ((Tiledata*)ea_data(out, 0))->data;
  g = ((Tiledata*)ea_data(out, 1))->data;
  b = ((Tiledata*)ea_data(out, 2))->data;
  
  if (area->corner.scale != data->directory) {
    if (!TIFFSetDirectory(data->file, area->corner.scale))
      abort();
    else
      data->directory = area->corner.scale;
  }

  TIFFGetField(data->file, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(data->file, TIFFTAG_IMAGELENGTH, &height);
  TIFFGetField(data->file, TIFFTAG_TILEWIDTH, &twidth);
  TIFFGetField(data->file, TIFFTAG_TILELENGTH, &tlenght);
  TIFFGetField(data->file, TIFFTAG_BITSPERSAMPLE, &bitspersample);
  TIFFGetField(data->file, TIFFTAG_SAMPLESPERPIXEL, &samplesperpixel);
  TIFFGetField(data->file, TIFFTAG_PLANARCONFIG, &planarconfig);

  assert(bitspersample == 8);
  assert(samplesperpixel == 3 || samplesperpixel == 4);
  //if (samplesperpixel == 4)
  //  printf("FIXME: discarding alpha?!\n");
  assert(twidth == area->width);
  assert(tlenght == area->height);
  
  if (area->corner.x < 0 || area->corner.y < 0 || area->corner.x >= width || area->corner.y >= height) {
    memset(r, 0, twidth*tlenght);
    memset(g, 0, twidth*tlenght);
    memset(b, 0, twidth*tlenght);
    
    printf("FIXME: invalid tilerequest");
    
    return;
  }
  
  if (planarconfig == PLANARCONFIG_CONTIG) {
     buf = _TIFFmalloc(TIFFTileSize(data->file));
     for (y = area->corner.y/tlenght*tlenght; y < area->corner.y+area->height; y += tlenght)
	for (x = area->corner.x/twidth*twidth; x < area->corner.x+area->width; x += twidth) {
	   TIFFReadTile(data->file, buf, x, y, 0, 0);
	   
	    //FIXME right and bottom border trash!
	   
	   for(j=0;j<area->height;j++)
	      for(i=0;i<area->width;i++) {
		 r[j*area->width+i] = buf[(j*area->width+i)*samplesperpixel+0];
		 g[j*area->width+i] = buf[(j*area->width+i)*samplesperpixel+1];
		 b[j*area->width+i] = buf[(j*area->width+i)*samplesperpixel+2];
	      }
	}
	
	_TIFFfree(buf);
  }
  else if (planarconfig == PLANARCONFIG_SEPARATE) {
    TIFFReadTile(data->file, r, area->corner.x, area->corner.y, 0, 0);
    TIFFReadTile(data->file, g, area->corner.x, area->corner.y, 0, 1);
    TIFFReadTile(data->file, b, area->corner.x, area->corner.y, 0, 2);
    
    if (area->corner.x+area->width > width)
      for(y=0;y<area->height;y++) {
	memset(r+twidth*y + width  - area->corner.x, 0, area->corner.x+area->width - width);
	memset(g+twidth*y + width  - area->corner.x, 0, area->corner.x+area->width - width);
	memset(b+twidth*y + width  - area->corner.x, 0, area->corner.x+area->width - width);
      }
      
      if (area->corner.y+area->height > height) {
	memset(r+twidth*(height - area->corner.y), 0, (area->height + area->corner.y - height)*256);
	memset(g+twidth*(height - area->corner.y), 0, (area->height + area->corner.y - height)*256);
	memset(b+twidth*(height - area->corner.y), 0, (area->height + area->corner.y - height)*256);
      }
      
  }
  
}

//TODO open file, get image dimensions etc
int _loadtiff_input_fixed(Filter *f)
{
  uint32_t width, height;
  uint32_t twidth, theight;
  short color;
  int succ = 0;
  _Data *data = ea_data(f->data, 0);
  
  data->file = TIFFOpen(data->input->data, "r");

  if (!data->file)
    return -1;
  
  succ += TIFFGetField(data->file, TIFFTAG_IMAGEWIDTH, &width);
  succ += TIFFGetField(data->file, TIFFTAG_IMAGELENGTH, &height);
  succ += TIFFGetField(data->file, TIFFTAG_TILEWIDTH, &twidth);
  succ += TIFFGetField(data->file, TIFFTAG_TILELENGTH, &theight);
  succ += TIFFGetField(data->file, TIFFTAG_PHOTOMETRIC, &color);
  
  if (succ != 5) {
    printf("TIFF in unsupported configuration %s\n", data->input->data);
    TIFFClose(data->file);
    return -1;
  }
  
  ((Dim*)data->dim)->width = width;
  ((Dim*)data->dim)->height = height;
  ((Dim*)data->dim)->scaledown_max = 0;
  
  switch (color) {
    case PHOTOMETRIC_RGB :
       *(int*)(data->color[0]->data) = CS_RGB_R;
       *(int*)(data->color[1]->data) = CS_RGB_G;
       *(int*)(data->color[2]->data) = CS_RGB_B;
      break;
    case PHOTOMETRIC_CIELAB :
       *(int*)(data->color[0]->data) = CS_LAB_L;
       *(int*)(data->color[1]->data) = CS_LAB_A;
       *(int*)(data->color[2]->data) = CS_LAB_B;
      break;
    default:
      abort();
  }
    
  data->directory = 0;
  
  while (TIFFReadDirectory(data->file)) {
    ((Dim*)data->dim)->scaledown_max++;
    data->directory++;
  }
    
  f->tile_width = twidth;
  f->tile_height = theight;

  
  return 0;
}

static int _del(Filter *f)
{
  _Data *data;
  int i;
  for(i=0;i<ea_count(f->data);i++) {
    data = ea_data(f->data, i);
    if (data->file)
      TIFFClose(data->file);
  }
  
  return 0;
}

Filter *filter_loadtiff_new(void)
{
  Filter *filter = filter_new(&filter_core_loadtiff);
  Meta *in, *out, *channel, *bitdepth, *dim, *fliprot;
  _Data *data = calloc(sizeof(_Data), 1);
  data->dim = calloc(sizeof(Dim), 1);
  
  TIFFSetErrorHandler(NULL);
  TIFFSetWarningHandler(NULL);
  
  filter->del = &_del;
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = &_loadtiff_worker;
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->data_new = &_loadtiff_data_new;
  filter->input_fixed = &_loadtiff_input_fixed;
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
  
  fliprot = meta_new_data(MT_FLIPROT, filter, malloc(sizeof(int)));
  *(int*)fliprot->data = 1;
  meta_attach(out, fliprot);
  
  channel = meta_new_channel(filter, 1);
  data->color[0] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color[0]->data) = CS_RGB_R;
  meta_attach(channel, data->color[0]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, dim);
  meta_attach(out, channel);
  
  channel = meta_new_channel(filter, 2);
  data->color[1] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color[1]->data) = CS_RGB_G;
  meta_attach(channel, data->color[1]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, dim);
  meta_attach(out, channel);
  
  channel = meta_new_channel(filter, 3);
  data->color[2] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color[2]->data) = CS_RGB_B;
  meta_attach(channel, data->color[2]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, dim);
  meta_attach(out, channel);
  
  return filter;
}

Filter_Core filter_core_loadtiff = {
  "TIFF loader",
  "loadtiff",
  "Loads TIFF images from a file",
  &filter_loadtiff_new
};

