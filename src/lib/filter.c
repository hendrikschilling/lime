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

#include <pthread.h>
#include "filter.h"

#include "configuration.h"
#include "tile.h"

int tw_get(Filter *f, int scale)
{
  if (f->tw_s)
    return f->tw_s[scale];
  
  return f->tile_width;
}

int th_get(Filter *f, int scale)
{
  if (f->th_s)
    return f->th_s[scale];
  
  return f->tile_height;
}

Filter *filter_new(Filter_Core *fc)
{
  Filter *filter = calloc(sizeof(Filter), 1);
  
  filter->fc = fc;
  filter->in = eina_array_new(2);
  filter->out = eina_array_new(2);
  filter->tune = eina_array_new(2);
  filter->settings = eina_array_new(2);
  filter->core = eina_array_new(2);
  filter->node = fg_node_new(filter);
  filter->node_orig = fg_node_new(filter);
  filter->data = eina_array_new(4);
  filter->metas = eina_array_new(8);
  
  filter->tile_width = DEFAULT_TILE_SIZE;
  filter->tile_height = DEFAULT_TILE_SIZE;
  
  pthread_mutex_init(&filter->lock, NULL);
    
  return filter;
}


Filter_Mode_Buffer *filter_mode_buffer_new(void)
{
  Filter_Mode_Buffer *mode = calloc(sizeof(Filter_Mode_Buffer), 1);
  
  return mode;
}

void filter_mode_buffer_del(Filter_Mode_Buffer *mode)
{
  free(mode);
}

Filter_Mode_Iter *filter_mode_iter_new(void)
{
  Filter_Mode_Iter *mode = calloc(sizeof(Filter_Mode_Iter), 1);
  
  return mode;
}

void filter_mode_iter_del(Filter_Mode_Iter *mode)
{
  free(mode);
}

void fg_node_del(Fg_Node *node)
{  
  //FIXME del content!
  //FIXME del node!
  free(node);
}

void filter_fill_thread_data(Filter *f, int thread_id)
{
  if (f->mode_buffer->data_new)
    while (ea_count(f->data) <= thread_id)
      ea_push(f->data, f->mode_buffer->data_new(f, ea_data(f->data, 0)));
}

int lime_setting_type_get(Filter *f, const char *setting)
{  
  int i;
  Meta *m;
  
  if (!ea_count(f->settings))
    return -1;
  
  for(i=0;i<ea_count(f->settings);i++) {
    m = ea_data(f->settings, i);
    if (!strcmp(setting, m->name))
      return m->type;
  }
  
  return -1;
}

int lime_setting_float_set(Filter *f, const char *setting, float value)
{
  int i;
  Meta *m;
  
  if (!ea_count(f->settings))
    return -1;
  
  for(i=0;i<ea_count(f->settings);i++) {
    m = ea_data(f->settings, i);
    if (!strcmp(setting, m->name)) {
      assert(m->data);
      *(float*)m->data = value;
      
      filter_hash_invalidate(f);
      
      if (f->setting_changed)
	f->setting_changed(f);
      
      lime_config_reset(f);
      
      return 0;
    }
  }
  
  return -1;
}

Fg_Node *fg_node_new(Filter *f)
{
  Fg_Node *node = calloc(sizeof(Fg_Node), 1);
  
  node->f = f;
  
  return node;
}

static void _meta2hash(Eina_Hash *hash, Meta *tree)
{
  int i;
  Meta *found = eina_hash_find(hash, tree);
  
  if (found)
    return;
  
  eina_hash_direct_add(hash, tree, tree);
  
  if (tree->childs)
    for(i=0;i<tree->childs->count;i++)
      _meta2hash(hash, ma_data(tree->childs, i));
}

static void _meta_array2hash(Eina_Hash *hash, Eina_Array *trees)
{
  int i;

  for(i=0;i<ea_count(trees);i++)
    _meta2hash(hash, ea_data(trees, i));
}

static void _free_meta(void *data)
{
  meta_del(data);
}

