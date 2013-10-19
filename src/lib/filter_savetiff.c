#include "filter_savetiff.h"
#include "tiffio.h"

typedef struct {
  Meta *m_size;
  Dim size;
  TIFF* file;
  int scales;
  uint32_t **scale_sums;
  uint64_t counter;
  uint8_t *buf;
  int *x_pos;
  int *y_pos;
  Meta *color[3];
  int colorspace;
  Meta *filename;
} _Data;

typedef struct {
  Filter *f;
  Rect area;
  int tw;
  int th;
  Eina_Array *f_source;
  Filter *f_source_curr;
  int finito;
  uint64_t counter;
} _Iter;

void _worker_gamma(Filter *f, void *in, int channel, Eina_Array *out, Rect *area, int thread_id)
{
   uint32_t xc, yc;
   int scale;
   uint64_t counter;
   int x, y, sx, sy, ox, oy;
   Tiledata *tile = in;
   _Data *data = ea_data(f->data, 0);
   int written;

   assert(TIFFSetDirectory(data->file, 0));
   written = TIFFWriteTile(data->file, tile->data, tile->area->corner.x, tile->area->corner.y, 0, channel);
   assert(written == 256*256);
   assert(TIFFRewriteDirectory(data->file));
   
   for(counter=data->counter, scale=1; scale<data->scales; counter/=4,scale++) {
      xc = tile->area->corner.x;
      xc = xc >> (scale + 8);
      xc *= 256;
      yc = tile->area->corner.y;
      yc = yc >> (scale + 8);
      yc *= 256;
      
      if (data->x_pos[scale-1] != xc || data->y_pos[scale-1] != yc) {
	 for(y=0;y<256;y++)
	    for(x=0;x<256;x++)
	       data->buf[y*256+x] = lime_l2g[((data->scale_sums[(scale-1)*3+channel])[y*256+x] + (1u<<(2*scale-1))) >> (scale*2)];
	    
	    assert(TIFFSetDirectory(data->file, scale));
	 written = TIFFWriteTile(data->file, data->buf, data->x_pos[scale-1], data->y_pos[scale-1], 0, channel);
	 assert(written == 256*256);
	 assert(TIFFRewriteDirectory(data->file));
	 
	 if (data->x_pos[scale-1] % 512) ox = 128;
	 else ox = 0;
	 if (data->y_pos[scale-1] % 512) oy = 128;
	 else oy = 0;
	 
	 for(y=0,sy=oy;sy<oy+128;sy++,y+=2)
	    for(x=0,sx=ox;sx<ox+128;sx++,x+=2) {
	       (data->scale_sums[scale*3 + channel])[sy*256 + sx] = (data->scale_sums[(scale-1)*3 + channel])[y*256 + x];
	       (data->scale_sums[scale*3 + channel])[sy*256 + sx] += (data->scale_sums[(scale-1)*3 + channel])[y*256 + x + 1];
	       (data->scale_sums[scale*3 + channel])[sy*256 + sx] += (data->scale_sums[(scale-1)*3 + channel])[y*256 + 256 + x];
	       (data->scale_sums[scale*3 + channel])[sy*256 + sx] += (data->scale_sums[(scale-1)*3 + channel])[y*256 + 256 + x + 1];
	    }  
	    
	    memset(data->scale_sums[(scale-1)*3+channel], 0, sizeof(uint32_t)*256*256);
      }
      
      
      if (channel == 2) {
	 data->x_pos[scale-1] = xc;
	 data->y_pos[scale-1] = yc;
      }
   }   
   
   if (data->counter & 0b01)
      ox = 128;
   else
      ox = 0;
   
   if (data->counter & 0b10)
      oy = 128;
   else
      oy = 0;
   
   if (ox == 0 && oy == 0)
      memset(data->scale_sums[channel], 0, sizeof(uint32_t)*256*256);
   
   for(y=0,sy=oy;sy<oy+128;sy++,y+=2)
      for(x=0,sx=ox;sx<ox+128;sx++,x+=2) {
	 (data->scale_sums[channel])[sy*256 + sx] =  lime_g2l[((uint8_t*)tile->data)[y*256 + x]];
	 (data->scale_sums[channel])[sy*256 + sx] += lime_g2l[((uint8_t*)tile->data)[y*256 + x + 1]];
	 (data->scale_sums[channel])[sy*256 + sx] += lime_g2l[((uint8_t*)tile->data)[y*256 + 256 + x]];
	 (data->scale_sums[channel])[sy*256 + sx] += lime_g2l[((uint8_t*)tile->data)[y*256 + 256 + x + 1]];
      }
      
}

