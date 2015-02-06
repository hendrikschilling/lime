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

#include "filter_denoise.h"

#include <math.h>

#define MAX_MATCH_COUNT 40
#define MAX_AVG_COUNT 20

#define MAX_SCALE_DIFF 100

#define SEARCH_SIZE 16

#define TILE_SIZE DEFAULT_TILE_SIZE

typedef struct {
  int x, y;
  int diff;
  int origin;
} _Match;

typedef struct {
  _Match *matches;
  uint8_t *avgs;
  uint8_t *changed;
  int *counts;
  int *worsts;
  int *iters;
  float *max_diff;
  float *max_diff_c;
} _Data;

void *_denoise_data_new(Filter *f, void *data)
{
  _Data *new = calloc(sizeof(_Data), 1);
  
  new->matches = malloc(sizeof(_Match)*TILE_SIZE*TILE_SIZE*MAX_MATCH_COUNT);
  new->changed = malloc(sizeof(uint8_t)*TILE_SIZE*TILE_SIZE);
  new->counts = malloc(sizeof(int)*TILE_SIZE*TILE_SIZE);
  new->worsts = malloc(sizeof(int)*TILE_SIZE*TILE_SIZE);
  new->avgs = malloc(sizeof(uint8_t)*(TILE_SIZE+2*SEARCH_SIZE)*(TILE_SIZE+2*SEARCH_SIZE)*3);
  new->max_diff = ((_Data*)data)->max_diff;
  new->max_diff_c = ((_Data*)data)->max_diff_c;
  new->iters = ((_Data*)data)->iters;
  
  return new;
}

static void _area_calc(Filter *f, Rect *in, Rect *out)
{ 
  if (!in->corner.scale) {
    out->corner.scale = in->corner.scale;
    out->corner.x = in->corner.x-SEARCH_SIZE;
    out->corner.y = in->corner.y-SEARCH_SIZE;
    out->width = in->width+2*SEARCH_SIZE;
    out->height = in->height+2*SEARCH_SIZE;
  }
  else
    *out = *in;
}

static inline int cmp_mtach(const void * a, const void * b)
{
  return (((_Match*)a)->diff - ((_Match*)b)->diff);
}

static void add_diff(_Data *data, _Match *m, int x, int y, Rect *area, Tiledata *td, int ch)
{
  int i, j;
  uint8_t *buf_ref, *buf_m;
  int avg_ref, avg_m;
  int width = td->area.width;
  int sum = 0;
  int ax, ay; //absolute coordinates
  int ix, iy; //input relative coordinates
  //  x   y     output relateive coordinates
  
  ax = area->corner.x+x;
  ay = area->corner.y+y;
  ix = ax - td->area.corner.x;
  iy = ay - td->area.corner.y;
  
  buf_ref = tileptr8(td, ax-1, ay-1);
  buf_m = tileptr8(td, ax+m->x-1, ay+m->y-1);
  
  avg_ref = data->avgs[(iy*td->area.width + ix)*3+ch];
  avg_m = data->avgs[((iy+m->y)*td->area.width + ix+m->x)*3+ch];
  
  /*m->diff += abs(buf_ref[-width-1]-avg_ref+avg_m-buf_m[-width-1]) + abs(buf_ref[-width]-avg_ref+avg_m-buf_m[-width]) + abs(buf_ref[-width+1]-avg_ref+avg_m-buf_m[-width+1]);
  m->diff += abs(buf_ref[-1]-avg_ref+avg_m-buf_m[-1]) + abs(buf_ref[0]-avg_ref+avg_m-buf_m[0]) + abs(buf_ref[1]-avg_ref+avg_m-buf_m[1]);
  m->diff += abs(buf_ref[width-1]-avg_ref+avg_m-buf_m[width-1]) + abs(buf_ref[width]-avg_ref+avg_m-buf_m[width]) + abs(buf_ref[width+1]-avg_ref+avg_m-buf_m[width+1]);*/

  for(j=0,buf_ref,buf_m;j<3;j++,buf_ref+=width,buf_m+=width) {
    for(i=0,buf_ref,buf_m;i<3;i++,buf_ref++,buf_m++)
      sum += abs(buf_ref[0]-avg_ref+avg_m-buf_m[0]);
    buf_m -= 3;
    buf_ref -= 3;
  }
  
  m->diff += sum;
}