void filter_del(Filter *f)
{
  Eina_Hash *metas;
  
  lime_config_reset(f);
  
  pthread_mutex_destroy(&f->lock);
  
  /*if (f->node->con_trees_in && ea_count(f->node->con_trees_in))
    filter_deconfigure(((Con*)ea_data(f->node->con_trees_in, 0))->source->filter);*/
  
  if (f->node->con_trees_in) {
    if (ea_count(f->node->con_trees_in))
      con_del_real(ea_pop(f->node->con_trees_in));
    eina_array_free(f->node->con_trees_in);
    f->node->con_trees_in = NULL;
  }
  
  if (f->node->con_trees_out) {
    if (ea_count(f->node->con_trees_out))
      con_del_real(ea_pop(f->node->con_trees_out));
    eina_array_free(f->node->con_trees_out);
    f->node->con_trees_out = NULL;
  }
  
  if (f->node_orig->con_trees_in) {
    if (ea_count(f->node_orig->con_trees_in))
      con_del(ea_pop(f->node_orig->con_trees_in));
    eina_array_free(f->node_orig->con_trees_in);
    f->node_orig->con_trees_in = NULL;
  }
  
  if (f->node_orig->con_trees_out) {
    if (ea_count(f->node_orig->con_trees_out))
      con_del(ea_pop(f->node_orig->con_trees_out));
    eina_array_free(f->node_orig->con_trees_out);
    f->node_orig->con_trees_out = NULL;
  }
  
  if (f->node->con_ch_in)
    eina_array_free(f->node->con_ch_in);
  
  if (f->del)
    f->del(f);
  else
    printf("FIXME! del filter %s!\n", f->fc->shortname);
  
  while(ea_count(f->metas))
    meta_del(ea_pop(f->metas));
  
  eina_array_free(f->metas);
  eina_array_free(f->in);
  eina_array_free(f->out);
  eina_array_free(f->tune);
  eina_array_free(f->settings);
  eina_array_free(f->core);
  fg_node_del(f->node);
  fg_node_del(f->node_orig); 
  
  eina_array_free(f->data);
  
  if (f->mode_buffer)
    filter_mode_buffer_del(f->mode_buffer);
  if (f->mode_iter)
    filter_mode_iter_del(f->mode_iter);
  
  if (f->tw_s)
    free(f->tw_s);
  if (f->th_s)
    free(f->th_s);
    
  free(f);
}

int lime_setting_int_set(Filter *f, const char *setting, int value)
{
  int i;
  Meta *m;
  
  if (!ea_count(f->settings))
    return -1;
  
  for(i=0;i<ea_count(f->settings);i++) {
    m = ea_data(f->settings, i);
    if (!strcmp(setting, m->name)) {
      assert(m->data);
      *(int*)m->data = value;
      
      filter_hash_invalidate(f);
      
      if (f->setting_changed)
        f->setting_changed(f);
      
      lime_config_reset(f);
      return 0;
    }
  }
  
  return -1;
}

int lime_setting_string_set(Filter *f, const char *setting, const char *value)
{
  int i;
  Meta *m;
  const char *str;
  
  if (!ea_count(f->settings))
    return -1;
  
  str = eina_stringshare_add(value);
  
  for(i=0;i<ea_count(f->settings);i++) {
    m = ea_data(f->settings, i);
    if (!strcmp(setting, m->name)) {
      m->data = str;
      
      filter_hash_invalidate(f);
      
      if (f->setting_changed)
	f->setting_changed(f);
      
      lime_config_reset(f);
      return 0;
    }
  }
  
  eina_stringshare_del(str);
  return -1;
}

void lime_filter_connect(Filter *source, Filter *sink)
{
  filter_connect(source, 0, sink, 0);
}

Filter *filter_chain_first_filter(Filter *f)
{
  while (f->node_orig->con_trees_in && ea_count(f->node_orig->con_trees_in))
    f = ((Con*)ea_data(f->node_orig->con_trees_in, 0))->source->filter;
  
  return f;
}

Filter *filter_chain_last_filter(Filter *f)
{
  while (f->node_orig->con_trees_out && ea_count(f->node_orig->con_trees_out))
    f = ((Con*)ea_data(f->node_orig->con_trees_out, 0))->sink->filter;
  
  return f;
}