void _worker_linear(Filter *f, void *in, int channel, Eina_Array *out, Rect *area, int thread_id)
{
   uint32_t xc, yc;
   int scale;
   uint64_t counter;
   int x, y, sx, sy, ox, oy;
   Tiledata *tile = in;
   _Data *data = ea_data(f->data, 0);
   int written;

   assert(TIFFSetDirectory(data->file, 0));
   written = TIFFWriteTile(data->file, tile->data, tile->area->corner.x, tile->area->corner.y, 0, channel);
   assert(written == 256*256);
   assert(TIFFRewriteDirectory(data->file));
   
   for(counter=data->counter, scale=1; scale<data->scales; counter/=4,scale++) {
      xc = tile->area->corner.x;
      xc = xc >> (scale + 8);
      xc *= 256;
      yc = tile->area->corner.y;
      yc = yc >> (scale + 8);
      yc *= 256;
      
      if (data->x_pos[scale-1] != xc || data->y_pos[scale-1] != yc) {
	 for(y=0;y<256;y++)
	    for(x=0;x<256;x++)
	       data->buf[y*256+x] = ((data->scale_sums[(scale-1)*3+channel])[y*256+x] + (1u<<(2*scale-1))) >> (scale*2);
	    
	    assert(TIFFSetDirectory(data->file, scale));
	 written = TIFFWriteTile(data->file, data->buf, data->x_pos[scale-1], data->y_pos[scale-1], 0, channel);
	 assert(written == 256*256);
	 assert(TIFFRewriteDirectory(data->file));
	 
	 if (data->x_pos[scale-1] % 512) ox = 128;
	 else ox = 0;
	 if (data->y_pos[scale-1] % 512) oy = 128;
	 else oy = 0;
	 
	 for(y=0,sy=oy;sy<oy+128;sy++,y+=2)
	    for(x=0,sx=ox;sx<ox+128;sx++,x+=2) {
	       (data->scale_sums[scale*3 + channel])[sy*256 + sx] = (data->scale_sums[(scale-1)*3 + channel])[y*256 + x];
	       (data->scale_sums[scale*3 + channel])[sy*256 + sx] += (data->scale_sums[(scale-1)*3 + channel])[y*256 + x + 1];
	       (data->scale_sums[scale*3 + channel])[sy*256 + sx] += (data->scale_sums[(scale-1)*3 + channel])[y*256 + 256 + x];
	       (data->scale_sums[scale*3 + channel])[sy*256 + sx] += (data->scale_sums[(scale-1)*3 + channel])[y*256 + 256 + x + 1];
	    }  
	    
	    memset(data->scale_sums[(scale-1)*3+channel], 0, sizeof(uint32_t)*256*256);
      }
      
      
      if (channel == 2) {
	 data->x_pos[scale-1] = xc;
	 data->y_pos[scale-1] = yc;
      }
   }   
   
   if (data->counter & 0b01)
      ox = 128;
   else
      ox = 0;
   
   if (data->counter & 0b10)
      oy = 128;
   else
      oy = 0;
   
   if (ox == 0 && oy == 0)
      memset(data->scale_sums[channel], 0, sizeof(uint32_t)*256*256);
   
   for(y=0,sy=oy;sy<oy+128;sy++,y+=2)
      for(x=0,sx=ox;sx<ox+128;sx++,x+=2) {
	 (data->scale_sums[channel])[sy*256 + sx] =  ((uint8_t*)tile->data)[y*256 + x];
	 (data->scale_sums[channel])[sy*256 + sx] += ((uint8_t*)tile->data)[y*256 + x + 1];
	 (data->scale_sums[channel])[sy*256 + sx] += ((uint8_t*)tile->data)[y*256 + 256 + x];
	 (data->scale_sums[channel])[sy*256 + sx] += ((uint8_t*)tile->data)[y*256 + 256 + x + 1];
      }
      
}

