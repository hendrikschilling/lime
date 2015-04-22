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

#include "render.h"

#include <unistd.h>

#include "filter.h"
#include "tile.h"
#include "cache.h"
#include "configuration.h"

#define MODE_INPUT 0 
#define MODE_CLOBBER 1
#define MODE_ITER 2

struct _Render_Node;
typedef struct _Render_Node Render_Node;

struct _Render_State;
typedef struct _Render_State Render_State;

struct _Render_Node {
  Rect area; //area that f needs
  int channel; //current channel in this node
  Pos pos; //current position in this area
  Filter *f; //filter that calculates this tile
  Tile *tile; //tile that wants to be calculated by f
  Eina_Array *inputs;
  int tw, th; //current channel's source filter tile size
  Eina_Array *f_source;
  Filter *f_source_curr;
  int mode;
  int need;
  Render_State *state;
  int depth;
  void *iter;
};

//Problem threading: Was wenn an einem Knoten nicht genug Arbeit anfällt?
//(Lösung1: Müssen mehrere Render_States abspalten können und parallel an diesen Arbeiten)
//(Lösung2: Zustand der traversierung kein array sondern (kleiner) Baum,)
//Lösung3: Mehrere Sorten Tiles, Render-Tiles sind tiles die noch gerendert werden, "Höhere" Knoten können dort ablegen das sie von diesen Tiles abhängen, Render_state enthält nur array des aktuellen fortschritts der suche nach tiles ()
struct _Render_State {
  //speichert top-down den Zustand der Traversierung. Tiles die nicht gecached sind kommen in den "Render-Cache", 
  Eina_Array *currstate; //Render_Nodes
  Eina_Array *ready; //list of tiles which can be processed because all input tiles are available
  int pending; //number of pending jobs
};

/*int do_clobber(Eina_Array *f_source, Filter *f, Rect *area)
{
  int i;
  Filter *source;
  Eina_Array_Iterator iter;
  Rect source_area;
  
  if (f_source)
    return 1;
  else
    return 0;
  
  if (!f_source)
    return 0;
  
  if (f->mode_buffer->area_calc)
    return 1;
  
  EINA_ARRAY_ITER_NEXT(f_source, i, source, iter)
    if (source->mode_buffer) {
      if (area->width != tw_get(source, area) || area->height != th_get(source, area))
	return 1;
      if(area->corner.x % tw_get(source, area) || area->corner.y % th_get(source, area))
	return 1;
    }
  
  return 0;
}*/


void filter_calc_req_area(Filter *f, Rect *area, Rect *req_area)
{
  if (f->mode_buffer && f->mode_buffer->area_calc)
    f->mode_buffer->area_calc(f, area, req_area);
  else
    *req_area = *area;
}

void filter_calc_valid_req_area(Filter *f, Rect *area, Rect *req_area)
{
  Dim *ch_dim;
  int x, y, w, h;
  int div;
  
  //calc area
  filter_calc_req_area(f, area, req_area);
  
  //restrict to prev filters output 
  assert(f->node->con_ch_in && ea_count(f->node->con_ch_in));
  ch_dim = meta_child_data_by_type(ea_data(f->node->con_ch_in, 0), MT_IMGSIZE);
  
  div = 1u<<req_area->corner.scale;

  x = ch_dim->x/div;
  y = ch_dim->y/div;
  w = ch_dim->width/div;
  h = ch_dim->height/div;
  
  if (x > req_area->corner.x) {
    req_area->width = req_area->width - x + req_area->corner.x;
    req_area->corner.x = x;
  }
  if (y > req_area->corner.y) {
    req_area->height = req_area->height - y + req_area->corner.y;
    req_area->corner.y = y;
  }
  if (x + w < req_area->corner.x + req_area->width)
    req_area->width = x + w - req_area->corner.x;
  if (y + h < req_area->corner.y + req_area->height)
    req_area->height = y + h - req_area->corner.y;
}