//FIXME on sinks should replace old connection!
Con *filter_connect(Filter *source, int out, Filter *sink, int in)
{
  Con *con = malloc(sizeof(Con));
  
  //no composition yet
  assert(in == 0);
  assert(out == 0);
  
  assert(source->in);
  assert(sink->out);
  
  lime_config_reset(sink);
  lime_config_reset(source);
  
  con->source = eina_array_data_get(source->out, out);
  con->sink = eina_array_data_get(sink->in, in);
  
  if (!source->node_orig->con_trees_out)
    source->node_orig->con_trees_out = eina_array_new(1);
  
  if (!sink->node_orig->con_trees_in)
    sink->node_orig->con_trees_in = eina_array_new(1);
  
  if (!ea_count(source->node_orig->con_trees_out))
    eina_array_push(source->node_orig->con_trees_out, con);
  else {
    while (ea_count(source->node_orig->con_trees_out))
      con_del(ea_pop(source->node_orig->con_trees_out));
    eina_array_push(source->node_orig->con_trees_out, con);
  }
  
  if (!ea_count(sink->node_orig->con_trees_in))
    eina_array_push(sink->node_orig->con_trees_in, con);
  else {
    while (ea_count(sink->node_orig->con_trees_in))
      con_del(ea_pop(sink->node_orig->con_trees_in));
    eina_array_push(sink->node_orig->con_trees_in, con);
  }
  
  filter_hash_invalidate(source);
  
  return con;
}

void con_del(Con *con)
{
  //FIXME als del real?
    if (con->source->filter->node_orig->con_trees_out 
	  && ea_count(con->source->filter->node_orig->con_trees_out))
    ea_pop(con->source->filter->node_orig->con_trees_out);
  if (con->sink->filter->node_orig->con_trees_in 
	  && ea_count(con->sink->filter->node_orig->con_trees_in))
    ea_pop(con->sink->filter->node_orig->con_trees_in);
  
  assert(!ea_count(con->source->filter->node_orig->con_trees_out));
  assert(!ea_count(con->sink->filter->node_orig->con_trees_in));
  
  free(con);
}

void con_del_real(Con *con)
{
  if (con->source->filter->node->con_trees_out 
	  && ea_count(con->source->filter->node->con_trees_out))
    ea_pop(con->source->filter->node->con_trees_out);
  if (con->sink->filter->node->con_trees_in 
	  && ea_count(con->sink->filter->node->con_trees_in))
    ea_pop(con->sink->filter->node->con_trees_in);
  
  assert(!ea_count(con->source->filter->node->con_trees_out));
  assert(!ea_count(con->sink->filter->node->con_trees_in));
  
  free(con);
}

//FIXME on sinks should replace old connection!
Con *filter_connect_real(Filter *source, int out, Filter *sink, int in)
{
  Con *con = malloc(sizeof(Con));
  
  con->source = eina_array_data_get(source->out, out);
  con->sink = eina_array_data_get(sink->in, in);
  
  if (!source->node->con_trees_out)
    source->node->con_trees_out = eina_array_new(1);
  
  if (!sink->node->con_trees_in)
    sink->node->con_trees_in = eina_array_new(1);
  
  if (ea_count(source->node->con_trees_out)) {
    printf("WARNING: have con_trees_out\n");
    while (ea_count(source->node->con_trees_out))
      ea_pop(source->node->con_trees_out);
  }
  if (ea_count(sink->node->con_trees_in)) {
    printf("WARNING: have con_trees_in\n");
    while (ea_count(sink->node->con_trees_in))
      ea_pop(sink->node->con_trees_in);
  }
  
  eina_array_push(source->node->con_trees_out, con);
  eina_array_push(sink->node->con_trees_in, con);
  
  filter_hash_invalidate(source);
  
  return con;
}

void filter_hash_invalidate(Filter *f)
{
  f->hash.len = 0;
}

void filter_hash_recalc(Filter *f)
{
  Filter *next;
  int i;
  int len;
    
  len = snprintf(f->hash.data, 1020, "%u%s", f->hash.prevhash, f->fc->shortname);
  
  if (len >= 1020)
    abort();
  
  for(i=0;i<ea_count(f->settings);i++) {
    len += mt_data_snprint(&f->hash.data[len], 
			   1024-len,
			   ((Meta*)ea_data(f->settings, i))->type,
			   ((Meta*)ea_data(f->settings, i))->data);
    if (len >= 1020)
      abort();
  }
  
  for(i=0;i<ea_count(f->tune);i++) {
    if (!((Meta*)ea_data(f->tune, i))->data) {
      printf("FIXME! no data for tune %d (%s) in %s\n", i, ((Meta*)ea_data(f->tune, i))->name, f->fc->shortname);
      continue;
    }
    //assert(((Meta*)ea_data(f->tune, i))->data);
    len += mt_data_snprint(&f->hash.data[len], 
			   1024-len,
			   ((Meta*)ea_data(f->tune, i))->type,
			   ((Meta*)ea_data(f->tune, i))->data);
    if (len >= 1020)
      abort();
  }
  
  len += snprintf(&f->hash.data[len], 1024-len, "tail");
  
  f->hash.len = len;
  f->hash.hash = eina_hash_superfast(f->hash.data, len);
    
  if (f->node->con_trees_out && ea_count(f->node->con_trees_out)) {
    assert(ea_count(f->node->con_trees_out) == 1);
    
    next = ((Con *)ea_data(f->node->con_trees_out, 0))->sink->filter;
    next->hash.prevhash = f->hash.hash;
    filter_hash_recalc(next);
  }
}