static void insert_match(_Data *data, _Match m, int x, int y)
{
  int i;
  _Match *matches;
  int *count;
  int *worst;
  
  matches = &data->matches[(y*TILE_SIZE+x)*MAX_MATCH_COUNT];
  count = &data->counts[y*TILE_SIZE+x];
  worst = &data->worsts[y*TILE_SIZE+x];
  
  //if we dont yet have MAX_AVG_COUNT we take anything!
  /*if (*count < MAX_AVG_COUNT) {*/
  if (*count > MAX_AVG_COUNT && m.diff > *worst)
    return;
  
  assert(*count < MAX_MATCH_COUNT);
  
    for(i=0;i<*count;i++)
      if (m.x == matches[i].x && m.y == matches[i].y)
	return;
    
    matches[*count] = m;
    (*count)++;
    if (*worst < m.diff)
      *worst = m.diff;
    
    data->changed[y*TILE_SIZE+x] = 1;
 /* }
  else if (m.diff < *worst) {
    for(i=0;i<*count;i++)
      if (m.x == matches[i].x && m.y == matches[i].y)
	return;
    //insert into the buffer between MAX_AVG_COUNT and MAX_MATCH_COUNT
    if (*count < MAX_MATCH_COUNT) {
      matches[*count] = m;
      (*count)++;
    }
    //sort array and drop matches above MAX_AVG_COUNT
    else {
      qsort(matches, MAX_MATCH_COUNT, sizeof(_Match), cmp_mtach);
      *count = MAX_AVG_COUNT;
      *worst = matches[MAX_AVG_COUNT-1].diff;
    }
  }*/
}

static void calc_avg(_Data *data, Eina_Array *in)
{
  int ch;
  Tiledata *td;
  int i, j;
  uint8_t *buf;
  int width;
  int sum;
  int x, y;
  
  for(ch=0;ch<3;ch++) {
    td = (Tiledata*)ea_data(in, ch);
    width = td->area.width;
    
    for(j=2;j<(TILE_SIZE+2*SEARCH_SIZE)-1;j++)
      for(i=2;i<(TILE_SIZE+2*SEARCH_SIZE)-1;i++) {
	buf = tileptr8(td, i+td->area.corner.x-1, j+td->area.corner.y-1);

	sum = 0;
	for(y=0;y<3;y++,buf+=width) {
	  for(x=0;x<3;x++,buf++)
	    sum += buf[0];
	  buf -= 3;
	}
	sum /= 9;
	
	data->avgs[(j*td->area.width+i)*3+ch] = sum;
      }
  }
}

static void sort_matches(_Data *data, int x, int y)
{ 
  if (!data->changed[y*TILE_SIZE + x])
    return;
  
  qsort(&data->matches[(y*TILE_SIZE + x)*MAX_MATCH_COUNT], data->counts[y*TILE_SIZE + x], sizeof(_Match), cmp_mtach);
  
  if (data->counts[y*TILE_SIZE + x] > MAX_AVG_COUNT) {  
    data->worsts[y*TILE_SIZE + x] = data->matches[(y*TILE_SIZE + x)*MAX_MATCH_COUNT + MAX_AVG_COUNT - 1].diff;
    data->counts[y*TILE_SIZE + x] = MAX_AVG_COUNT;
  }
}

static void match_rand(_Data *data, int x, int y, Eina_Array *in, Rect *area, float max)
{
  int ch;
  _Match m;
  Tiledata *td;
  
  m.x = 0;
  m.y = 0;
  m.diff = 0;
  m.origin = 0;
  while (!m.x && !m.y) {
    if (rand() % 2)
      m.x = rand() * max / RAND_MAX;
    else
      m.x = -rand() * max / RAND_MAX;
    
    if (rand() % 2)
      m.y = rand() * max / RAND_MAX;
    else
      m.y = -rand() * max / RAND_MAX;
  }
  
  for(ch=0;ch<3;ch++) {
    td = (Tiledata*)ea_data(in, ch);
    add_diff(data, &m, x, y, area, td, ch);
  }
  
  insert_match(data, m, x, y);
}

static void match_rand_from_match(_Data *data, int x, int y, Eina_Array *in, Rect *area)
{
  int ch;
  _Match m;
  _Match m2;
  Tiledata *td;
  int max = data->counts[y*TILE_SIZE + x];
  
  if (max > MAX_AVG_COUNT) max = MAX_AVG_COUNT;
  
  m = data->matches[(y*TILE_SIZE + x)*MAX_MATCH_COUNT+(rand() % max)];
  
  if (x + m.x < 0 || x + m.x >= TILE_SIZE)
    return;
  if (y + m.y < 0 || y + m.y >= TILE_SIZE)
    return;
  if (!data->counts[(y+m.y)*TILE_SIZE + (x+m.x)])
    return;
  /*if (!data->changed[y*TILE_SIZE + x])
    return;*/
  
  max = data->counts[(y+m.y)*TILE_SIZE + (x+m.x)];
  if (max > MAX_AVG_COUNT) max = MAX_AVG_COUNT;
  
  m2 = data->matches[((y+m.y)*TILE_SIZE + (x+m.x))*MAX_MATCH_COUNT + (rand() % max)];
  m.x += m2.x;
  m.y += m2.y;
  m.diff = 0;
  m.origin = 1;
  
  for(ch=0;ch<3;ch++) {
    td = (Tiledata*)ea_data(in, ch);
    add_diff(data, &m, x, y, area, td, ch);
  }
  
  insert_match(data, m, x, y);
}