void clobbertile_add(Tiledata *big, Tiledata *small)
{
  int size;
  int minx, miny, maxx, maxy;
  int width;
  int y;
  
  assert(big);
  assert(small);
  assert(big->area.corner.scale == small->area.corner.scale);
  assert(big->data);
  assert(small->data);
  
  //FIXME proper pixel size handling!
  hack_tiledata_fixsize(small->size, big);
  size = small->size;

  minx = big->area.corner.x;
  if (small->area.corner.x > minx) minx = small->area.corner.x;
  
  miny = big->area.corner.y;
  if (small->area.corner.y > miny) miny = small->area.corner.y;
  
  maxx = big->area.corner.x+big->area.width;
  if (small->area.corner.x+ small->area.width < maxx) maxx = small->area.corner.x+ small->area.width;
  width = maxx - minx;
  
  maxy = big->area.corner.y+big->area.height;
  if (small->area.corner.y + small->area.height < maxy) maxy = small->area.corner.y + small->area.height;
  
  if (maxy <= miny || maxx <= minx) {
      printf("TODO: fix negative coords?\n");
      return;
  }
  
  //FIXME BITDEPTH!!!
  //assert(big->size = small->size);
  for(y=miny;y<maxy;y++) {
    memcpy(big->  data + size*((y-big  ->area.corner.y)*big  ->area.width + minx-big  ->area.corner.x),
	   small->data + size*((y-small->area.corner.y)*small->area.width + minx-small->area.corner.x),
	   width*size);
  }
}

void render_node_del(Render_Node *node)
{
  int i;
  
  if (node->inputs) {
    for(i=0;i<ea_count(node->inputs);i++)
      tiledata_del(ea_data(node->inputs, i));
    eina_array_free(node->inputs);
  }
  
  if (node->f_source)
    eina_array_free(node->f_source);
  
  if (node->tile && !cache_tile_get(&node->tile->hash))
    tile_del(node->tile);
  
  free(node);
}

//f_source: source-filter by channel
Render_Node *render_node_new(Filter *f, Tile *tile, Render_State *state, int depth)
{
  int i;
  Rect inputs_area;
  Render_Node *node = calloc(sizeof(Render_Node), 1);
  Rect *area;
  
  if (tile)
    area = &tile->area;
  else
    area = NULL;
  
  node->state = state;
  node->depth = depth;

  if (f->node->con_ch_in && ea_count(f->node->con_ch_in))
      node->f_source = eina_array_new(4);
  
  for(i=0;i<ea_count(f->node->con_ch_in);i++)
    ea_push(node->f_source,  filter_get_input_filter(f, i));

  
  node->f = f;
  node->tile = tile;
  
  node->inputs = eina_array_new(4);
  
  if (node->f_source && f->mode_iter) {
    node->mode = MODE_ITER;
    
    node->iter = f->mode_iter->iter_new(f, area, node->f_source, &node->pos, &node->channel);
    
    node->f_source_curr = ea_data(node->f_source, node->channel);
        
    node->tw = tw_get(node->f_source_curr, node->area.corner.scale);
    node->th = th_get(node->f_source_curr, node->area.corner.scale);
  }
  else if (node->f_source && f->mode_buffer) {
    node->mode = MODE_CLOBBER;
    
    node->channel = 0;

    assert(area);
    
    filter_calc_valid_req_area(f, area, &node->area);
    
    node->f_source_curr = ea_data(node->f_source, node->channel);
        
    node->tw = tw_get(node->f_source_curr, node->area.corner.scale);
    node->th = th_get(node->f_source_curr, node->area.corner.scale);
    
    assert(f->node->con_ch_in && ea_count(f->node->con_ch_in));
  
    if (node->area.corner.x >= 0)
      node->pos.x = (node->area.corner.x/node->tw)*node->tw;
    else
      node->pos.x = ((node->area.corner.x-node->tw+1)/node->tw)*node->tw;
    if (node->area.corner.y >= 0)
      node->pos.y = (node->area.corner.y/node->th)*node->th;
    else
      node->pos.y = ((node->area.corner.y-node->th+1)/node->th)*node->th;
    node->pos.scale = node->area.corner.scale;

    //this is the input provided to filtes, so it will always be as large as actually requested by the filter
    filter_calc_req_area(f, area, &inputs_area);
    
    for(i=0;i<ea_count(f->node->con_ch_in);i++)
      ea_push(node->inputs, tiledata_new(&inputs_area, 1, NULL));
  }
  else
    node->mode = MODE_INPUT;

  
  return node;
}

void render_state_print(Render_State *state, int t_id) 
{
  int i;
  
  printf("%4d ", t_id);
  
  for(i=0;i<ea_count(state->currstate);i++) {
    printf("%4dx%4d ", ((Render_Node*)ea_data(state->currstate, i))->pos.x, ((Render_Node*)ea_data(state->currstate, i))->pos.y);
  }
  
  printf("\n");
}

