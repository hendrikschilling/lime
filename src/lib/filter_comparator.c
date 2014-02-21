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

#include "filter_comparator.h"

typedef struct {
   int channel;
   int x;
   int y;
   int scale;
   uint8_t *buf;
} _Cmp_Data;

typedef struct {
  Meta *m_size;
  Dim size;
  int scales;
  uint32_t **scale_sums;
  uint64_t counter;
  uint8_t *buf;
  int *x_pos;
  int *y_pos;
  Eina_Array *to_compare;
  int diffs[8];
  int colorspace;
  Meta *color[3];
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
  int lastchannel;
} _Iter;

static void _worker_gamma(Filter *f, void *in, int channel, Eina_Array *out, Rect *area, int thread_id)
{
   uint32_t xc, yc;
   int scale;
   uint64_t counter;
   int x, y, sx, sy, ox, oy;
   Tiledata *tile = in;
   _Data *data = ea_data(f->data, 0);
   _Cmp_Data *cmp;
   int max, diff;
   int div = 1u << tile->area->corner.scale;

   if (tile->area->corner.scale) {
      //printf("rendered %dx%d @ %d\n", tile->area->corner.x, tile->area->corner.y, tile->area->corner.scale);
      cmp = ea_pop(data->to_compare);
      
      if (tile->area->corner.x+tile->area->width < data->size.width/div && tile->area->corner.y+tile->area->height < data->size.height/div) {
	max = 0;
      
      	 for(y=0;y<256;y++)
	    for(x=0;x<256;x++) {
	       diff = abs(cmp->buf[y*256+x] - ((uint8_t*)tile->data)[y*256+x]);
	       if (diff > max)
		 max = diff;
	    }
	    
      /*if (max)
	printf("max diff: %d (%d %d %dx%d)\n", max, channel, tile->area->corner.scale, tile->area->corner.x, tile->area->corner.y);*/
      if (max > data->diffs[tile->area->corner.scale]) {
	data->diffs[tile->area->corner.scale] = max;
	printf("max diff per scale:\n");
	for(x=1;x<data->scales-2;x++)
	  printf("%d %d\n", x, data->diffs[x]);
	printf("\n");
	
	char buf[1024];
	
	sprintf(buf, "tile%dref.ppm", tile->area->corner.scale);
	FILE *file = fopen(buf, "w");
	fprintf(file, "P5\n256 256\n255\n");
	fwrite(cmp->buf, 256*256,1 ,file);
	fclose(file);
	
	sprintf(buf, "tile%dbad.ppm", tile->area->corner.scale);
	file = fopen(buf, "w");
	fprintf(file, "P5\n256 256\n255\n");
	fwrite(tile->data, 256*256,1 ,file);
	fclose(file);
	
	//printf("worst for scale %d located at %dx%d\n", tile->area->corner.scale, tile->area->corner.x, tile->area->corner.y);
      }
      }
      
      free(cmp->buf);
      free(cmp);
      
      return;
   }

   for(counter=data->counter, scale=1; scale<data->scales; counter/=4,scale++) {
      xc = tile->area->corner.x;
      xc = xc >> (scale + 8);
      xc *= 256;
      yc = tile->area->corner.y;
      yc = yc >> (scale + 8);
      yc *= 256;
      
      if (data->x_pos[scale-1] != xc || data->y_pos[scale-1] != yc) {
	
	if (data->x_pos[scale-1] && data->y_pos[scale-1]) {
	 cmp = calloc(sizeof(_Cmp_Data), 1);
	 cmp->x = data->x_pos[scale-1];
	 cmp->y = data->y_pos[scale-1];
	 cmp->channel = channel;
	 cmp->scale = scale;
	 cmp->buf = malloc(256*256);
	
	 eina_array_push(data->to_compare, cmp);
	 
	 for(y=0;y<256;y++)
	    for(x=0;x<256;x++)
	       cmp->buf[y*256+x] = lime_l2g[((data->scale_sums[(scale-1)*3+channel])[y*256+x] + (1u<<(2*scale-1))) >> (scale*2)];
	}
	/*else
	  printf("FIXME: ignoring border tiles!\n");*/
	 
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
   
   for(y=0,sy=oy;sy<oy+128;sy++,y+=2)
      for(x=0,sx=ox;sx<ox+128;sx++,x+=2) {
	 (data->scale_sums[channel])[sy*256 + sx] =  lime_g2l[((uint8_t*)tile->data)[y*256 + x]];
	 (data->scale_sums[channel])[sy*256 + sx] += lime_g2l[((uint8_t*)tile->data)[y*256 + x + 1]];
	 (data->scale_sums[channel])[sy*256 + sx] += lime_g2l[((uint8_t*)tile->data)[y*256 + 256 + x]];
	 (data->scale_sums[channel])[sy*256 + sx] += lime_g2l[((uint8_t*)tile->data)[y*256 + 256 + x + 1]];
      }
      
}


static void _worker_linear(Filter *f, void *in, int channel, Eina_Array *out, Rect *area, int thread_id)
{
   uint32_t xc, yc;
   int scale;
   uint64_t counter;
   int x, y, sx, sy, ox, oy;
   Tiledata *tile = in;
   _Data *data = ea_data(f->data, 0);
   _Cmp_Data *cmp;
   int max, diff;
   int div = 1u << tile->area->corner.scale;

   if (tile->area->corner.scale) {
      cmp = ea_pop(data->to_compare);
      
      if (tile->area->corner.x+tile->area->width < data->size.width/div && tile->area->corner.y+tile->area->height < data->size.height/div) {
	max = 0;
      
      	 for(y=0;y<256;y++)
	    for(x=0;x<256;x++) {
	       diff = abs(cmp->buf[y*256+x] - ((uint8_t*)tile->data)[y*256+x]);
	       if (diff > max)
		 max = diff;
	    }
	    
      if (max > data->diffs[tile->area->corner.scale]) {
	data->diffs[tile->area->corner.scale] = max;
	printf("max diff per scale:\n");
	for(x=1;x<data->scales-2;x++)
	  printf("%d %d\n", x, data->diffs[x]);
	printf("\n");
	
	char buf[1024];
	
	sprintf(buf, "tile%dref.ppm", tile->area->corner.scale);
	FILE *file = fopen(buf, "w");
	fprintf(file, "P5\n256 256\n255\n");
	fwrite(cmp->buf, 256*256,1 ,file);
	fclose(file);
	
	sprintf(buf, "tile%dbad.ppm", tile->area->corner.scale);
	file = fopen(buf, "w");
	fprintf(file, "P5\n256 256\n255\n");
	fwrite(tile->data, 256*256,1 ,file);
	fclose(file);
	
	//printf("worst for scale %d located at %dx%d\n", tile->area->corner.scale, tile->area->corner.x, tile->area->corner.y);
      }
      }
      
      free(cmp->buf);
      free(cmp);
      
      return;
   }

   for(counter=data->counter, scale=1; scale<data->scales; counter/=4,scale++) {
      xc = tile->area->corner.x;
      xc = xc >> (scale + 8);
      xc *= 256;
      yc = tile->area->corner.y;
      yc = yc >> (scale + 8);
      yc *= 256;
      
      if (data->x_pos[scale-1] != xc || data->y_pos[scale-1] != yc) {
	
	if (data->x_pos[scale-1] && data->y_pos[scale-1]) {
	 cmp = calloc(sizeof(_Cmp_Data), 1);
	 cmp->x = data->x_pos[scale-1];
	 cmp->y = data->y_pos[scale-1];
	 cmp->channel = channel;
	 cmp->scale = scale;
	 cmp->buf = malloc(256*256);
	
	 eina_array_push(data->to_compare, cmp);
	 
	 for(y=0;y<256;y++)
	    for(x=0;x<256;x++)
	       cmp->buf[y*256+x] = ((data->scale_sums[(scale-1)*3+channel])[y*256+x] + (1u<<(2*scale-1))) >> (scale*2);
	}
	/*else
	  printf("FIXME: ignoring border tiles!\n");*/
	 
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
   
   for(y=0,sy=oy;sy<oy+128;sy++,y+=2)
      for(x=0,sx=ox;sx<ox+128;sx++,x+=2) {
	 (data->scale_sums[channel])[sy*256 + sx] =  ((uint8_t*)tile->data)[y*256 + x];
	 (data->scale_sums[channel])[sy*256 + sx] += ((uint8_t*)tile->data)[y*256 + x + 1];
	 (data->scale_sums[channel])[sy*256 + sx] += ((uint8_t*)tile->data)[y*256 + 256 + x];
	 (data->scale_sums[channel])[sy*256 + sx] += ((uint8_t*)tile->data)[y*256 + 256 + x + 1];
      }
      
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
      break;
    case 1 : 
      *(int*)(data->color[0]->data) = CS_LAB_L;
      *(int*)(data->color[1]->data) = CS_LAB_A;
      *(int*)(data->color[2]->data) = CS_LAB_B;
      f->mode_iter->worker = &_worker_linear;
      printf("set lab worker\n");
      break;
    case 2 : 
      *(int*)(data->color[0]->data) = CS_YUV_Y;
      *(int*)(data->color[1]->data) = CS_YUV_U;
      *(int*)(data->color[2]->data) = CS_YUV_V;
      f->mode_iter->worker = &_worker_linear;
      break;
    default :
      abort();    
  }
  
  return 0;
}


//TODO open file, get image dimensions etc
static int _input_fixed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  int size, w, h;
  int i;
  
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
  
  data->to_compare = eina_array_new(16);
  
  for(i=0;i<data->scales;i++) {
     data->scale_sums[i*3] = malloc(sizeof(uint32_t)*256*256);
     data->scale_sums[i*3+1] = malloc(sizeof(uint32_t)*256*256);
     data->scale_sums[i*3+2] = malloc(sizeof(uint32_t)*256*256);

     w = (w+1)/2;
     h = (h+1)/2;
  }
  
  data->buf = malloc(256*256);
  
  return 0;
}

static void _finish(Filter *f)
{
}

static void pos_from_counter(uint64_t counter, int *x, int *y)
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


static void _iter_next(void *data, Pos *pos, int *channel)
{
  _Iter *iter = data;
  _Data *f_data = ea_data(iter->f->data, 0);
  
  _Cmp_Data *cmp;
  
  if (ea_count(f_data->to_compare)) {
     cmp = ea_data(f_data->to_compare, ea_count(f_data->to_compare)-1);
     pos->x = cmp->x;
     pos->y = cmp->y;
     pos->scale = cmp->scale;
     *channel = cmp->channel;

     return;
   }
  
  //printf("cont %d\n", iter->lastchannel);
  
  *channel = iter->lastchannel + 1;
  if (*channel < 3) {
    iter->lastchannel = *channel;
    pos_from_counter(iter->counter, &pos->x, &pos->y);
    pos->x *= 256;
    pos->y *= 256;
    pos->scale = 0;
    iter->f_source_curr = ea_data(iter->f_source, *channel);
    iter->tw = tw_get(iter->f_source_curr, iter->area.corner.scale);
    iter->th = th_get(iter->f_source_curr, iter->area.corner.scale);
  }
  else {
    *channel = 0;
    iter->lastchannel = 0;
    pos->scale = 0;
        
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
  
  f_data->counter = iter->counter;
}

static int _iter_eoi(void *data, Pos pos, int channel)
{ 
  _Iter *iter = data;
  
  return iter->finito;
}

static void *_iter_new(Filter *f, Rect *area, Eina_Array *f_source, Pos *pos, int *channel)
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
  
  printf("max diff per scale:\n1 0\n2 0\n3 0\n4 0\n");
  
  return iter;
}

Filter *filter_compare_new(void)
{
  Filter *filter = filter_new(&filter_core_compare);
  Meta *in, *channel, *bitdepth, *size, *setting, *bound;
  _Data *data = calloc(sizeof(_Data), 1);
  ea_push(filter->data, data);
  
  filter->mode_iter = filter_mode_iter_new();
  filter->mode_iter->iter_new = &_iter_new;
  filter->mode_iter->iter_next = &_iter_next;
  filter->mode_iter->iter_eoi = &_iter_eoi;
  filter->mode_iter->worker = &_worker_gamma;
  filter->mode_iter->finish = &_finish;
  
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
  meta_name_set(setting, "input colorspace");
  eina_array_push(filter->settings, setting);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 2;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  return filter;
}

Filter_Core filter_core_compare = {
  "Comparison filter",
  "compare",
  "Filter that compares the full resolution filter with the scaled filter to examine the scaling accuracy",
  &filter_compare_new
};