static void match_rand_from(_Data *data, int x, int y, int x_off, int y_off, Eina_Array *in, Rect *area)
{
  int ch;
  _Match m;
  Tiledata *td;
  int max = data->counts[(y+y_off)*TILE_SIZE + (x+x_off)];
  
  if (max > MAX_AVG_COUNT) max = MAX_AVG_COUNT;
  
  m = data->matches[((y+y_off)*TILE_SIZE + (x+x_off))*MAX_MATCH_COUNT + (rand() % max)];
  m.diff = 0;
  m.origin = 2;
  
  /*if (!data->changed[y*TILE_SIZE + x])
    return;*/
  
  for(ch=0;ch<3;ch++) {
    td = (Tiledata*)ea_data(in, ch);
    add_diff(data, &m, x, y, area, td, ch);
  }
  
  insert_match(data, m, x, y);
}

static void limit(Eina_Array *in, Eina_Array *out, Rect *area)
{
  int ch;
  int i, j;
  int filtered, ref;
  int add;
  Tiledata *in_td, *out_td;
  uint8_t *in_buf, *out_buf;
  
  for(ch=0;ch<3;ch++) {
    in_td = (Tiledata*)ea_data(in, ch);
    out_td = (Tiledata*)ea_data(out, ch);
    
    for(j=area->corner.y;j<area->corner.y+area->height;j+=2) {
      in_buf = tileptr8(in_td, area->corner.x, j);
      out_buf = tileptr8(in_td, area->corner.x, j);
      
      for(i=0;i<area->width;i++,in_buf+=2,out_buf+=2) {
	ref = in_buf[0] + in_buf[1] + in_buf[in_td->area.width] + in_buf[in_td->area.width+1];
	filtered = out_buf[0] + out_buf[1] + out_buf[out_td->area.width] + out_buf[out_td->area.width+1];
	
	if (ref-filtered > MAX_SCALE_DIFF*4) {
	  add = (ref-filtered + MAX_SCALE_DIFF*4)/4;
	  out_buf[0] += add;
	  out_buf[1] += add;
	  out_buf[out_td->area.width] += add;
	  out_buf[out_td->area.width+1] += add;
	}
	else if (filtered-ref > MAX_SCALE_DIFF*4) {
	  add = (ref-filtered + MAX_SCALE_DIFF*4)/4;
	  out_buf[0] += add;
	  out_buf[1] += add;
	  out_buf[out_td->area.width] += add;
	  out_buf[out_td->area.width+1] += add;
	}
      }
    }
  }
}