void incnode(Render_Node *node)
{
  if (node->mode == MODE_CLOBBER) {
    node->pos.x += node->tw;
    if (node->pos.x >= node->area.corner.x+node->area.width) {
      if (node->area.corner.x >= 0)
	node->pos.x = (node->area.corner.x/node->tw)*node->tw;
      else
	node->pos.x = ((node->area.corner.x-node->tw+1)/node->tw)*node->tw;
      node->pos.y += node->th;
      
      if (node->pos.y >= node->area.corner.y+node->area.height) {
	if (node->area.corner.y >= 0)
	  node->pos.y = (node->area.corner.y/node->th)*node->th;
	else
	  node->pos.y = ((node->area.corner.y-node->th+1)/node->th)*node->th;
	
	node->channel++;
	if (node->channel < ea_count(node->f_source)) {
	  node->f_source_curr = ea_data(node->f_source, node->channel);
	  node->tw = tw_get(node->f_source_curr, node->area.corner.scale);
	  node->th = th_get(node->f_source_curr, node->area.corner.scale);
	  
	  filter_calc_valid_req_area(node->f, &node->tile->area, &node->area);
	  
	  if (node->area.corner.x >= 0)
	    node->pos.x = (node->area.corner.x/node->tw)*node->tw;
	  else
	    node->pos.x = ((node->area.corner.x-node->tw+1)/node->tw)*node->tw;
	  if (node->area.corner.y >= 0)
	    node->pos.y = (node->area.corner.y/node->th)*node->th;
	  else
	    node->pos.y = ((node->area.corner.y-node->th+1)/node->th)*node->th;
	  node->pos.scale = node->area.corner.scale;
	}
      }
    }
  }
  else if (node->mode == MODE_ITER) {
    node->f->mode_iter->iter_next(node->iter, &node->pos, &node->channel);
    
    node->tw = tw_get(node->f_source_curr, node->pos.scale);
    node->th = th_get(node->f_source_curr, node->pos.scale);
  }
  else
    abort();
}



void filter_render_tile(Render_Node *job, int thread_id)
{
  int i;
  struct timespec t_start;
  struct timespec t_stop;
  Eina_Array *channels;
  
  assert(job->f->mode_buffer);
  assert(job->f->mode_buffer->worker != NULL);
  assert(!job->tile->channels);
  assert(job->tile->refs);
  assert(job->mode != MODE_ITER);
  
  if (job->f->fixme_outcount) {
    channels = eina_array_new(4);
  
    for(i=0;i<job->f->fixme_outcount;i++)
      ea_push(channels, tiledata_new(&job->tile->area, 1, job->tile));
  }
  else
    channels = 0;

  if (channels)
    assert(cache_tile_get(&job->tile->hash) != NULL);
  
  if (job->f->prepare && job->f->prepared_hash != job->f->hash.hash) {
    job->f->prepare(job->f);
    job->f->prepared_hash = job->f->hash.hash;
  }
  
  if (job->f->mode_buffer->threadsafe)
    lime_unlock();
  
  if (job->f->mode_buffer->threadsafe)
    filter_fill_thread_data(job->f, thread_id);
  /*else {
   *     if (!job->f->lock) {
   * job->f->lock = calloc(sizeof(pthread_mutex_t),1);
   * pthread_mutex_init(job->f->lock, NULL);
   }
   pthread_mutex_lock(job->f->lock);
   }*/
  
  clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t_start);
  
  if (job->f->mode_buffer->threadsafe)
    job->f->mode_buffer->worker(job->f, job->inputs, channels, &job->tile->area, thread_id);
  else
    job->f->mode_buffer->worker(job->f, job->inputs, channels, &job->tile->area, 0);
    
  
  clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t_stop);
  
  //if (!job->f->mode_buffer->threadsafe)
  //  pthread_mutex_unlock(job->f->lock);
  
  if (job->f->mode_buffer->threadsafe)
    lime_lock();
  
  job->tile->time = t_stop.tv_sec*1000000000 - t_start.tv_sec*1000000000
  +  t_stop.tv_nsec - t_start.tv_nsec;
  
  job->tile->channels = channels;
  cache_stats_update(job->tile, 0, 0, job->tile->time, 0);
  
  //printf("render add %p filter %s\n", job->tile, job->f->fc->shortname);
  //???
  //if (job->f->fixme_outcount)
  //  cache_tile_channelmem_add(job->tile);
}