//FIXME proper hash calculation!
Hash *filter_hash_get(Filter *f)
{
  assert(f->hash.len);
  
  return &f->hash;
}

//FIXME proper hash calculation!
uint32_t filter_hash_value_get(Filter *f)
{
  assert(f->hash.len);
  
  return f->hash.hash;
}

//FIXME proper hash calculation!
uint32_t hash_hash_value_get(Hash *h)
{
  assert(h->len);
  
  return h->hash;
}

//gives the predecessor according to channel
Filter *filter_get_input_filter(Filter *f,  int channel)
{
  Filter *input;
  
  if (!f->node->con_ch_in)
    return NULL;
  
  if (ea_count(f->node->con_ch_in) <= channel)
    return NULL;
  
  input = ((Meta*)ea_data(f->node->con_ch_in, channel))->filter;
  
  //FIXME too simple, channels?
  while (!input->mode_buffer && !input->mode_iter)
    input = filter_get_input_filter(input,  channel);
  
  return input;
}

//FIXME proper hash calculation
Tilehash tile_hash_calc(Filter *f, Rect *area)
{
  Tilehash h;
  char buf[1024];
  int len;
  
  h.filterhash = filter_hash_get(f);
  
  len = sprintf(buf, "%d%d%d%d%d%dtail", h.filterhash->hash, area->corner.x, area->corner.y, area->width, area->height, area->corner.scale);
  
  h.tilehash = eina_hash_superfast(buf, len);
  h.area = *area;
  
  return h;
}

void *filter_core_by_name(Filter *f, const char *name)
{
  int i;
  for(i=0;i<ea_count(f->core);i++)
    if (!strcmp(name, ((Meta*)ea_data(f->core, i))->name))
      return ((Meta*)ea_data(f->core, i))->data;
    
  return NULL;
}

void *filter_core_by_type(Filter *f, int type)
{
  int i;
  for(i=0;i<ea_count(f->core);i++)
    if (type == ((Meta*)ea_data(f->core, i))->type)
      return ((Meta*)ea_data(f->core, i))->data;
    
  return NULL;
}


void vizp_filter(FILE *file, Filter *filter)
{
  int begun = 0;
  fprintf(file, "subgraph cluster_%p {\n"
  "label = \"%s\";\n"
  "node [style=filled];\n"
  "\"%p\" [label = \"{<type>%s ",
  filter, filter->fc->name, filter, filter->fc->name);
  
  if (ea_count(filter->in)) {
    fprintf(file, "| { <in>in ");
    begun = 1;
  }
  
  if (ea_count(filter->out)) {
    if (begun)
      fprintf(file, "| <out>out ");
    else {
      fprintf(file, "| { <out>out ");
      begun = 1;
    }
  }
  
  if (ea_count(filter->core)) {
    if (begun)
      fprintf(file, "| <core>core ");
    else {
      fprintf(file, "| { <core>core ");
      begun = 1;
    }
  }
  
  if (ea_count(filter->tune)) {
    if (begun)
      fprintf(file, "| <tune>tune ");
    else {
      fprintf(file, "| { <tune>tune ");
      begun = 1;
    }
  }
  
  if (ea_count(filter->settings)) {
    if (begun)
      fprintf(file, "| <settings>settings ");
    else {
      fprintf(file, "| { <settings>settings ");
      begun = 1;
    }
  }
  
  fprintf(file, "}}\"]\n");
  
  vizp_ar(file, filter->in, filter, "in");
  vizp_ar(file, filter->out, filter, "out");
  vizp_ar(file, filter->core, filter, "core");
  vizp_ar(file, filter->tune, filter, "tune");
  vizp_ar(file, filter->settings, filter, "settings");
  
  fprintf(file, "}\n");
}