//TODO open file, get image dimensions etc
int _input_fixed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  int size, w, h;
  int i;

  // Open the TIFF file
  if((data->file = TIFFOpen(data->filename->data, "w")) == NULL)
    return -1;
  
  data->size = *(Dim*)data->m_size->data;
  
  size = data->size.width;
  if (data->size.height > size)
     size = data->size.height;
  
  data->scales = 0;
  while(size > 256) {
     size /= 2;
     data->scales++;
   }
   
   if (data->scales > 8)
      data->scales = 8;
  
  data->scale_sums = calloc(sizeof(uint32_t*)*data->scales*3, 1);
  data->x_pos = calloc(sizeof(int)*data->scales, 1);
  data->y_pos = calloc(sizeof(int)*data->scales, 1);
  w = data->size.width;
  h = data->size.height;
  
  for(i=0;i<data->scales;i++) {
     data->scale_sums[i*3] = malloc(sizeof(uint32_t)*256*256);
     data->scale_sums[i*3+1] = malloc(sizeof(uint32_t)*256*256);
     data->scale_sums[i*3+2] = malloc(sizeof(uint32_t)*256*256);
     
     TIFFSetField(data->file, TIFFTAG_IMAGEWIDTH, w);
     TIFFSetField(data->file, TIFFTAG_IMAGELENGTH, h);
     TIFFSetField(data->file, TIFFTAG_BITSPERSAMPLE, 8);
     TIFFSetField(data->file, TIFFTAG_SAMPLESPERPIXEL, 3);
     TIFFSetField(data->file, TIFFTAG_TILEWIDTH, 256);
     TIFFSetField(data->file, TIFFTAG_TILELENGTH, 256);
     TIFFSetField(data->file, TIFFTAG_PLANARCONFIG, PLANARCONFIG_SEPARATE);
     switch (data->colorspace) {
       case 0 :
	 TIFFSetField(data->file, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
	 break;
       case 1 : 
	 TIFFSetField(data->file, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_CIELAB);
	 break;
     }
     TIFFSetField(data->file, TIFFTAG_COMPRESSION, COMPRESSION_JPEG);
     TIFFSetField(data->file, TIFFTAG_JPEGQUALITY, 85);
     
     TIFFCheckpointDirectory(data->file);
     TIFFWriteDirectory(data->file);
     
     w = (w+1)/2;
     h = (h+1)/2;
  }
  
  data->buf = malloc(256*256);
  
  return 0;
}


void _finish_gamma(Filter *f)
{
   int scale;
   int x, y, ox, oy;
   int sx, sy;
   int channel;
   int written;
  _Data *data = ea_data(f->data, 0);

  for(channel=0;channel<3;channel++)
   for(scale=1; scale<data->scales; scale++) {
	    for(y=0;y<256;y++)
	       for(x=0;x<256;x++)
		  data->buf[y*256+x] = lime_l2g[((data->scale_sums[(scale-1)*3+channel])[y*256+x]+(1u<<(2*scale-1))) >> (scale*2)];
	       
	       assert(TIFFSetDirectory(data->file, scale));
	       written = TIFFWriteTile(data->file, data->buf, data->x_pos[scale-1], data->y_pos[scale-1], 0, channel);
	       assert(written == 256*256);
	       assert(TIFFRewriteDirectory(data->file));

	       if (data->x_pos[scale-1] % 512) ox = 128;
	       else ox = 0;
	       if (data->y_pos[scale-1] % 512) oy = 128;
	       else oy = 0;

	       for(y=0,sy=oy;sy<oy+128;sy++,y+=2)
		  for(x=0,sx=ox;sx<ox+128;sx++,x+=2) {
		     (data->scale_sums[scale*3 + channel])[sy*256 + sx] =  (data->scale_sums[(scale-1)*3 + channel])[y*256 + x];
		     (data->scale_sums[scale*3 + channel])[sy*256 + sx] += (data->scale_sums[(scale-1)*3 + channel])[y*256 + x + 1];
		     (data->scale_sums[scale*3 + channel])[sy*256 + sx] += (data->scale_sums[(scale-1)*3 + channel])[y*256 + 256 + x];
		     (data->scale_sums[scale*3 + channel])[sy*256 + sx] += (data->scale_sums[(scale-1)*3 + channel])[y*256 + 256 + x + 1];
		  }   
   }
  
  TIFFClose(data->file);
}