int end_of_iteration(Render_Node *node)
{
  if (node->mode == MODE_CLOBBER) {
    if (node->channel < ea_count(node->f->node->con_ch_in))
      return 0;
    else
      return 1;
  }
  else if (node->mode == MODE_ITER) {

    int ret = node->f->mode_iter->iter_eoi(node->iter, node->pos, node->channel);
	return ret;
  } 
  else
    abort();
}

//return 0 on success
Render_Node *render_state_getjob( Render_State *state)
{
  int i;
  Rect area;
  Tilehash hash;
  Render_Node *node;
  Tile *tile;
  Render_Node *jobnode;
  int found;
  
  if (ea_count(state->ready))
    return ea_pop(state->ready);
  
  if (!ea_count(state->currstate)) {
    
    if (state->pending) {
      lime_unlock();
      while(state->pending && !ea_count(state->ready)) {
	usleep(1000);
	lime_lock();
	lime_unlock();
      }
      
      lime_lock();
      
      assert(ea_count(state->ready));
      
      return ea_pop(state->ready);
    }

    return NULL;
  }

  node = ea_data(state->currstate, ea_count(state->currstate)-1);
    
  while (node) {
    if (!end_of_iteration(node)) {
      area.corner.x = node->pos.x;
      area.corner.y = node->pos.y;
      area.corner.scale = node->pos.scale;
      area.width = node->tw;
      area.height = node->th;
      
      hash = tile_hash_calc(filter_get_input_filter(node->f, node->channel), &area);
         
      if ((tile = cache_tile_get(&hash))) {
	
	cache_stats_update(tile, 1, 0, 0, 0);
	  
	assert(node->mode);
	  
	//check if we're alredy waiting for this tile on another channel
	found = 0;
	if (tile->want)
	  for(i=0;i<ea_count(tile->want);i++)
	    if (node == ea_data(tile->want, i)) {
	      found = 1;
	      break;
	  }
	  
	  if (found) {
	    //do nothing, we just continue
	    
	  }
	  else if (!tile->channels) {
	    //tile is not yet rendered, push ref
	    
	    if (!tile->want)
	      tile->want = eina_array_new(4);

	    node->need++;
	    ea_push(tile->want, node);
	  }
	  else {
	    if (node->mode == MODE_CLOBBER) {
	      //TODO attention: we assume channels are always processed in the same order
	      clobbertile_add(ea_data(node->inputs, node->channel), ea_data(tile->channels, node->channel));
	      assert(ea_count(node->inputs) > node->channel);
	    }
	    else if (node->mode == MODE_ITER) {
	      node->f->mode_iter->worker(node->f, ea_data(tile->channels, node->channel), node->channel, NULL, NULL, 0);
	    }
	    else
	      abort();
	  }
	  
	incnode(node);
      }
      //this node does not need any input tiles
      else if (!ea_count(node->f_source_curr->node->con_ch_in)) {
	jobnode = render_node_new(node->f_source_curr, tile_new(&area, hash, node->f_source_curr, node->f, node->depth), state, node->depth+1);
	assert(jobnode->f->fixme_outcount);
	cache_tile_add(jobnode->tile);
	cache_stats_update(jobnode->tile, 0, 1, 0, 0);

	//don't incnode so parent node will recheck for this tile
	//the parent will propably be added to tile->need
	return jobnode;
      }
      //node needs input tiles
      else {
	tile = tile_new(&area, hash, node->f_source_curr, node->f, node->depth);
	cache_stats_update(tile, 0, 1, 0, 0);
	if (node->f->fixme_outcount);
	  cache_tile_add(tile);
        
	jobnode = render_node_new(node->f_source_curr, tile, state, node->depth+1);
        
        tile->want = eina_array_new(4);
        node->need++;
        ea_push(tile->want, node);
        incnode(node);
        
        node = jobnode;
        
	//don't incnode so parent node will recheck for this tile
	//the parent will propably be added to tile->need
	
	ea_push(state->currstate, node);
	//to lock it in the currstate array
	node->need += 1000;
      }
    }
    else {
      //all inputs have been processed
      //this doesn't mean that all inputs are available
      jobnode = node;
      
      node = ea_pop(state->currstate);
      node->need -= 1000;
      
      assert(node == jobnode);

      if (jobnode->need) {
	//what happens with jobnode?
	//why this code??
	if (ea_count(state->currstate))
	  node = ea_data(state->currstate, ea_count(state->currstate)-1);
	else
	  node = NULL;
	state->pending++;
      }
      else
	return jobnode;
    }
  }
  
  if (ea_count(state->ready))
    return ea_pop(state->ready);
    
  if (state->pending) {
    lime_unlock();
    while(state->pending && !ea_count(state->ready)) {
      usleep(1000);
      lime_lock();
      lime_unlock();
    }

    lime_lock();
    
    assert(ea_count(state->ready));

    return ea_pop(state->ready);
  } 
  
  return NULL;
}