static void avg_matches(_Data *data, Eina_Array *in, Eina_Array *out, Rect *area, int max_diff, int max_diff_c)
{
  int n;
  int ch;
  int i, j;
  float sum;
  Tiledata *in_td, *out_td;
  _Match m;
  int max;
  float count;
  int origins[3] = {0, 0, 0};
  int orig_sum;
  int rough;
  
  for(j=0;j<TILE_SIZE;j++)
    for(i=0;i<TILE_SIZE;i++) {
      
      //TODO sort, limit to MAX_AVG_COUNT!
      //printf("best: %d worst: %d\n", data->matches[(j*TILE_SIZE + i)*MAX_MATCH_COUNT].diff,data->matches[(j*TILE_SIZE + i)*MAX_MATCH_COUNT+data->counts[j*TILE_SIZE + j]-1].diff);

      qsort(&data->matches[(j*TILE_SIZE + i)*MAX_MATCH_COUNT], data->counts[j*TILE_SIZE + j], sizeof(_Match), cmp_mtach);
      
      max = data->counts[j*TILE_SIZE + j];
      if (MAX_AVG_COUNT < max) max = MAX_AVG_COUNT;
      
      for(ch=0;ch<3;ch++) {
	in_td = (Tiledata*)ea_data(in, ch);
	out_td = (Tiledata*)ea_data(out, ch);
	
	sum = tileptr8(in_td, area->corner.x + i, area->corner.y + j)[ch] 
	      - data->avgs[((area->corner.y + j - in_td->area.corner.y)*in_td->area.width + area->corner.x + i - in_td->area.corner.x)*3+ch];
	count = 1.0/(max_diff+0.00000001);
        sum *= count;
	rough = abs(sum);
	
	for(n=0;n<max;n++) {
	  m = data->matches[(j*TILE_SIZE + i)*MAX_MATCH_COUNT + n];
	  
	  /*if (ch) {
	    if (m.diff*m.diff < max_diff_c*(rough+10)*10) {
	      origins[m.origin]++;
	      sum += tileptr8(in_td, area->corner.x + i + m.x, area->corner.y + j + m.y)[0] 
		      - data->avgs[((area->corner.y + j + m.y - in_td->area.corner.y)*in_td->area.width + area->corner.x + i + m.x - in_td->area.corner.x)*3+ch];
	      count++;
	    }
	  }
	  else {*/
	    //if (m.diff*m.diff < max_diff*(rough+10)*10) {
	      origins[m.origin]++;
              float mul = 1.0/fmax(m.diff, max_diff);
	      sum += mul*clip_u8(tileptr8(in_td, area->corner.x + i + m.x, area->corner.y + j + m.y)[0]
	      - data->avgs[((area->corner.y + j + m.y - in_td->area.corner.y)*in_td->area.width + area->corner.x + i + m.x - in_td->area.corner.x)*3+ch]);
	      count += mul;
	    //}
	  //}
	}
	
	tileptr8(out_td, area->corner.x + i, area->corner.y + j)[0] = clip_u8(sum / count
	    + data->avgs[((area->corner.y + j - in_td->area.corner.y)*in_td->area.width + area->corner.x + i - in_td->area.corner.x)*3+ch]);
      }
    }
    
    orig_sum = origins[0] + origins[1] + origins[2];
    printf("rand: %.1f%% match: %.1f%% neigh: %.1f%%\n", origins[0]*100.0/orig_sum, origins[1]*100.0/orig_sum, origins[2]*100.0/orig_sum);
}

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int n;
  int ch;
  int i, j;
  int k;
  Tiledata *in_td, *out_td;
  _Data *data = ea_data(f->data, 0);
  int max_diff = *data->max_diff*9*255*3*0.001;
  int max_diff_c = *data->max_diff_c*9*255*3*0.001;
  float dist_mult = log(SEARCH_SIZE)/(*data->iters)/log(2);
  float dist;
  
  assert(in && ea_count(in) == 3);
  assert(out && ea_count(out) == 3);
  
  if (area->corner.scale) {
    for(ch=0;ch<3;ch++) {
      in_td = (Tiledata*)ea_data(in, ch);
      out_td = (Tiledata*)ea_data(out, ch);
      
      memcpy(out_td->data, in_td->data, TILE_SIZE*TILE_SIZE);
    }
   
    return;
  }
  
  memset(data->counts, 0, sizeof(int)*TILE_SIZE*TILE_SIZE);
  memset(data->worsts, 127, sizeof(int)*TILE_SIZE*TILE_SIZE);
  
  calc_avg(data, in);
  
  for(n=0;n<*data->iters;n++) {
    dist = pow(2.0, (*data->iters - n )*dist_mult);
    
    for(j=0;j<TILE_SIZE;j++)
      for(i=0;i<TILE_SIZE;i++)
      {
	match_rand(data, i, j, in, area, dist);
	//match_rand(data, i, j, in, area, dist);
	//match_rand(data, i, j, in, area, dist);
	if (j) match_rand_from(data, i, j, 0, -1, in, area);
	if (i) match_rand_from(data, i, j, -1, 0, in, area);
	//if (i && j) match_rand_from(data, i, j, -1, -1, in, area);
	//for(k=0;k<n/2;k++)
	//  match_rand_from_match(data, i, j, in, area);
	sort_matches(data, i, j);
    }
    
    n++;
    if (!(n<*data->iters)) {
      for(j=TILE_SIZE-1;j>=0;j--)
	for(i=TILE_SIZE-1;i>=0;i--)
	sort_matches(data, i, j);
      break;
    }
    
    dist = pow(2.0, (*data->iters - n)*dist_mult);
    
    for(j=TILE_SIZE-1;j>=0;j--)
      for(i=TILE_SIZE-1;i>=0;i--)
      {
	match_rand(data, i, j, in, area, dist);
	//match_rand(data, i, j, in, area, dist);
	//match_rand(data, i, j, in, area, dist);
	if (j<TILE_SIZE-1) match_rand_from(data, i, j, 0, 1, in, area);
	if (i<TILE_SIZE-1) match_rand_from(data, i, j, 1, 0, in, area);
	if (i<TILE_SIZE-1 && j<TILE_SIZE-1) match_rand_from(data, i, j, 1, 1, in, area);
	//for(k=0;k<n/2;k++)
	//  match_rand_from_match(data, i, j, in, area);
	sort_matches(data, i, j);
    }
    
    memset(data->changed, 0, TILE_SIZE*TILE_SIZE);
  }
    
  avg_matches(data, in, out, area, max_diff, max_diff_c);
  limit(in, out, area);
}