void _finish_linear(Filter *f)
{
   int scale;
   int x, y, ox, oy;
   int sx, sy;
   int channel;
   int written;
  _Data *data = ea_data(f->data, 0);

  for(channel=0;channel<3;channel++)
   for(scale=1; scale<data->scales; scale++) {
	    for(y=0;y<256;y++)
	       for(x=0;x<256;x++)
		  data->buf[y*256+x] = ((data->scale_sums[(scale-1)*3+channel])[y*256+x]+(1u<<(2*scale-1))) >> (scale*2);
	       
	       assert(TIFFSetDirectory(data->file, scale));
	       written = TIFFWriteTile(data->file, data->buf, data->x_pos[scale-1], data->y_pos[scale-1], 0, channel);
	       assert(written == 256*256);
	       assert(TIFFRewriteDirectory(data->file));

	       if (data->x_pos[scale-1] % 512) ox = 128;
	       else ox = 0;
	       if (data->y_pos[scale-1] % 512) oy = 128;
	       else oy = 0;

	       for(y=0,sy=oy;sy<oy+128;sy++,y+=2)
		  for(x=0,sx=ox;sx<ox+128;sx++,x+=2) {
		     (data->scale_sums[scale*3 + channel])[sy*256 + sx] =  (data->scale_sums[(scale-1)*3 + channel])[y*256 + x];
		     (data->scale_sums[scale*3 + channel])[sy*256 + sx] += (data->scale_sums[(scale-1)*3 + channel])[y*256 + x + 1];
		     (data->scale_sums[scale*3 + channel])[sy*256 + sx] += (data->scale_sums[(scale-1)*3 + channel])[y*256 + 256 + x];
		     (data->scale_sums[scale*3 + channel])[sy*256 + sx] += (data->scale_sums[(scale-1)*3 + channel])[y*256 + 256 + x + 1];
		  }  
   }
  
  TIFFClose(data->file);
}

void pos_from_counter(uint64_t counter, int *x, int *y)
{
   int pos = 1;
   *x = 0;
   *y = 0;
   
   //consume counter from small to big
   while (counter) {
      if (counter % 2)
	 *x = *x + pos;
      counter /= 2;
      if (counter % 2)
	 *y = *y + pos;
      pos *= 2;
      counter /= 2;
   }
}


void _iter_next(void *data, Pos *pos, int *channel)
{
  _Iter *iter = data;
  
  (*channel)++;
  if (*channel < 3) {
    iter->f_source_curr = ea_data(iter->f_source, *channel);
    iter->tw = tw_get(iter->f_source_curr, iter->area.corner.scale);
    iter->th = th_get(iter->f_source_curr, iter->area.corner.scale);
  }
  else {
    *channel = 0;
    
    while (1) {
      iter->counter++;
      pos_from_counter(iter->counter, &pos->x, &pos->y);

      pos->x *= 256;
      pos->y *= 256;
      
      if (pos->x >= iter->area.corner.x + iter->area.width && pos->y >= iter->area.corner.y + iter->area.height) {
	iter->finito = 1;
	break;
      }
      
      if (pos->x < iter->area.corner.x + iter->area.width && pos->y < iter->area.corner.y + iter->area.height)
	 break;
   }
  }
  
  ((_Data*)ea_data(iter->f->data, 0))->counter = iter->counter;
}