void render_state_del(Render_State *state)
{
  assert(ea_count(state->currstate) == 0);
  assert(ea_count(state->ready) == 0);
  assert(state->pending == 0);
  
  eina_array_free(state->currstate);
  eina_array_free(state->ready);

  free(state);
}

//Positionen und Größer immer bezogen auf scale
//FIXME check if area is already cached!
Render_State *render_state_new(Rect *area, Filter *f)
{
  Render_State *state = calloc(sizeof(Render_State), 1);  
  Tile *tile;
  
  if (area) {
    tile = tile_new(area, tile_hash_calc(f, area), f, NULL, 1);
  }
  else
    tile = NULL;
  
  Render_Node *node =  render_node_new(f, tile, state, 1); 
  
  node->need = 1000;

  state->currstate = eina_array_new(64);
  state->ready = eina_array_new(64);
  
  eina_array_push(state->currstate, node);
  
  return state;
}

//only internal threading, render full filters (e.g. savetiff, compare)
void lime_render(Filter *f)
{
  Dim *size_ptr;
  Rect area;
  
  lime_config_test(f);
  
  size_ptr = filter_core_by_type(f, MT_IMGSIZE);
  
  if (size_ptr) {
    area.corner.x = 0;
    area.corner.y = 0;
    area.corner.scale = 0;
    area.width = size_ptr->width;
    area.height = size_ptr->height;
    
    lime_render_area(&area, f, 0);
  }
  else
    lime_render_area(NULL, f, 0);
  
}

//render area with external threading
void lime_render_area(Rect *area, Filter *f, int thread_id)
{
  int j;
  Render_Node *waiter;
  Render_Node *job;
  Dim *ch_dim;
  
  if (!f)
    return;
  
  lime_lock();
  
  lime_config_test(f);
  
  lime_filter_config_ref(f);
  
  ch_dim = meta_child_data_by_type(ea_data(f->node->con_ch_in, 0), MT_IMGSIZE);
  
  assert(area->corner.x < DIV_SHIFT_ROUND_UP(ch_dim->width, area->corner.scale));
  assert(area->corner.y < DIV_SHIFT_ROUND_UP(ch_dim->height, area->corner.scale));
  
  Render_State *state = render_state_new(area, f);
  
  while ((job = render_state_getjob(state))) {
    assert(job->need == 0);
    
    if (job->mode == MODE_CLOBBER || job->mode == MODE_INPUT) {
      assert(job->tile->refs > 0);
      filter_render_tile(job, thread_id);
    }
    //MODE_ITER
    else if (job->mode == MODE_ITER) {
      if (job->f->mode_iter->finish)
	job->f->mode_iter->finish(job->f);
    }
    else
      abort();

    if (job->tile && job->tile->want)
      while(ea_count(job->tile->want)) {
	waiter = ea_pop(job->tile->want);
	for(j=0;j<ea_count(waiter->f_source);j++)
	  if (filter_hash_value_get(ea_data(waiter->f_source, j)) == job->tile->filterhash)
	    //FIXME channel selection, use paired channels not blindly the same number!
	    clobbertile_add(ea_data(waiter->inputs, j), ea_data(job->tile->channels, j));
	waiter->need--;
	if (!waiter->need) {
	  ea_push(waiter->state->ready, waiter);
	  waiter->state->pending--;
	}
	//FIXME add proper interface!
	else if (!strcmp(waiter->f->fc->shortname, "savejpeg"))
          cache_stats_print();
      }
    
    if (job->tile)
      job->tile->refs--;

    render_node_del(job);
  }
  
  render_state_del(state);
  
  lime_filter_config_unref(f);
  lime_unlock();
}