static Filter *filter_denoise_new(void)
{
  Filter *filter = filter_new(&filter_core_denoise);
  Meta *in, *out, *channel, *color[3], *setting, *bound;
  Meta *ch_out[3];
  _Data *data = calloc(sizeof(_Data), 1);
  data->matches = malloc(sizeof(_Match)*TILE_SIZE*TILE_SIZE*MAX_MATCH_COUNT);
  data->changed = malloc(sizeof(uint8_t)*TILE_SIZE*TILE_SIZE);
  data->counts = malloc(sizeof(int)*TILE_SIZE*TILE_SIZE);
  data->worsts = malloc(sizeof(int)*TILE_SIZE*TILE_SIZE);
  data->avgs = malloc(sizeof(uint8_t)*(TILE_SIZE+2*SEARCH_SIZE)*(TILE_SIZE+2*SEARCH_SIZE)*3);
  data->max_diff = malloc(sizeof(float));
  *data->max_diff = 20.0;
  data->max_diff_c = malloc(sizeof(float));
  *data->max_diff_c = 40.0;
  data->iters = malloc(sizeof(int));
  *data->iters = 2;
  filter->tile_width = TILE_SIZE;
  filter->tile_height = TILE_SIZE;

  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->threadsafe = 0;
  filter->mode_buffer->worker = &_worker;
  filter->mode_buffer->area_calc = &_area_calc;
  filter->mode_buffer->data_new = &_denoise_data_new;
  filter->fixme_outcount = 3;
  ea_push(filter->data, data);
  
  out = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->out, out);
  
  channel = meta_new_channel(filter, 1);
  color[0] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[0]->data) = CS_LAB_L;
  meta_attach(channel, color[0]);
  meta_attach(out, channel);
  ch_out[0] = channel;
  
  channel = meta_new_channel(filter, 2);
  color[1] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[1]->data) = CS_LAB_A;
  meta_attach(channel, color[1]);
  meta_attach(out, channel);
  ch_out[1] = channel;
  
  channel = meta_new_channel(filter, 3);
  color[2] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[2]->data) = CS_LAB_B;
  meta_attach(channel, color[2]);
  meta_attach(out, channel);
  ch_out[2] = channel;
  
  in = meta_new(MT_BUNDLE, filter);
  in->replace = out;
  eina_array_push(filter->in, in);
  
  channel = meta_new_channel(filter, 1);
  color[0]->replace = color[0];
  channel->replace = ch_out[0];
  meta_attach(channel, color[0]);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 2);
  color[1]->replace = color[1];
  channel->replace = ch_out[1];
  meta_attach(channel, color[1]);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 3);
  color[2]->replace = color[2];
  color[2]->replace = color[2];
  channel->replace = ch_out[2];
  meta_attach(channel, color[2]);
  meta_attach(in, channel);
  
  //setting
  setting = meta_new_data(MT_FLOAT, filter, data->max_diff);
  meta_name_set(setting, "luma");
  eina_array_push(filter->settings, setting);
  
  bound = meta_new_data(MT_FLOAT, filter, malloc(sizeof(float)));
  *(float*)bound->data = 0.0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, filter, malloc(sizeof(float)));
  *(float*)bound->data = 100.0;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
 //setting
  setting = meta_new_data(MT_FLOAT, filter, data->max_diff_c);
  meta_name_set(setting, "chroma");
  eina_array_push(filter->settings, setting);
  
  bound = meta_new_data(MT_FLOAT, filter, malloc(sizeof(float)));
  *(float*)bound->data = 0.0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_FLOAT, filter, malloc(sizeof(float)));
  *(float*)bound->data = 100.0;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  //setting
  setting = meta_new_data(MT_INT, filter, data->iters);
  meta_name_set(setting, "iterations");
  eina_array_push(filter->settings, setting);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 2;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 22;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  return filter;
}

Filter_Core filter_core_denoise = {
  "Denoise",
  "denoise",
  "denoise using simplified Non-Local Means via PatchMatch",
  &filter_denoise_new
};