int _iter_eoi(void *data, Pos pos, int channel)
{ 
  _Iter *iter = data;
  
  return iter->finito;
}

void *_iter_new(Filter *f, Rect *area, Eina_Array *f_source, Pos *pos, int *channel)
{
  _Iter *iter = calloc(sizeof(_Iter), 1);
  _Data *data = ea_data(f->data, 0);
  
  iter->f = f;
  *channel = 0;
  
  iter->area.corner.x = 0;
  iter->area.corner.y = 0;
  iter->area.corner.scale = 0;
  iter->area.width = data->size.width;
  iter->area.height = data->size.height;
  
  iter->f_source = f_source;
  iter->f_source_curr = ea_data(f_source, *channel);
  
  iter->tw = tw_get(iter->f_source_curr, iter->area.corner.scale);
  iter->th = th_get(iter->f_source_curr, iter->area.corner.scale);
  
  iter->counter = 0;
  data->counter = 0;
  
  //FIXME calc pos!
  pos->x = iter->area.corner.x;
  pos->y = iter->area.corner.y;
  pos->scale = iter->area.corner.scale;
  
  return iter;
}

static int _setting_changed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  
  switch (data->colorspace) {
    case 0 : 
      *(int*)(data->color[0]->data) = CS_RGB_R;
      *(int*)(data->color[1]->data) = CS_RGB_G;
      *(int*)(data->color[2]->data) = CS_RGB_B;
      f->mode_iter->worker = &_worker_gamma;
      f->mode_iter->finish = &_finish_gamma;
      break;
    case 1 : 
      *(int*)(data->color[0]->data) = CS_LAB_L;
      *(int*)(data->color[1]->data) = CS_LAB_A;
      *(int*)(data->color[2]->data) = CS_LAB_B;
      f->mode_iter->worker = &_worker_linear;
      f->mode_iter->finish = &_finish_linear;
      break;
    default :
      abort();    
  }
  
  return 0;
}

Filter *filter_savetiff_new(void)
{
  Filter *filter = filter_new(&filter_core_savetiff);
  Meta *in, *channel, *bitdepth, *size, *setting, *bound;
  _Data *data = calloc(sizeof(_Data), 1);
  ea_push(filter->data, data);
  
  filter->mode_iter = filter_mode_iter_new();
  filter->mode_iter->iter_new = &_iter_new;
  filter->mode_iter->iter_next = &_iter_next;
  filter->mode_iter->iter_eoi = &_iter_eoi;
  filter->mode_iter->worker = &_worker_gamma;
  filter->mode_iter->finish = &_finish_gamma;
  
  filter->input_fixed = &_input_fixed;
  filter->setting_changed = &_setting_changed;
  
  bitdepth = meta_new_data(MT_BITDEPTH, filter, malloc(sizeof(int)));
  *(int*)(bitdepth->data) = BD_U8;
  
  size = meta_new(MT_IMGSIZE, filter);
  ea_push(filter->core, size);
  data->m_size = size;
  
  in = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->in, in);
  
  channel = meta_new_channel(filter, 1);
  data->color[0] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color[0]->data) = CS_RGB_R;
  meta_attach(channel, data->color[0]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 2);
  data->color[1] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color[1]->data) = CS_RGB_G;
  meta_attach(channel, data->color[1]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 3);
  data->color[2] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color[2]->data) = CS_RGB_B;
  meta_attach(channel, data->color[2]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size);
  meta_attach(in, channel);
  
  setting = meta_new_data(MT_INT, filter, &data->colorspace);
  meta_name_set(setting, "colorspace");
  eina_array_push(filter->settings, setting);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 1;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  data->filename = meta_new(MT_STRING, filter);
  meta_name_set(data->filename, "filename");
  eina_array_push(filter->settings, data->filename);
  
  return filter;
}

Filter_Core filter_core_savetiff = {
  "TIFF saver",
  "savetiff",
  "Saves image to a tiled pyramidal tiff file",
  &filter_savetiff_new
};
