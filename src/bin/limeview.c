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

#include <Elementary.h>
#include <Ecore.h>
#include <Eio.h>
#include <fcntl.h>
#include <libgen.h>

#include "Lime.h"
#include "cli.h"
#include "tagfiles.h"
#include "cache.h"

#include "filter_convert.h"
#include "filter_gauss.h"
#include "filter_contrast.h"
#include "filter_downscale.h"
#include "filter_memsink.h"
#include "filter_loadtiff.h"
#include "filter_load.h"
#include "filter_savetiff.h"
#include "filter_comparator.h"
#include "filter_sharpen.h"
#include "filter_denoise.h"
#include "filter_pretend.h"
#include "filter_crop.h"
#include "filter_simplerotate.h"
#include "filter_interleave.h"
#include "filter_savejpeg.h"

#define TILE_SIZE DEFAULT_TILE_SIZE

#define PENDING_ACTIONS_BEFORE_SKIP_STEP 3
#define REPEATS_ON_STEP_HOLD 2

//#define BENCHMARK
#define BENCHMARK_LENGTH 498

//FIXME adjust depending on speed!
#define PRECALC_CONFIG_RANGE 4
#define PRELOAD_THRESHOLD 8

int high_quality_delay =  300;
int max_reaction_delay =  1000;
int fullscreen = 0;
int max_fast_scaledown = 5;
int first_preview = 0;
int filtered_image_count = 0;
Ecore_Job *workerfinish_idle = NULL;
Ecore_Idler *idle_render = NULL;
Ecore_Idler *idle_progress_print = NULL;
Ecore_Timer *timer_render = NULL;
Eina_Array *taglist_add = NULL;
Eina_Array *preload_list;
int quick_preview_only = 0;
int cur_key_down = 0;
int key_repeat = 0;

int config_waitfor_idx;
int config_waitfor_groupidx;

Tagfiles *files = NULL;
Tagfiles *files_new = NULL;
int last_file_step = 1;

Elm_Genlist_Item_Class *tags_list_itc;
Elm_Genlist_Item_Class *tags_filter_itc;

typedef struct {
  const char *extensions;
  char *path;
  Eina_Array *list;
  Evas_Object *eo_progress;
  int count;
} Export_Data;

typedef struct {
  Filter *f;
  Evas_Object *frame;
  Elm_Object_Item *item;
} Filter_Chain;

typedef struct {
  int failed;
  Filter *load, *sink;
  Ecore_Thread *running;
  Eina_List *filter_chain;
  Eina_List *filters;
} Config_Data;

typedef struct {
  Eina_Matrixsparse **mats;
  Evas_Object **high_of_layer;
  Evas_Object **low_of_layer;
  int scale_max;
  Eina_Array **imgs;
} Mat_Cache;

int max_workers;
int max_thread_id;
Evas_Object *clipper, *win, *scroller, *file_slider, *filter_list, *select_filter, *pos_label, *fsb, *load_progress, *load_label, *load_notify;
Evas_Object *tab_group, *tab_filter, *tab_settings, *tab_tags, *tab_current, *tab_box, *tab_export, *tab_tags, *tags_list, *tags_filter_list, *seg_rating;
Evas_Object *group_list, *export_box, *export_extensions, *export_path, *main_vbox;
Evas_Object *grid = NULL, *vbox_bottom, *bg;
char *labelbuf;
char *pos_lbl_buf;
int posx, posy;
Evas_Object *slider_blur, *slider_contr, *gridbox = NULL;
int cache_size;
Dim size;
Mat_Cache *mat_cache = NULL;
Mat_Cache *mat_cache_old = NULL;
int forbid_fill = 0;
Eina_List *filter_last_selected = NULL;
Filter *(*select_filter_func)(void);
int verbose;
Eina_Array *finished_threads = NULL;
Ecore_Timer *preview_timer = NULL;
void fc_new_from_filters(Eina_List *filters);
int fixme_no_group_select = 0;
File_Group *cur_group = NULL;

int bench_idx = 0;

int *thread_ids;

float scale_goal = 1.0;
Bench_Step *bench;
int fit;

Config_Data *config_curr = NULL;

int worker = 0;
int worker_config = 0;
int worker_preload = 0;

typedef struct {
  void (*action)(void *data, Evas_Object *obj);
  void *data;
  Evas_Object *obj;
} _Pending_Action;

Eina_List *pending_actions = NULL;

Eina_Hash *tags_filter = NULL;
int tags_filter_rating = 0;
char *dir;

static void on_scroller_move(void *data, Evas_Object *obj, void *event_info);
static void fill_scroller(void);
void workerfinish_schedule(void (*func)(void *data, Evas_Object *obj), void *data, Evas_Object *obj, Eina_Bool append);
void filter_settings_create_gui(Eina_List *chain_node, Evas_Object *box);
void step_image_do(void *data, Evas_Object *obj);

int pending_action(void)
{
  if (pending_actions)
    return eina_list_count(pending_actions);
  
  return EINA_FALSE;
}

void pending_exe(void)
{
  _Pending_Action *action;
  
  while (pending_action() && !worker) {
    action = eina_list_data_get(pending_actions);
    pending_actions = eina_list_remove_list(pending_actions, pending_actions);

    action->action(action->data, action->obj);
  }
}

void pending_add(void (*action)(void *data, Evas_Object *obj), void *data, Evas_Object *obj)
{
  _Pending_Action *pending = malloc(sizeof(_Pending_Action));
  
  pending->action = action;
  pending->data = data;
  pending->obj = obj;
  
  pending_actions = eina_list_append(pending_actions, pending);
}

Filter_Chain *fc_new(Filter *f)
{
  Filter_Chain *fc = calloc(sizeof(Filter_Chain), 1);
  fc->f = f;
  
  return fc;
}

void delgrid(void);

typedef struct {
  Evas_Object *img;
  Config_Data *config;
  int scale;
  uint8_t *buf;
  Rect area;
  int t_id;
  int packx, packy, packw, packh;
  int show_direct;
} _Img_Thread_Data;

void size_recalc(void)
{  
  Dim *size_ptr;
  size_ptr = (Dim*)filter_core_by_type(config_curr->sink, MT_IMGSIZE);
  if (size_ptr) {
    size = *size_ptr;
  }
}

Dim *size_recalc2(Filter *sink)
{
  return filter_core_by_type(sink, MT_IMGSIZE);
}

void grid_setsize(void)
{
  int x,y,w,h;
  
  if (!config_curr)
    return;
    
  forbid_fill++;
  
  size_recalc();
  
  if (size.width && size.height) {
    elm_grid_size_set(grid, size.width, size.height);
    elm_grid_pack_set(clipper, 0, 0, size.width, size.height);
    elm_box_recalculate(gridbox);
  }
  else {
    /*elm_grid_size_set(grid, 200, 200);
    elm_grid_pack_set(clipper, 0, 0, 200, 200);
    elm_box_recalculate(gridbox);*/
    forbid_fill--;
    return;
  }
  
  //FIXME useful?
  //if (forbid_fill)
    //return;
  
  
  if (fit) {

    elm_scroller_region_get(scroller,&x,&y,&w,&h);

    //FIXME!!!
    if (!w || !h) {
      printf("scroller has no area!\n");
      forbid_fill--;
      return;
    }
    else {
      scale_goal = (float)size.width / w;	
      if ((float)size.height / h > scale_goal)
	scale_goal = (float)size.height / h;
    }
  }
    
  evas_object_size_hint_min_set(grid,  size.width/scale_goal, size.height/scale_goal);
  elm_box_recalculate(gridbox);
  
  forbid_fill--;
}


void int_changed_do(void *data, Evas_Object *obj)
{
  Meta *m = data;
  
  assert(!worker);
  assert(cur_group);
  assert(config_curr);

  forbid_fill++;
  
  bench_delay_start();
  
  lime_setting_int_set(m->filter, m->name, (int)elm_spinner_value_get(obj));
  
  lime_config_test(config_curr->sink);
  
  filegroup_filterchain_set(cur_group, lime_filter_chain_serialize(((Filter_Chain*)eina_list_data_get(eina_list_next(config_curr->filter_chain)))->f));
  
  delgrid();
  
  size_recalc();
  
  forbid_fill--;
  
  fill_scroller();
}

void float_changed_do(void *data, Evas_Object *obj)
{
  Meta *m = data;
  
  assert(!worker);
  assert(cur_group);
  
  forbid_fill++;
  
  bench_delay_start();
  
  lime_setting_float_set(m->filter, m->name, (float)elm_spinner_value_get(obj));
  
  lime_config_test(config_curr->sink);
  
  filegroup_filterchain_set(cur_group, lime_filter_chain_serialize(((Filter_Chain*)eina_list_data_get(eina_list_next(config_curr->filter_chain)))->f));
  
  delgrid();
  
  size_recalc();
  
  forbid_fill--;
  
  fill_scroller();
}

void del_filter_settings(void);

void remove_filter_do(void *data, Evas_Object *obj)
{ 
  Eina_List *chain_node = data;
  Filter_Chain *fc = eina_list_data_get(chain_node);
  Filter_Chain *prev, *next;
  
  assert(!worker);
  assert(cur_group);
  assert(config_curr);

  forbid_fill++;
  
  //we can not delete the first (input) or last (output) filter in the chain
  assert(eina_list_prev(chain_node));
  assert(eina_list_next(chain_node));
  
  prev = eina_list_data_get(eina_list_prev(chain_node));
  next = eina_list_data_get(eina_list_next(chain_node));
  
  filter_connect(prev->f, 0, next->f, 0);
  
  filter_del(fc->f);
  
  del_filter_settings(); 
  elm_object_item_del(fc->item);
  config_curr->filter_chain = eina_list_remove_list(config_curr->filter_chain, chain_node);
  
  lime_config_test(config_curr->sink);
 
  filegroup_filterchain_set(cur_group, lime_filter_chain_serialize(((Filter_Chain*)eina_list_data_get(eina_list_next(config_curr->filter_chain)))->f));
  
  forbid_fill--;
  
  //FIXME just call fill_...?
  step_image_do(NULL, NULL);
}

void _on_filter_select(void *data, Evas_Object *obj, void *event_info)
{ 
  del_filter_settings();
  
  filter_settings_create_gui(data, tab_filter);
  filter_last_selected = data;
}

void fc_insert_filter(Filter *f, Eina_List *src, Eina_List *sink)
{
  Filter_Chain *fc_src, *fc_sink;
  Filter_Chain *fc;
  fc = fc_new(f);
  Eina_List *chain_node = NULL;
    
  assert(cur_group);
  assert(config_curr);
  
  //we always have src or sink, but those might not have an elm_item!
  assert(src);
  assert(sink);
  
  fc_src = eina_list_data_get(src);
  fc_sink = eina_list_data_get(sink);
  
  config_curr->filter_chain = eina_list_append_relative_list(config_curr->filter_chain, fc, src);
  chain_node = config_curr->filter_chain;
  while (eina_list_data_get(chain_node) != fc)
    chain_node = eina_list_next(chain_node);

  if (fc_src->item)
    fc->item = elm_list_item_insert_after(filter_list, fc_src->item, f->fc->name, NULL, NULL, &_on_filter_select, chain_node);
  else if (fc_sink->item)
    fc->item = elm_list_item_insert_before(filter_list, fc_sink->item, f->fc->name, NULL, NULL, &_on_filter_select, chain_node);
  else
    fc->item = elm_list_item_append(filter_list, f->fc->name, NULL, NULL, &_on_filter_select, chain_node);
  
  elm_list_item_selected_set(fc->item, EINA_TRUE);
  elm_list_go(filter_list);
  
  filter_connect(fc_src->f, 0, f, 0);
  filter_connect(f, 0, fc_sink->f, 0);
  
  filegroup_filterchain_set(cur_group, lime_filter_chain_serialize(((Filter_Chain*)eina_list_data_get(eina_list_next(config_curr->filter_chain)))->f));
  
  delgrid();
  
  //FIXME do we need this, shouldn't size recalc trigger reconfigure?
  lime_config_test(fc_sink->f);
  size_recalc();
  //size_recalc();
}

void insert_before_do(void *data, Evas_Object *obj)
{
  Eina_List *source_l, *sink_l;
  
  assert(config_curr);
  
  if (!select_filter_func) return;
  
  if (!filter_last_selected) {
    sink_l = eina_list_last(config_curr->filter_chain);
    source_l = eina_list_prev(sink_l);
  }
  else {
    sink_l = filter_last_selected;
    source_l = eina_list_prev(sink_l);
  }
  
  fc_insert_filter(select_filter_func(), source_l, sink_l);
  
  fill_scroller();
}

void insert_rotation_do(void *data, Evas_Object *obj)
{
  Filter *f;
  int rotation = (intptr_t)data;
  Eina_List *src, *sink;
  
  sink = eina_list_last(config_curr->filter_chain);
  src = eina_list_prev(sink);

  f = filter_core_simplerotate.filter_new_f();
  lime_setting_int_set(f, "rotation", rotation); 
  
  fc_insert_filter(f, src, sink);
  
  fill_scroller();
}

void insert_after_do(void *data, Evas_Object *obj)
{
  Eina_List *source_l, *sink_l;
  
  assert(config_curr);
  
  if (!select_filter_func) return;
  
  if (!filter_last_selected) {
    source_l = config_curr->filter_chain;
    sink_l = eina_list_next(source_l);
  }
  else {
    source_l = filter_last_selected;
    sink_l = eina_list_next(source_l);
  }
    
  fc_insert_filter(select_filter_func(), source_l, sink_l);
  
  fill_scroller();
}

static void
on_int_changed(void *data, Evas_Object *obj, void *event_info)
{
  workerfinish_schedule(&int_changed_do, data, obj, EINA_TRUE);
  
}

static void on_select_filter_select(void *data, Evas_Object *obj, void *event_info)
{
  Filter_Core *fc = data;
  
  elm_object_text_set(select_filter, fc->shortname);
  select_filter_func = fc->filter_new_f;
}


static void
on_remove_filter(void *data, Evas_Object *obj, void *event_info)
{
  bench_delay_start();
  
  workerfinish_schedule(&remove_filter_do, data, obj, EINA_TRUE);
}

static void
on_float_changed(void *data, Evas_Object *obj, void *event_info)
{ 
  workerfinish_schedule(&float_changed_do, data, obj, EINA_TRUE);
}

void setting_spinner_insert(Evas_Object *vbox, Meta *setting)
{
  int i;
  Meta *sub;
  Evas_Object *spinner;
  int imin = 0, imax = 100, istep = 1;
  float fmin = 0.0, fmax = 100.0, fstep = 0.1;
  
  spinner = elm_spinner_add(vbox);

  //elm_spinner_unit_format_set(spinner, "%.1f");
  evas_object_size_hint_weight_set(spinner, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(spinner, EVAS_HINT_FILL, 0);
  if (setting->type == MT_INT) {
    evas_object_smart_callback_add(spinner, "delay,changed", on_int_changed, setting);
    for(i=0;i<ma_count(setting->childs);i++) {
      sub = ma_data(setting->childs, i);
      assert(sub->type == setting->type);
      if (!strcmp("PARENT_SETTING_MIN", sub->name))
  imin = *(int*)sub->data;
      else if (!strcmp("PARENT_SETTING_MAX", sub->name))
  imax = *(int*)sub->data;
      else if (!strcmp("PARENT_SETTING_STEP", sub->name))
        istep = *(int*)sub->data;
      
    }
    elm_spinner_min_max_set(spinner, (float)imin, (float)imax);
    if (!istep)
      fstep = 0.01;
    else
      fstep = istep;
    elm_spinner_step_set(spinner, fstep);
    elm_spinner_label_format_set(spinner, "%.0f");
    elm_spinner_value_set(spinner, (float)*(int*)setting->data);
  }
  else if (setting->type == MT_FLOAT) {
    evas_object_smart_callback_add(spinner, "delay,changed", on_float_changed, setting);
    for(i=0;i<ma_count(setting->childs);i++) {
      sub = ma_data(setting->childs, i);
      assert(sub->type == setting->type);
      if (!strcmp("PARENT_SETTING_MIN", sub->name))
  fmin = *(float*)sub->data;
      else if (!strcmp("PARENT_SETTING_MAX", sub->name))
  fmax = *(float*)sub->data;
      else if (!strcmp("PARENT_SETTING_STEP", sub->name))
        fstep = *(float*)sub->data;
    }
    elm_spinner_min_max_set(spinner, fmin, fmax);
    if (!fstep)
      fstep = 0.01;
    
    elm_spinner_step_set(spinner, fstep);
    elm_spinner_label_format_set(spinner, "%.3f");
    elm_spinner_value_set(spinner, *(float*)setting->data);
  }
  elm_box_pack_end(vbox, spinner);
  evas_object_show(spinner);
}

void filter_settings_create_gui(Eina_List *chain_node, Evas_Object *box)
{
  int i;
  Evas_Object *vbox, *btn;
  Filter_Chain *fc = eina_list_data_get(chain_node);
  
  if (!fc->f->settings || !ea_count(fc->f->settings))
    return;
  
  fc->frame = elm_frame_add(win);
  evas_object_size_hint_weight_set(fc->frame, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(fc->frame, EVAS_HINT_FILL, 0);
  elm_box_pack_end(box, fc->frame);
  elm_object_text_set(fc->frame, fc->f->fc->name);
  
  vbox = elm_box_add(fc->frame);
  elm_box_horizontal_set(vbox, EINA_FALSE);
  evas_object_size_hint_weight_set(vbox, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(vbox, EVAS_HINT_FILL, 0);
  elm_object_content_set(fc->frame, vbox);
  
  btn = elm_button_add(fc->frame);
  elm_object_text_set(btn, "remove");
  evas_object_smart_callback_add(btn, "clicked", &on_remove_filter, chain_node);
  elm_box_pack_end(vbox, btn);
  evas_object_show(btn);
  
  
  for(i=0;i<ea_count(fc->f->settings);i++)
    setting_spinner_insert(vbox, ea_data(fc->f->settings, i));
  
  evas_object_show(vbox);
  evas_object_show(fc->frame);
}

void del_filter_settings(void)
{
  Filter_Chain *chain_data;
    
  if (filter_last_selected) {
    chain_data = eina_list_data_get(filter_last_selected);
    elm_box_unpack(tab_filter, chain_data->frame);
    evas_object_del(chain_data->frame);
    chain_data->frame = NULL;
  }
  
  filter_last_selected = NULL;
}

Mat_Cache *mat_cache_new(void)
{
  Mat_Cache *mat_cache = calloc(sizeof(Mat_Cache), 1);
  
  mat_cache->scale_max = -1;
  
  return mat_cache;
}

void mat_cache_flush(Mat_Cache *mat_cache)
{
  int i;
//  _Img_Thread_Data *tdata;
  Eina_Iterator *iter;
  //uint8_t *buf;
  
  for(i=0;i<=mat_cache->scale_max;i++) {
    if (mat_cache->mats[i]) { 
      /*iter = eina_matrixsparse_iterator_new(mat_cache->mats[i]);
      while (eina_iterator_next (iter, &img)) {
	buf = evas_object_image_data_get(img, EINA_TRUE);
	evas_object_image_data_set(img, NULL);
	free(buf);
      }
      eina_iterator_free(iter);*/
      
      eina_matrixsparse_free(mat_cache->mats[i]);
      mat_cache->mats[i] = NULL;
    }
    mat_cache->low_of_layer[i] = NULL;
    mat_cache->high_of_layer[i] = NULL;
  }
}

void mat_cache_check(Mat_Cache *mat_cache)
{
  int layer = 0;
  float scale = scale_goal;
//  Eina_Iterator *iter;
//  Evas_Object *img;
  
  while (scale > 1.0) {
    /*iter = eina_matrixsparse_iterator_new(mat_cache->mats[layer]);
    
    while (eina_iterator_next (iter, &img)) {
      evas_object_del(img);
    }
    eina_iterator_free(iter);*/
    
    //eina_matrixsparse_size_set(mat_cache->mats[layer], 1, 1);
    //eina_matrixsparse_data_idx_set(mat_cache->mats[layer], 0, 0, NULL);
    mat_cache->low_of_layer[layer] = NULL;
    mat_cache->high_of_layer[layer] = NULL;
    
    layer++;
    scale /= 2;
  }
}

void mat_cache_del(Mat_Cache *mat_cache)
{
  mat_cache_flush(mat_cache);  
  free(mat_cache);
}

void mat_cache_max_set(Mat_Cache *mat_cache, int scale)
{
  int i;
  
  if (scale <= mat_cache->scale_max)
    return;
  
  mat_cache->mats = realloc(mat_cache->mats, sizeof(Eina_Matrixsparse*)*(scale+1));
  mat_cache->imgs = realloc(mat_cache->imgs, sizeof(_Img_Thread_Data*)*(scale+1));
  mat_cache->high_of_layer = realloc(mat_cache->high_of_layer, sizeof(Evas_Object*)*(scale+1));
  mat_cache->low_of_layer = realloc(mat_cache->low_of_layer, sizeof(Evas_Object*)*(scale+1));
  
  for(i=mat_cache->scale_max+1;i<=scale;i++) {
    mat_cache->mats[i] = NULL;
    mat_cache->high_of_layer[i] = NULL;
    mat_cache->low_of_layer[i] = NULL;
    mat_cache->imgs[i] = NULL;
  }
    
  mat_cache->scale_max = scale;
}

void *mat_cache_get(Mat_Cache *mat_cache, int scale, int x, int y)
{
  long unsigned int mx, my;
  
  if (scale > mat_cache->scale_max)
    return NULL;

  if (!mat_cache->mats[scale])
    return NULL;

  eina_matrixsparse_size_get(mat_cache->mats[scale], &mx, &my);

  if (x >= mx)
    return NULL;

 if (y >= my)
    return NULL;
 
  return eina_matrixsparse_data_idx_get(mat_cache->mats[scale], x, y);
}

void mat_cache_obj_stack(Mat_Cache *mat_cache, Evas_Object *obj, int scale)
{
  int i;
  
  mat_cache_max_set(mat_cache, scale);
  
  if (mat_cache->high_of_layer[scale]) {
    evas_object_stack_above(obj, mat_cache->high_of_layer[scale]);
    mat_cache->high_of_layer[scale] = obj;
    return;
  } 
  
  for(i=scale-1;i>=0;i--)
    if (mat_cache->low_of_layer[i]) {
      evas_object_stack_below(obj, mat_cache->low_of_layer[i]);
      mat_cache->low_of_layer[scale] = obj;
      mat_cache->high_of_layer[scale] = obj;
      return;
    }
    
    
  for(i=scale+1;i<=mat_cache->scale_max;i++)
    if (mat_cache->high_of_layer[i]) {
      evas_object_stack_above(obj, mat_cache->high_of_layer[i]);
      mat_cache->low_of_layer[scale] = obj;
      mat_cache->high_of_layer[scale] = obj;
      return;
    }
    
  mat_cache->low_of_layer[scale] = obj;
  mat_cache->high_of_layer[scale] = obj;
}

float actual_scale_get()
{
  int x,y,w,h,grid_w,grid_h;
  
  elm_scroller_region_get(scroller, &x, &y, &w, &h);
  evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
    
  return (float)size.width / grid_w;
}
float config_actual_scale_get(Config_Data *config)
{
  int x,y,w,h,grid_w,grid_h;
  
  elm_scroller_region_get(scroller, &x, &y, &w, &h);
  evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
    
  return (float)size_recalc2(config->sink)->width / grid_w;
}

void mat_free_func(void *user_data, void *cell_data)
{
  _Img_Thread_Data *cell = cell_data;
  
  evas_object_image_data_set(cell->img, NULL);
  evas_object_del(cell->img);
  cache_app_del(cell->buf, TILE_SIZE*TILE_SIZE*4);
  free(cell);
}

void mat_cache_set(Mat_Cache *mat_cache, int scale, int x, int y, void *data)
{
  long unsigned int mx, my;
  
  mat_cache_max_set(mat_cache, scale);
  
  if (!mat_cache->mats[scale])
    mat_cache->mats[scale] = eina_matrixsparse_new(x+1, y+1, &mat_free_func, NULL);  
  
  eina_matrixsparse_size_get(mat_cache->mats[scale], &mx, &my);

  if (x >= mx) {
    eina_matrixsparse_size_set(mat_cache->mats[scale], x+1, my);
    //FIXME this should not be necessary!
    eina_matrixsparse_size_get(mat_cache->mats[scale], &mx, &my);
  }
  
  if (y >= my) {
    eina_matrixsparse_size_set(mat_cache->mats[scale], mx, y+1);
    //FIXME just tried if duplicate of above code helbs
    eina_matrixsparse_size_get(mat_cache->mats[scale], &mx, &my);
  }
  
  //printf("pos: %dx%d before %dx%d\n", eina)
  eina_matrixsparse_data_idx_set(mat_cache->mats[scale], x, y, data);
}

void elm_exit_do(void *data, Evas_Object *obj)
{
  elm_exit();
}

Eina_Bool timer_run_render(void *data)
{
  timer_render = NULL;
  
  if (pending_action() && worker)
    return ECORE_CALLBACK_CANCEL;
  
  quick_preview_only = 0;
  
  fill_scroller();
  
  return ECORE_CALLBACK_CANCEL;
}

Eina_Bool idle_run_render(void *data)
{
  if (!pending_action()) {
    if (quick_preview_only) {
      //FIXME what time is good?
      //timer_run_render(NULL);
      quick_preview_only = 0;
      fill_scroller();
      //timer_render = ecore_timer_add(FAST_SKIP_RENDER_DELAY, &timer_run_render, NULL);
  }
    else {
      fill_scroller();
    }
  }
  
  idle_render = NULL;
  
  
#ifdef BENCHMARK
  if (!worker) {
    bench_delay_start();
    if (tagfiles_idx(files) >= BENCHMARK_LENGTH)
      workerfinish_schedule(&elm_exit_do, NULL, NULL, EINA_TRUE);
    else
      workerfinish_schedule(&step_image_do, (void*)(intptr_t)1, NULL, EINA_TRUE);
  }
#endif
  
  return ECORE_CALLBACK_CANCEL;
}

void workerfinish_idle_run(void *data)
{ 
  workerfinish_idle = NULL;
  
  assert(!worker);
  
  pending_exe();
  
  return ECORE_CALLBACK_CANCEL;
}



void preload_add(_Img_Thread_Data *tdata)
{
  eina_array_push(preload_list, tdata);
  //preload_list = eina_list_append(eina_list_last(preload_list), tdata);
  //preload_count++;
}

int preload_pending(void)
{
  return ea_count(preload_list);
}


void preload_flush(void)
{
  //eina_array_flush(preload_list);
}


_Img_Thread_Data *preload_get(void)
{
  /*assert(preload_count);
  _Img_Thread_Data *tdata = eina_list_data_get(eina_list_last(preload_list));
  preload_list = eina_list_remove_list(preload_list, eina_list_last(preload_list));
  preload_count--;
  
  return tdata;*/
  return ea_pop(preload_list);
}

void workerfinish_schedule(void (*func)(void *data, Evas_Object *obj), void *data, Evas_Object *obj, Eina_Bool append)
{
  //FIXME free stuff
  //FIXME need to flush old preloads!
  //eina_array_flush(preload_list);
  
  if (idle_render) {    
    ecore_idler_del(idle_render);
    idle_render = NULL;
  }
  
  if (timer_render) {
    ecore_timer_del(timer_render);
    timer_render = NULL;
  }
  
  if (append || !pending_action())
    pending_add(func, data, obj);
  
  if (!worker) {
    if (!workerfinish_idle) {
      workerfinish_idle = ecore_job_add(workerfinish_idle_run, NULL);
    }
    //pending_exe();
  }
  else {  
    quick_preview_only = 1;
  }
}

static void _process_tile(void *data, Ecore_Thread *th)
{
  _Img_Thread_Data *tdata = data;
    
  eina_sched_prio_drop();
  lime_render_area(&tdata->area, tdata->config->sink, tdata->t_id);
}

static void _process_tile_bg(void *data, Ecore_Thread *th)
{
  _Img_Thread_Data *tdata = data;
    
  eina_sched_prio_drop();
  eina_sched_prio_drop();
  lime_render_area(&tdata->area, tdata->config->sink, tdata->t_id);
}

void _insert_image(_Img_Thread_Data *tdata)
{
  tdata->img = evas_object_image_filled_add(evas_object_evas_get(win));
  evas_object_image_colorspace_set(tdata->img, EVAS_COLORSPACE_ARGB8888);
  evas_object_image_alpha_set(tdata->img, EINA_FALSE);
  evas_object_image_size_set(tdata->img, TILE_SIZE, TILE_SIZE);
  evas_object_image_smooth_scale_set(tdata->img, EINA_FALSE); 
  evas_object_image_scale_hint_set(tdata->img, EVAS_IMAGE_SCALE_HINT_DYNAMIC);
  evas_object_image_scale_hint_set(tdata->img, EVAS_IMAGE_SCALE_HINT_DYNAMIC);
  evas_object_image_data_set(tdata->img, tdata->buf);
  //evas_object_image_data_update_add(tdata->img, 0, 0, TILE_SIZE, TILE_SIZE);
  evas_object_show(tdata->img);
  
  elm_grid_pack(grid, tdata->img, tdata->packx, tdata->packy, tdata->packw, tdata->packh);
  
  mat_cache_obj_stack(mat_cache, tdata->img, tdata->scale);
  
  evas_object_clip_set(tdata->img, clipper);
}

Eina_Bool _display_preview(void *data)
{
  int i;

  if (!mat_cache_old)
    return ECORE_CALLBACK_CANCEL;
  
  if (preview_timer) {
    ecore_timer_del(preview_timer);
    preview_timer = NULL;
  }
  
  grid_setsize();  
  
  evas_object_show(clipper);
  mat_cache_del(mat_cache_old);
  mat_cache_old = NULL;
  //grid_setsize();
  for(i=0;i<ea_count(finished_threads);i++) {
    _insert_image(ea_data(finished_threads, i));
  }
  eina_array_free(finished_threads);
  finished_threads = NULL;
      
  printf("final delay for preview: %f\n", bench_delay_get());

  preview_timer = NULL;

    
  return ECORE_CALLBACK_CANCEL;
}

static void _finished_tile_blind(void *data, Ecore_Thread *th);

void run_preload_threads(void)
{
  _Img_Thread_Data *tdata;
  
  while (worker_preload+worker < max_workers && preload_pending()) {
    worker_preload++;
    tdata = preload_get();
    tdata->t_id = lock_free_thread_id();
    tdata->buf = cache_app_alloc(TILE_SIZE*TILE_SIZE*4);
    filter_memsink_buffer_set(tdata->config->sink, tdata->buf, tdata->t_id);
    ecore_thread_run(_process_tile_bg, _finished_tile_blind, NULL, tdata);
  }
}

static void
_finished_tile_blind(void *data, Ecore_Thread *th)
{
  _Img_Thread_Data *tdata = data;
  
  assert(tdata->t_id != -1);
  
  thread_ids[tdata->t_id] = 0;
  tdata->t_id = -1;
  
  worker_preload--;

  cache_app_del(tdata->buf, TILE_SIZE*TILE_SIZE*4);
  free(tdata);
  
  //if (worker || pending_action())
    //FIXME free tdatas
    //FIXME need to flush old preloads!
    //eina_array_flush(preload_list);
  
  run_preload_threads();
}

static void
_finished_tile(void *data, Ecore_Thread *th)
{
  _Img_Thread_Data *tdata = data;
  double delay = bench_delay_get();
  
  assert(tdata->t_id != -1);
  
  thread_ids[tdata->t_id] = 0;
  tdata->t_id = -1;
    
 /*
#ifdef BENCHMARK
  bench_delay_start();
  if (tagfiles_idx(files) >= BENCHMARK_LENGTH)
    workerfinish_schedule(&elm_exit_do, NULL, NULL, EINA_TRUE);
  else
    workerfinish_schedule(&step_image_do, (void*)(intptr_t)1, NULL, EINA_TRUE);
#endif*/
  
  worker--;
  
  if (!pending_action()) {
    if (first_preview) {
      idle_render = ecore_idler_add(idle_run_render, NULL);
    }
    else {
      fill_scroller();
    }
  }
  
  if (mat_cache_old) {
    if ((!pending_action() && delay < (1-quick_preview_only)*high_quality_delay && (worker || first_preview))) {
      //printf("delay for now: %f (%d)\n", delay, tdata->scale);
      eina_array_push(finished_threads, tdata);
      
      if (first_preview) {
	if (!preview_timer) {
	    preview_timer = ecore_timer_add((high_quality_delay - delay)*0.001, &_display_preview, NULL);
	}
      }
      else
	if (!worker)
	  _display_preview(NULL);

      if (worker < max_workers && preload_pending() < PRELOAD_THRESHOLD)
	step_image_preload_next();
      else
	run_preload_threads();
	
      first_preview = 0;

      return;
    }
    else {
      if (!first_preview && !pending_action()) {
        grid_setsize();
        fill_scroller();
      }
      
      _display_preview(NULL);
    }
  }
  else if (!worker)
    printf("final delay: %f\n", bench_delay_get());
  
  _insert_image(tdata);
  
  first_preview = 0;
  
  if (!worker && pending_action()) {
    if (mat_cache_old)
      _display_preview(NULL);
    //this will schedule an idle enterer to only process func after we are finished with rendering
    //workerfinish_schedule(pending_action, pending_data, pending_obj, EINA_TRUE);
    printf("pending!\n");
    pending_exe();
  }
  else if (worker < max_workers && preload_pending() < PRELOAD_THRESHOLD) {
    printf("do preload!\n");
    step_image_preload_next();
  }
  else {
    printf("enough preload - run %d!\n", preload_pending());
    run_preload_threads();
  }
  
#ifdef BENCHMARK
  if (!worker && !idle_render) {
    bench_delay_start();
    if (tagfiles_idx(files) >= BENCHMARK_LENGTH)
      workerfinish_schedule(&elm_exit_do, NULL, NULL, EINA_TRUE);
    else
      workerfinish_schedule(&step_image_do, (void*)(intptr_t)1, NULL, EINA_TRUE);
  }
#endif
  
}

int lock_free_thread_id(void)
{
  int i;
  
  for(i=0;i<max_thread_id;i++)
    if (!thread_ids[i]) {
      thread_ids[i] = 1;
      return i;
    }
    
  abort();
}

int fill_area_blind(int xm, int ym, int wm, int hm, int minscale, Config_Data *config)
{
  int x,y,w,h;
  int i, j;
  uint8_t *buf;
  int scale;
  int scalediv;
  Rect area;
  float actual_scalediv;
  int minx, miny, maxx, maxy;
  int scale_start;
  int actual_scale;
  _Img_Thread_Data *tdata;
  Dim size = *size_recalc2(config->sink);

  elm_scroller_region_get(scroller, &x, &y, &w, &h);
  
  if (!w || !h) {
    return 0;
  }
  
  actual_scalediv = config_actual_scale_get(config);
  
  x += xm;
  y += ym;
  w += wm;
  h += hm;
  
  x *= actual_scalediv;
  y *= actual_scalediv;
  w *= actual_scalediv;
  h *= actual_scalediv;
  
  actual_scale = 0;
  while (actual_scalediv >= 2.0) {
    actual_scalediv *= 0.5;
    actual_scale++;
  }

  minscale = actual_scale + minscale;
  
  if (minscale > size.scaledown_max)
    minscale = size.scaledown_max;
  
  scale_start = minscale + max_fast_scaledown;
  
  if (scale_start > size.scaledown_max)
    scale_start = size.scaledown_max;
  
  for(scale=scale_start;scale>=minscale;scale--) {
    //additional scaledown for preview
    scalediv = ((uint32_t)1) << scale;
    for(j=y/TILE_SIZE/scalediv;j<(y+h+TILE_SIZE*scalediv-1)/TILE_SIZE/scalediv;j++)
      for(i=x/TILE_SIZE/scalediv;i<(x+w+TILE_SIZE*scalediv-1)/TILE_SIZE/scalediv;i++) {

	if (i*TILE_SIZE*scalediv >= size.width || j*TILE_SIZE*scalediv >= size.height) {
	  assert(j<=100 && i >= 0);
	  continue;
	  }
		  
	  minx = i*TILE_SIZE*scalediv;
	  miny = j*TILE_SIZE*scalediv;
	  maxx = minx + TILE_SIZE*scalediv;
	  maxy = miny + TILE_SIZE*scalediv;
	  
	  if (minx < 0) {
	    minx = 0;
	  }
	  if (miny < 0) {
	    miny = 0;
	  }
	  if (maxx > size.width) {
	    maxx = size.width;
	  }
	  if (maxy > size.height) {
	    maxy = size.height;
	  }
	  
	  area.corner.scale = scale;
	  area.corner.x = i*TILE_SIZE;
	  area.corner.y = j*TILE_SIZE;  
	  area.width = TILE_SIZE;
	  area.height = TILE_SIZE;
	  
	  tdata = calloc(sizeof(_Img_Thread_Data), 1);
	 
	  tdata->scale = scale;
	  tdata->area = area;
	  tdata->config = config;
	  
	  //printf("process %d %dx%d %dx%d\n", area.corner.scale, area.corner.x, area.corner.y, area.width, area.height);
	  preload_add(tdata);
      }
  }
  
  run_preload_threads();
}

int fill_area(int xm, int ym, int wm, int hm, int minscale, int preview)
{
  int x,y,w,h;
  int i, j;
  Evas_Object *cell;
  uint8_t *buf;
  int scale;
  int scalediv;
  Rect area;
  float actual_scalediv;
  int minx, miny, maxx, maxy;
  int scale_start;
  int actual_scale;
  int started = 0;
  
  _Img_Thread_Data *tdata;
  
  if (forbid_fill)
    return 0;
  
  if (first_preview && worker)
    return 0;
  
  if (worker >= max_workers)
    return 0;
  
  /*if (pending_action())
    return 0;*/
  
  if (!grid)
    return 0;
  
  elm_scroller_region_get(scroller, &x, &y, &w, &h);
  
  if (!w || !h) {
    printf("FIXME avoid fill_scroller_preview: scroller does not yet have region!");
    return 0;
  }
  
  actual_scalediv = actual_scale_get();
  
  x += xm;
  y += ym;
  w += wm;
  h += hm;
  
  x *= actual_scalediv;
  y *= actual_scalediv;
  w *= actual_scalediv;
  h *= actual_scalediv;
  
  actual_scale = 0;
  while (actual_scalediv >= 2.0) {
    actual_scalediv *= 0.5;
    actual_scale++;
  }

  minscale = actual_scale + minscale;
  
  if (minscale > size.scaledown_max)
    minscale = size.scaledown_max;
  
  scale_start = minscale + max_fast_scaledown;
  
  if (scale_start > size.scaledown_max)
    scale_start = size.scaledown_max;
  
  for(scale=scale_start;scale>=minscale;scale--) {
    //additional scaledown for preview
    scalediv = ((uint32_t)1) << scale;
    for(j=y/TILE_SIZE/scalediv;j<(y+h+TILE_SIZE*scalediv-1)/TILE_SIZE/scalediv;j++)
      for(i=x/TILE_SIZE/scalediv;i<(x+w+TILE_SIZE*scalediv-1)/TILE_SIZE/scalediv;i++) {

        cell = mat_cache_get(mat_cache, scale, i, j);
	
	if (i*TILE_SIZE*scalediv >= size.width || j*TILE_SIZE*scalediv >= size.height) {
	  assert(j<=100 && i >= 0);
	  continue;
	  }
		
	if (!cell) {	  
	  minx = i*TILE_SIZE*scalediv;
	  miny = j*TILE_SIZE*scalediv;
	  maxx = minx + TILE_SIZE*scalediv;
	  maxy = miny + TILE_SIZE*scalediv;
	  
	  if (minx < 0) {
	    minx = 0;
	  }
	  if (miny < 0) {
	    miny = 0;
	  }
	  if (maxx > size.width) {
	    maxx = size.width;
	  }
	  if (maxy > size.height) {
	    maxy = size.height;
	  }
	  
	  //FIXME does not work, clipping is at the moment done uncoditionally in _image_insert
	  /*if (crop)
	    evas_object_clip_set(img, clipper);*/
	  
	  area.corner.scale = scale;
	  area.corner.x = i*TILE_SIZE;
	  area.corner.y = j*TILE_SIZE;  
	  area.width = TILE_SIZE;
	  area.height = TILE_SIZE;
	  
	  tdata = calloc(sizeof(_Img_Thread_Data), 1);
	 
  	  //buf = evas_object_image_data_get(img, EINA_TRUE);
	  buf = cache_app_alloc(TILE_SIZE*TILE_SIZE*4);
	  
	  //elm_grid_pack(grid, img, i*TILE_SIZE*scalediv, j*TILE_SIZE*scalediv, TILE_SIZE*scalediv, TILE_SIZE*scalediv);
	  
	  tdata->buf = buf;
	  //tdata->img = img;
	  tdata->scale = scale;
	  tdata->area = area;
	  tdata->packx = i*TILE_SIZE*scalediv;
	  tdata->packy = j*TILE_SIZE*scalediv;
	  tdata->packw = TILE_SIZE*scalediv;
	  tdata->packh = TILE_SIZE*scalediv;
	  tdata->config = config_curr;
	  
	  tdata->t_id = lock_free_thread_id();
	  
	  assert(buf);
	  filter_memsink_buffer_set(config_curr->sink, tdata->buf, tdata->t_id);
	  
	  mat_cache_set(mat_cache, scale, i, j, tdata);
	  	  
	  worker++;
	  
	  ecore_thread_run(_process_tile, _finished_tile, NULL, tdata);
	  started++;

	  if (worker >= max_workers || (first_preview && started))
	    return started;
	}
      }
  }
  
  return started;
}


void fc_del_gui(Eina_List *filter_chain)
{
  Eina_List *list_iter;
  Filter_Chain *fc;
  
  EINA_LIST_FOREACH(filter_chain, list_iter, fc) {
    //FIXME actually remove filters!
    if (fc->item) {
      elm_object_item_del(fc->item);
    }
  }
}

static void fill_scroller(void)
{
  int x, y, w, h, grid_w, grid_h;
  float scale;
  
  if (!config_curr)
    return;
  
  if (!grid)
    return;

  evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
  elm_scroller_region_get(scroller, &x, &y, &w, &h);
  
  if (!w || !h) {
    return;
  }

  if (grid_w && grid_h) {
  scale = size.width / grid_w;	
  if ((float)size.height / grid_h > scale)
    scale = (float)size.height / grid_h;
  }
  else
    scale = INFINITY;
  sprintf(pos_lbl_buf, "%dx%d - %dx%d @ %.2f\n", x, y, w, h, scale);
  elm_object_text_set(pos_label, pos_lbl_buf);
  
  if (!w)
    w++;
  if (!h)
    h++;

  if (fill_area(0,0,0,0, max_fast_scaledown, 0))
    return;
    
  if (fill_area(0,0,0,0,0, 0))
    return;
}

static void fill_scroller_blind(Config_Data *config)
{
  int x, y, w, h, grid_w, grid_h;
  float scale;
  Dim *size;
  
  if (!grid)
    return;

  evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
  elm_scroller_region_get(scroller, &x, &y, &w, &h);
  size = size_recalc2(config->sink);
  
  if (!w || !h) {
    return;
  }

  if (grid_w && grid_h) {
  scale = size->width / grid_w;	
  if ((float)size->height / grid_h > scale)
    scale = (float)size->height / grid_h;
  }
  else
    scale = INFINITY;
  
  if (!w)
    w++;
  if (!h)
    h++;

  //FIXME 
  fill_area_blind(0,0,0,0, 0, config);
}

static void
on_scroller_move(void *data, Evas_Object *obj, void *event_info)
{
  if (forbid_fill)
    return;
  
  fill_scroller();
}

static void
on_done(void *data, Evas_Object *obj, void *event_info)
{
  // quit the mainloop (elm_run function will return)
  workerfinish_schedule(&elm_exit_do, NULL, NULL, EINA_TRUE);
}


void group_select_do(void *data, Evas_Object *obj)
{
  int group_idx;
  int failed;
  const char *filename;
  Elm_Object_Item *it;
  
  delgrid();
    
  File_Group *group = tagfiles_get(files);
  
  group_idx = *(int*)data;
    
  failed = 1;
  
  while(failed) {
    if (group_idx == filegroup_count(group)) {
      group_idx = 0;
    }
    
    filename = filegroup_nth(group, group_idx);
    if (!filename) {
      group_idx++;
      continue;
    }
      
    lime_setting_string_set(config_curr->load, "filename", filename);
    
    failed = lime_config_test(config_curr->sink);
    if (failed) {
      group_idx++;
      printf("could not configure: %s\n", filename);
    }
  }
  
  it = elm_list_first_item_get(group_list);
  while (group_idx) {
    it = elm_list_item_next(it);
    group_idx--;
  }
  elm_list_item_selected_set(it, EINA_TRUE);
  
  size_recalc();

  fill_scroller();
}

/*
void tags_select_do(void *data, Evas_Object *obj)
{
  int failed;
  const char *filename;
  
  delgrid();
    
  File_Group *group = eina_inarray_nth(files, file_idx);
  
  group_idx = *(int*)data;
    
  failed = 1;
  
  while(failed) {
    if (group_idx == eina_inarray_count(group->files)) {
      group_idx = 0;
    }
    
    filename = ((Tagged_File*)eina_inarray_nth(group->files, group_idx))->filename;
    if (!filename) {
      group_idx++;
      continue;
    }
      
    lime_setting_string_set(load, "filename", filename);
    
    failed = lime_config_test(sink);
    if (failed)
      group_idx++;
  }

  size_recalc();

  fill_scroller();
}*/

void delgrid(void)
{  
  if (mat_cache_old) {
    //we have not yet shown the current image (which would delete mat_cache_old)
    printf("FIXME have old matcache!\n");
    mat_cache_old = NULL;
  }
  
  //mat_cache_flush(mat_cache);
  mat_cache_old = mat_cache;
  mat_cache = mat_cache_new();
  
  finished_threads = eina_array_new(16);
}

static void on_jump_image(void *data, Evas_Object *obj, void *event_info)
{
  if (!files)
    return;
  
  int idx = elm_slider_value_get(file_slider);

  if (idx == tagfiles_idx(files))
    return;
  
  bench_delay_start();
  tagfiles_idx_set(files, idx);
  
  if (mat_cache_old && !worker)
    _display_preview(NULL);
  //FIXME detect if same actions come behind each other and skip?
  workerfinish_schedule(&step_image_do, NULL, NULL, EINA_FALSE);
}


static void
on_group_select(void *data, Evas_Object *obj, void *event_info)
{ 
  //FIXME no double select!
  if (!fixme_no_group_select)
    workerfinish_schedule(&group_select_do, data, obj, EINA_TRUE);
}

typedef struct {
  const char *tag;
  File_Group *group;
}
Tags_List_Item_Data;

Eina_Bool tags_hash_filter_func(const Eina_Hash *hash, const void *key, void *data, void *fdata)
{
  elm_genlist_item_append(tags_filter_list, tags_filter_itc, data, NULL, ELM_GENLIST_ITEM_NONE, NULL, NULL);
  
  return 1;
}

typedef struct {
  File_Group *group;
  int valid;
} Filter_Check_Data;

Eina_Bool tag_filter_check_and_hash_func(const Eina_Hash *hash, const void *key, void *data, void *fdata)
{
  abort();
  /*Filter_Check_Data *check = fdata;
  
  if (!eina_hash_find(check->group->tags, data))
    check->valid = 0;
  
  return 1;*/
}

Eina_Bool tag_filter_check_or_hash_func(const Eina_Hash *hash, const void *key, void *data, void *fdata)
{
  Filter_Check_Data *check = fdata;
  
  if (eina_hash_find(filegroup_tags(check->group), data))
    check->valid = 1;
  
  return 1;
}

int group_in_filters(File_Group *group, Eina_Hash *filters)
{
  Filter_Check_Data check;
  
  if (!filegroup_tags_valid(group))
    if ((filters && eina_hash_population(filters)) || tags_filter_rating)
      return 0;
    else
      return 1;
  
  if (tags_filter_rating && filegroup_rating(group) < tags_filter_rating)
    return 0;
  
  //for or hash!
  if (!filters || !eina_hash_population(filters))
    check.valid = 1;
  else
    check.valid = 0;
  check.group = group;
  
  if (filters && eina_hash_population(filters))
    eina_hash_foreach(filters, tag_filter_check_or_hash_func, &check);
  
  return check.valid;
}

void on_known_tags_changed(Tagfiles *tagfiles, void *data, const char *new_tag)
{  
  Tags_List_Item_Data *tag;
  
  if (cur_group && filegroup_tags_valid(cur_group)) {
    if (taglist_add) {
      while (ea_count(taglist_add)) {
	tag = malloc(sizeof(Tags_List_Item_Data));
	tag->tag = eina_array_pop(taglist_add);
	tag->group = NULL;
	elm_genlist_item_append(tags_list, tags_list_itc, tag, NULL, ELM_GENLIST_ITEM_NONE, NULL, NULL);
      }
      eina_array_free(taglist_add);
      taglist_add = NULL;
    }
    
    tag = malloc(sizeof(Tags_List_Item_Data));
    tag->tag = new_tag;
    tag->group = cur_group;
    elm_genlist_item_append(tags_list, tags_list_itc, tag, NULL, ELM_GENLIST_ITEM_NONE, NULL, NULL);
  }
  else {
    if (!taglist_add) taglist_add = eina_array_new(32);
    eina_array_push(taglist_add, new_tag);
  }
  elm_genlist_item_append(tags_filter_list, tags_filter_itc, new_tag, NULL, ELM_GENLIST_ITEM_NONE, NULL, NULL);
}

void filegroup_changed_cb(File_Group *group)
{
  char unit_fmt[32];
  
  if (tags_filter_rating || (tags_filter && eina_hash_population(tags_filter)))
    if (group_in_filters(group, tags_filter)) {  
      filtered_image_count = filtered_image_count_get();
      sprintf(unit_fmt, "%%.0f/%d", filtered_image_count);
      elm_slider_unit_format_set(file_slider, unit_fmt); 
    }
  
  //FIXME do we have to schedule this even if something else is running?
  workerfinish_schedule(step_image_do, NULL, NULL, EINA_TRUE);
}

Config_Data *config_data_get(File_Group *group, int nth)
{
  char *filename;
  Config_Data *config;
  
  filename = filegroup_nth(group, nth);
  //FIXME better file ending list (no static file endings!)
  if (!filename || (!eina_str_has_extension(filename, ".jpg") 
   && !eina_str_has_extension(filename, ".JPG")
    && !eina_str_has_extension(filename, ".tif")
    && !eina_str_has_extension(filename, ".TIF")
    && !eina_str_has_extension(filename, ".tiff")
    && !eina_str_has_extension(filename, ".TIFF"))) {
    return NULL;
}

  config = filegroup_data_get(group, nth);
  if (config && !config->running) {
    printf("using cached config!\n");
    return config;
  }
  else if (config)
    printf("discarding running config!\n");
  //FIXME handle running config!
  
  config = calloc(sizeof(Config_Data), 1);
  if (filegroup_tags_valid(group) && filegroup_filterchain(group)) {
    config->filters = lime_filter_chain_deserialize(filegroup_filterchain(group));
    
    //FIXME select group according to load file 
    config->load = eina_list_data_get(config->filters);
    if (strcmp(config->load->fc->shortname, "load")) {
      config->load = lime_filter_new("load");
      config->filters = eina_list_prepend(config->filters, config->load);
    }
    config->sink = eina_list_data_get(eina_list_last(config->filters));
    if (strcmp(config->sink->fc->shortname, "sink")) {
      config->sink = lime_filter_new("memsink");
      lime_setting_int_set(config->sink, "add alpha", 1);
      config->filters = eina_list_append(config->filters, config->sink);
    }
    fc_connect_from_list(config->filters);
  }
  else {
    
    config->load = lime_filter_new("load");
    config->filters = eina_list_append(NULL, config->load);
    config->sink = lime_filter_new("memsink");
    lime_setting_int_set(config->sink, "add alpha", 1);
    config->filters = eina_list_append(config->filters, config->sink);
    
    fc_connect_from_list(config->filters);
  } 
  
  //strcpy(image_file, filename);
  filegroup_data_attach(group, nth, config);
  
  lime_setting_string_set(config->load, "filename", filename);
  
  config->failed = lime_config_test(config->sink);
  
  return config;
}

void config_exe(void *data, Ecore_Thread *thread)
{
  Config_Data *config = data;
  
  config->failed = lime_config_test(config->sink);
}

void config_finish(void *data, Ecore_Thread *thread)
{
  Config_Data *config = data;
  _Img_Thread_Data *tdata = calloc(sizeof(_Img_Thread_Data), 1);
  uint8_t *buf;
  
  worker_config--;
  config->running = NULL;
  //FIXME free stuff on failed!
  
  tdata->scale =  size_recalc2(config->sink)->scaledown_max;
  tdata->area.corner.x = 0;
  tdata->area.corner.y = 0;
  tdata->area.corner.scale = size_recalc2(config->sink)->scaledown_max;
  tdata->area.width = TILE_SIZE;
  tdata->area.height =  TILE_SIZE;
  tdata->config = config;
  
  preload_add(tdata);
  
  printf("run low scale prload?\n");
  run_preload_threads();
}

void config_thread_start(File_Group *group, int nth)
{
  char *filename;
  Config_Data *config;
  
  config = filegroup_data_get(group, nth);
  if (config)
    return;
  
  filename = filegroup_nth(group, nth);
  //FIXME better file ending list (no static file endings!)
  if (!filename || (!eina_str_has_extension(filename, ".jpg") 
   && !eina_str_has_extension(filename, ".JPG")
    && !eina_str_has_extension(filename, ".tif")
    && !eina_str_has_extension(filename, ".TIF")
    && !eina_str_has_extension(filename, ".tiff")
    && !eina_str_has_extension(filename, ".TIFF"))) {
    return;
}
  
  config = calloc(sizeof(Config_Data), 1);
  if (filegroup_tags_valid(group) && filegroup_filterchain(group)) {
    config->filters = lime_filter_chain_deserialize(filegroup_filterchain(group));
    
    //FIXME select group according to load file 
    config->load = eina_list_data_get(config->filters);
    if (strcmp(config->load->fc->shortname, "load")) {
      config->load = lime_filter_new("load");
      config->filters = eina_list_prepend(config->filters, config->load);
    }
    config->sink = eina_list_data_get(eina_list_last(config->filters));
    if (strcmp(config->sink->fc->shortname, "sink")) {
      config->sink = lime_filter_new("memsink");
      lime_setting_int_set(config->sink, "add alpha", 1);
      config->filters = eina_list_append(config->filters, config->sink);
    }
    fc_connect_from_list(config->filters);
  }
  else {
    
    config->load = lime_filter_new("load");
    config->filters = eina_list_append(NULL, config->load);
    config->sink = lime_filter_new("memsink");
    lime_setting_int_set(config->sink, "add alpha", 1);
    config->filters = eina_list_append(config->filters, config->sink);
    
    fc_connect_from_list(config->filters);
  } 
  
  //strcpy(image_file, filename);
  lime_setting_string_set(config->load, "filename", filename);
  
  filegroup_data_attach(group, nth, config);
  
  worker_config++;
  
  config->running = ecore_thread_run(&config_exe, &config_finish, NULL, config);
}

void step_image_start_configs(int n)
{
  int i;
  int group_idx;
  int idx;
  File_Group *group;
  Config_Data *config;
  int step;

  assert(files);
  
  if (!tagfiles_count(files))
    return;
  
  if (!last_file_step)
    step = 1;
  else
    step = last_file_step;
  
  idx = tagfiles_idx(files);
  
  for (i=0;i<n;i++) {
    idx += step;
    
    group = tagfiles_nth(files, idx);
    
    if (group_in_filters(group, tags_filter)) {
      for(group_idx=0;group_idx<filegroup_count(group);group_idx++)
	config_thread_start(group, group_idx);
    }
  }
}

//FIXME 
void step_image_preload_next(void)
{
  int i;
  int group_idx;
  int idx;
  File_Group *group;
  Config_Data *config;
  int step;

  assert(files);
  
  if (!tagfiles_count(files))
    return;
  
  if (!last_file_step)
    step = 1;
  else
    step = last_file_step;
  
  idx = (tagfiles_idx(files)+tagfiles_count(files)+step) % tagfiles_count(files);
  
  while (idx != tagfiles_idx(files)) {
    group = tagfiles_nth(files, idx);
    if (group_in_filters(group, tags_filter)) {
      for(group_idx=0;group_idx<filegroup_count(group);group_idx++) {
	config = config_data_get(group, group_idx);
	if (!config->failed) {
	  fill_scroller_blind(config);
	  printf("preload %s\n", filegroup_nth(group, group_idx));
	  return;
	}
      }
    }
    idx += step;
  }
}


void step_image_do(void *data, Evas_Object *obj)
{
  int i;
  int start_idx;
  int group_idx;
  int *idx_cp;
  int failed;
  File_Group *group;
  const char *filename;
  Elm_Object_Item *item;
  Config_Data *config = NULL;
  
  printf("non-chancellation delay: %f\n", bench_delay_get());
  
  assert(!worker);
  
  assert(files);
  
  if (!tagfiles_count(files))
    return;
  
  //FIXME free stuff!
  preload_flush();
  
  tagfiles_step(files, (intptr_t)data);
  last_file_step = (intptr_t)data;
  
  del_filter_settings();  
  
  forbid_fill++;
  
  start_idx = tagfiles_idx(files);
  
  group = tagfiles_get(files);
  
  while (!config || config->failed) {
    group_idx = 0;
    
    if (group_in_filters(group, tags_filter)) {
      while(!config || config->failed) {
	if (group_idx == filegroup_count(group))
	  break;
	
	  config = config_data_get(group, group_idx);
	  
	  if (!config || config->failed) {
	    //FIXME del filters
	    //free(config);
	    //config = NULL;
	    printf("failed to find valid configuration for %s\n", filegroup_nth(group, group_idx));
	    group_idx++;
	  }
      }
    }
    //else
    //  failed = 1;
    
    if (config && !config->failed)
      break;
    
    tagfiles_step(files, last_file_step);
    group = tagfiles_get(files);
    
    if (start_idx == tagfiles_idx(files)){
      printf("no valid configuration found for any file!\n");
      forbid_fill--;
      return;
    }
  }
  
  if (config_curr) {
    printf("del filterchain!\n");
    fc_del_gui(config_curr->filter_chain);
    config_curr->filter_chain = NULL;
    //config_curr->filters = NULL;
    //config_curr = NULL;
  }
  config_curr = config;
  fc_gui_from_list(config_curr->filters);
  //FIXME free filter list
  config_curr->filters = NULL;
  //FIXME get all filter handling from actual filter chain
  filegroup_data_attach(group, group_idx, NULL);
  
  cur_group = group;
  delgrid();
  
  tagfiles_group_changed_cb_flush(files);
  tagfiles_group_changed_cb_insert(files, group, filegroup_changed_cb);
    
  //we start as early as possible with rendering!
  forbid_fill--;
  size_recalc();
  if (quick_preview_only)
    first_preview = 1;
  printf("configuration delay: %f\n", bench_delay_get()); 
  fill_scroller();
  
  if (tab_current == tab_group)
    refresh_group_tab();
  
  //update tag list

  if (filegroup_tags_valid(cur_group)) {
    elm_genlist_realized_items_update(tags_list);
  
    //update tag rating
    elm_segment_control_item_selected_set(elm_segment_control_item_get(seg_rating, filegroup_rating(group)), EINA_TRUE);
  } 
  
  elm_slider_value_set(file_slider, tagfiles_idx(files)+0.1);
  
  step_image_start_configs(PRECALC_CONFIG_RANGE);
}

void del_file_done(void *data, Eio_File *handler)
{
  printf("moved %s to delete\n", (char*)data);
}

void del_file_error(void *data, Eio_File *handler, int error)
{
  printf("del failed with %s on %s!\n", strerror(error), (char*)data);
}

/*void file_group_del(File_Group *group)
{
  //FIXME
}*/

void delete_image_do(void *data, Evas_Object *obj)
{
  int i;
  Tagged_File *file;
  File_Group *group;

  group = tagfiles_get(files);
  
  filegroup_move_trash(group);
  
  tagfiles_del_curgroup(files);
  
  step_image_do(NULL, NULL);
}


typedef struct {
  int w, h;
  int new_w, new_h;
  int co_x, co_y;
  int cn_x, cn_y;
  int c_w, c_h;
} _Grid_Zoom;

Elm_Transit_Effect *_trans_grid_zoom_contex_new(int w, int h, int new_w, int new_h, int co_x, int co_y, int cn_x, int cn_y, int c_w, int c_h)
{
  _Grid_Zoom *zoom;
  
  zoom = malloc(sizeof(_Grid_Zoom));
  if (!zoom) return NULL;
  
  zoom->w = w;
  zoom->h = h;
  zoom->new_w = new_w;
  zoom->new_h = new_h;
  
  zoom->co_x = co_x;
  zoom->co_y = co_y;
  zoom->cn_x = cn_x;
  zoom->cn_y = cn_y;
  zoom->c_w = c_w;
  zoom->c_h = c_h;
  
  return zoom;
}

void _trans_grid_zoom_contex_del(void *data, Elm_Transit *transit)
{
  free(data);
  
  if (forbid_fill)
    return;
  
  grid_setsize();
  fill_scroller();
}

void _trans_grid_zoom_trans_cb(Elm_Transit_Effect *effect, Elm_Transit *transit, double progress)
{
  _Grid_Zoom *zoom = effect;
  
  double inv = 1.0 - progress;
  
  //elm_grid_size_set(grid, zoom->w*(1.0-progress)+zoom->new_w*(progress), zoom->h*(1.0-progress)+zoom->new_h*progress);
  evas_object_size_hint_min_set(grid,  zoom->w*(1.0-progress)+zoom->new_w*(progress), zoom->h*(1.0-progress)+zoom->new_h*progress);
  
  elm_box_recalculate(gridbox);
  
  elm_scroller_region_show(scroller, 
			   zoom->co_x*inv+zoom->cn_x*progress-zoom->c_w/2,
			   zoom->co_y*inv+zoom->cn_y*progress-zoom->c_h/2,
			   zoom->c_w,
			   zoom->c_h
  );
  
  if (!forbid_fill)
    fill_scroller();
  
}

typedef struct {
  const char *filename;
  const char *filterchain;
  Export_Data *export;
} Export_Job;

void start_single_export(Export_Data *export, Export_Job *job)
{
  char dst[2048];
  const char *filename;
  
  
  if (!job) {
    if (!export->list || !ea_count(export->list)) {
      export->list = NULL;
      return;
    }
    job = eina_array_pop(export->list);
  }
  
  filename = ecore_file_file_get(job->filename);
  
  if (job->filterchain)
    sprintf(dst, "limedo \'%s,savejpeg:filename=%s/%s\' \'%s\'", job->filterchain, string_escape_colon(job->export->path), string_escape_colon(filename), job->filename);
  else
    sprintf(dst, "limedo \'savejpeg:filename=%s/%s\' \'%s\'", string_escape_colon(job->export->path), string_escape_colon(filename), job->filename);

  printf("start export of: %s\n", dst);
  ecore_exe_run(dst, job);
}


static Eina_Bool _rsync_term(void *data, int type, void *event)
{
//  int i;
  Ecore_Exe_Event_Del *del_event = event;
  Export_Job *job = ecore_exe_data_get(del_event->exe);
  
  if (!job) {
    return ECORE_CALLBACK_PASS_ON;
  }
  
  printf("file: %s return code: %d\n", job->filename, del_event->exit_code);

  if (del_event->exit_code)
    start_single_export(job->export, job);
  else
    start_single_export(job->export, NULL);
  
  if (job->export->list) {
    elm_progressbar_value_set(job->export->eo_progress, 1.0 - (double)ea_count(job->export->list)/job->export->count);
    printf("around %d remaining\n", ea_count(job->export->list));
  }
  else {
    elm_progressbar_value_set(job->export->eo_progress, 100.0);
    printf("export_finished\n");
  }
  
  free(job);
  
  return ECORE_CALLBACK_PASS_ON;
}

static void
on_exe_images_rsync(void *data, Evas_Object *obj, void *event_info)
{
  int i, j;
  File_Group *group;
  const char *filename;
  char dst[2048];
  Eina_Array *dirs;
  Export_Data *export = calloc(sizeof(Export_Data), 1);
  Export_Job *job;
  
  //FIXME check if we finished scanning?
  
  export->extensions = elm_entry_entry_get(export_extensions);
  if (export->extensions && !strlen(export->extensions)) {
    export->extensions = NULL;
  }
  export->path = strdup(elm_fileselector_entry_path_get(export_path));
  
  //progress bar
  export->eo_progress = elm_progressbar_add(export_box);
  evas_object_size_hint_weight_set(export->eo_progress, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(export->eo_progress, EVAS_HINT_FILL, 0);
  evas_object_show(export->eo_progress);
  elm_box_pack_end(export_box, export->eo_progress);
  
  elm_progressbar_value_set(export->eo_progress, 0.0);
  
  for(i=0;i<tagfiles_count(files);i++) {
    group = tagfiles_nth(files, i);
    if (group_in_filters(group, tags_filter)) {
      for(j=0;j<filegroup_count(group);j++) {
	filename = filegroup_nth(group, j);
	if (filename && (!export->extensions || (strstr(filename, export->extensions) && strlen(strstr(filename, export->extensions)) == strlen (export->extensions) ))) {
	  if (!export->list)
	    export->list = eina_array_new(32);
	  job = malloc(sizeof(Export_Job));
	  job->filename = filename;
	  job->filterchain = filegroup_filterchain(group);
	  job->export = export;
	  eina_array_push(export->list, job);
	}
      }
    }
  }

  if (export->path[strlen(export->path)-1] == '/')
    export->path[strlen(export->path)-1] = '\0';
  
  if (export->list)
    export->count = ea_count(export->list);
  
  for(i=0;i<ecore_thread_max_get();i++)
    start_single_export(export, NULL);
}

static void
on_fit_image(void *data, Evas_Object *obj, void *event_info)
{
  Elm_Transit *trans;
  int x,y,w,h;
  int grid_w, grid_h;
  float new_scaledown;
  
  if (fit != 1) {
    elm_scroller_region_get(scroller, &x, &y, &w, &h);
    
    evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
    
    fit = 1;

    if (!w || !h) {
      scale_goal = 1.0;
    }
    else {
      scale_goal = (float)size.width / w;	
      if ((float)size.height / h > scale_goal)
	scale_goal = (float)size.height / h;
    }
    
    new_scaledown = 1.0/actual_scale_get()*scale_goal;
    
    trans = elm_transit_add();
    elm_transit_object_add(trans, grid);
    elm_transit_effect_add(trans, &_trans_grid_zoom_trans_cb, _trans_grid_zoom_contex_new(grid_w, grid_h, size.width/scale_goal,size.height/scale_goal,x+w/2,y+h/2,x/new_scaledown+w/(new_scaledown*2),y/new_scaledown+h/(new_scaledown*2),w,h), &_trans_grid_zoom_contex_del);
    elm_transit_duration_set(trans, 0.5);
    elm_transit_tween_mode_set(trans, ELM_TRANSIT_TWEEN_MODE_SINUSOIDAL);
    elm_transit_repeat_times_set(trans, 0);
    elm_transit_go(trans);
  }
}

static void
on_fullscreen(void *data, Evas_Object *obj, void *event_info)
{
  if (!fullscreen) {
    elm_bg_color_set(bg, 0, 0, 0);
    elm_win_fullscreen_set(win, EINA_TRUE);
    elm_box_unpack(main_vbox, vbox_bottom);
    evas_object_hide(vbox_bottom);
  }
  else {
    printf("FIXME: HELP! How do I reset elm_bg to default background?");
    elm_bg_color_set(bg, 0, 0, 0);
    elm_win_fullscreen_set(win, EINA_FALSE);
    elm_box_pack_end(main_vbox, vbox_bottom);
    evas_object_show(vbox_bottom);
  }
  
  fullscreen = 1-fullscreen;
}

static void
on_origscale_image(void *data, Evas_Object *obj, void *event_info)
{
  Elm_Transit *trans;
  int x,y,w,h;
  int grid_w, grid_h;
  float new_scaledown;
  
  if (scale_goal != 1.0) {
    elm_scroller_region_get(scroller, &x, &y, &w, &h);
    
    evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
    
    fit = 0;
    scale_goal = 1.0;
    new_scaledown = 1.0/actual_scale_get()*scale_goal;
    
    trans = elm_transit_add();
    elm_transit_object_add(trans, grid);
    elm_transit_effect_add(trans, &_trans_grid_zoom_trans_cb, _trans_grid_zoom_contex_new(grid_w, grid_h, size.width/scale_goal,size.height/scale_goal,x+w/2,y+h/2,x/new_scaledown+w/(new_scaledown*2),y/new_scaledown+h/(new_scaledown*2),w,h), &_trans_grid_zoom_contex_del);
    elm_transit_duration_set(trans, 0.5);
    elm_transit_tween_mode_set(trans, ELM_TRANSIT_TWEEN_MODE_SINUSOIDAL);
    elm_transit_repeat_times_set(trans, 0);
    elm_transit_go(trans);
  }
}


static void
on_next_image(void *data, Evas_Object *obj, void *event_info)
{
  //already waiting for an action?
  if (pending_action() <= PENDING_ACTIONS_BEFORE_SKIP_STEP && files) {
    
    bench_delay_start();
    //tagfiles_step(files, last_file_step);

    if (mat_cache_old && !worker)
      _display_preview(NULL);
    //doesnt matter if true or false
    workerfinish_schedule(&step_image_do, (void*)(intptr_t)1, NULL, EINA_TRUE);
  }
}

static void
on_delete_image(void *data, Evas_Object *obj, void *event_info)
{
  workerfinish_schedule(&delete_image_do, NULL, NULL, EINA_TRUE);
}

static void
on_prev_image(void *data, Evas_Object *obj, void *event_info)
{//already waiting for an action?
  if (pending_action() <= PENDING_ACTIONS_BEFORE_SKIP_STEP && files) {
    
    bench_delay_start();
    
    if (mat_cache_old && !worker)
      _display_preview(NULL);
    //FIXME doesnt matter if true or false
    workerfinish_schedule(&step_image_do, (void*)(intptr_t)-1, NULL, EINA_TRUE);
  }
}

void zoom_in_do(void)
{
  Elm_Transit *trans;
  int x,y,w,h;
  int grid_w, grid_h;
  float new_scaledown;
  
  if (scale_goal > 0.25) {
    elm_scroller_region_get(scroller, &x, &y, &w, &h);
    
    evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
    
    fit = 0;
    
    scale_goal /= 1.5;
    if (scale_goal < 0.25)
      scale_goal = 0.25;
    
    new_scaledown = 1.0/actual_scale_get()*scale_goal;
    
    trans = elm_transit_add();
    elm_transit_object_add(trans, grid);
    elm_transit_effect_add(trans, &_trans_grid_zoom_trans_cb, _trans_grid_zoom_contex_new(grid_w, grid_h, size.width/scale_goal,size.height/scale_goal,x+w/2,y+h/2,x/new_scaledown+w/(new_scaledown*2),y/new_scaledown+h/(new_scaledown*2),w,h), &_trans_grid_zoom_contex_del);
    elm_transit_duration_set(trans, 0.5);
    elm_transit_tween_mode_set(trans, ELM_TRANSIT_TWEEN_MODE_SINUSOIDAL);
    elm_transit_repeat_times_set(trans, 0);
    elm_transit_go(trans);
  }
}


static void
on_zoom_in(void *data, Evas_Object *obj, void *event_info)
{
  zoom_in_do();
}


Eina_Bool _idle_progress_printer(void *data)
{
  Tagfiles *tagfiles = data;
  char buf[64];
  
  sprintf(buf, "scanned %d files in %d dirs", tagfiles_scanned_files(tagfiles), tagfiles_scanned_dirs(tagfiles));
  
  elm_object_text_set(load_label, buf);
  
  if (files_new) {
    if (files)
      tagfiles_del(files);
    files = files_new;
    files_new = NULL;
  
    elm_slider_min_max_set(file_slider, 0, tagfiles_count(files)-1);
    evas_object_smart_callback_add(file_slider, "changed", &on_jump_image, NULL);
    elm_slider_value_set(file_slider, 0);
    evas_object_size_hint_weight_set(file_slider, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(file_slider, EVAS_HINT_FILL, 0);
    elm_slider_unit_format_set(file_slider, "%.0f");
    evas_object_show(file_slider);
    
    if (!gridbox) {
      gridbox = elm_box_add(win);
      elm_object_content_set(scroller, gridbox);
      evas_object_size_hint_weight_set(gridbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
      evas_object_size_hint_align_set(gridbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
      evas_object_show(gridbox);
      
      grid = elm_grid_add(win);
      clipper = evas_object_rectangle_add(evas_object_evas_get(win));
      elm_grid_pack(grid, clipper, 0, 0, size.width, size.height);
      evas_object_size_hint_min_set(grid,  200, 200);
      elm_box_recalculate(gridbox);
      elm_box_pack_start(gridbox, grid);
      evas_object_show(grid);
    }
    
    //grid_setsize();
    
    bench_delay_start();
    
    evas_object_show(scroller);
  }
  else
    elm_slider_min_max_set(file_slider, 0, tagfiles_count(files)-1);
  
  if (!cur_group)
    step_image_do(NULL, NULL);
  
  //FIXME can't we leave this?
  idle_progress_print = NULL;
  return ECORE_CALLBACK_CANCEL;
}

static void _ls_progress_cb(Tagfiles *tagfiles, void *data)
{
  if (!idle_progress_print)
    idle_progress_print = ecore_idler_add(&_idle_progress_printer, tagfiles);
}

static void _ls_done_cb(Tagfiles *tagfiles, void *data)
{
  evas_object_del(load_notify);
  
  if (idle_progress_print) {
    ecore_idler_del(idle_progress_print);
    idle_progress_print = NULL;
  }
  
  if (files_new) {
    if (files)
      tagfiles_del(files);
    files = files_new;
    files_new = NULL;
  
    elm_slider_min_max_set(file_slider, 0, tagfiles_count(files)-1);
    evas_object_smart_callback_add(file_slider, "changed", &on_jump_image, NULL);
    elm_slider_value_set(file_slider, 0);
    evas_object_size_hint_weight_set(file_slider, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(file_slider, EVAS_HINT_FILL, 0);
    elm_slider_unit_format_set(file_slider, "%.0f");
    evas_object_show(file_slider);
    
    if (!gridbox) {
      gridbox = elm_box_add(win);
      elm_object_content_set(scroller, gridbox);
      evas_object_size_hint_weight_set(gridbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
      evas_object_size_hint_align_set(gridbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
      evas_object_show(gridbox);
      
      grid = elm_grid_add(win);
      clipper = evas_object_rectangle_add(evas_object_evas_get(win));
      elm_grid_pack(grid, clipper, 0, 0, size.width, size.height);
      evas_object_size_hint_min_set(grid,  200, 200);
      elm_box_recalculate(gridbox);
      elm_box_pack_start(gridbox, grid);
      evas_object_show(grid);
    }
    
    //elm_genlist_clear(tags_filter_list);
    //eina_hash_foreach(known_tags, tags_hash_filter_func, NULL);
    
    
    //grid_setsize();
    
    bench_delay_start();
    
    evas_object_show(scroller);
  }
  else
    elm_slider_min_max_set(file_slider, 0, tagfiles_count(files)-1);
  
  if (!cur_group)
    step_image_do(NULL, NULL);
}

void on_open_dir(char *path)
{ 
  char *dir = path;

  if (dir) {
    printf("open dir %s\n", path);
    
    if (files_new) {
      tagfiles_del(files_new);
      files_new = NULL;
    }
    
    elm_fileselector_button_path_set(fsb, dir);
    load_notify = elm_notify_add(win);
    load_label = elm_label_add(load_notify);
    elm_object_text_set(load_label, "found 0 files groups");
    elm_object_content_set(load_notify, load_label);
    evas_object_show(load_label);
    evas_object_show(load_notify);
    files_new = tagfiles_new_from_dir(dir, _ls_progress_cb, _ls_done_cb, on_known_tags_changed);
  }
}

static void on_open(void *data, Evas_Object *obj, void *event_info)
{
  on_open_dir((char*)event_info);
}


void zoom_out_do(void)
{
  Elm_Transit *trans;
  int x,y,w,h;
  int grid_w, grid_h;
  float new_scaledown;
  
  elm_scroller_region_get(scroller, &x, &y, &w, &h);
  
  evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
  
  fit = 0;
  
  scale_goal *= 1.5;
  
  new_scaledown = 1.0/actual_scale_get()*scale_goal;
  
  trans = elm_transit_add();
  elm_transit_object_add(trans, grid);
  elm_transit_effect_add(trans, &_trans_grid_zoom_trans_cb, _trans_grid_zoom_contex_new(grid_w, grid_h, size.width/scale_goal,size.height/scale_goal,x+w/2,y+h/2,x/new_scaledown+w/(new_scaledown*2),y/new_scaledown+h/(new_scaledown*2),w,h), &_trans_grid_zoom_contex_del);
  elm_transit_duration_set(trans, 0.5);
  elm_transit_tween_mode_set(trans, ELM_TRANSIT_TWEEN_MODE_SINUSOIDAL);
  elm_transit_repeat_times_set(trans, 0);
  elm_transit_go(trans);
}

static void
on_zoom_out(void *data, Evas_Object *obj, void *event_info)
{
  zoom_out_do();
}

static void on_insert_before(void *data, Evas_Object *obj, void *event_info)
{
  bench_delay_start();
  
  workerfinish_schedule(&insert_before_do, NULL, NULL, EINA_TRUE);
}

static void on_insert_after(void *data, Evas_Object *obj, void *event_info)
{
  bench_delay_start();
  
  workerfinish_schedule(&insert_after_do, NULL, NULL, EINA_TRUE);
}

void refresh_group_tab(void) 
{
  int i, group_idx;
  int *idx_cp;
  Elm_Object_Item *item;
  
  if (!cur_group)
    return;
  
  elm_list_clear(group_list);
  for(i=0;i<filegroup_count(cur_group);i++)
    if (filegroup_nth(cur_group, i)) {
      idx_cp = malloc(sizeof(int));
      *idx_cp = i;
      item = elm_list_item_append(group_list, filegroup_nth(cur_group, i), NULL, NULL, &on_group_select, idx_cp);
      if (group_idx == i) {
	fixme_no_group_select = EINA_TRUE;
	elm_list_item_selected_set(item, EINA_TRUE);
	fixme_no_group_select = EINA_FALSE;
      }
    }
  
  elm_list_go(group_list);
}

static void on_tab_select(void *data, Evas_Object *obj, void *event_info)
{
  void (*refresh)(void);
  evas_object_hide(tab_current);
  elm_box_unpack(tab_box, tab_current);
  
  tab_current = (Evas_Object*)data;
  
  refresh = evas_object_data_get(tab_current, "limeview,main_tab,refresh_cb");
  
  if (refresh)
    refresh();
  
  elm_box_pack_end(tab_box, tab_current);
  evas_object_show(tab_current);
}

void _scroller_resize_cb(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
  if (forbid_fill)
    return;
  
  if (!config_curr)
    return;
    
  grid_setsize();
  fill_scroller();
}

void shortcut(void *data, Evas *e, Evas_Object *obj, void *event_info)
//Eina_Bool shortcut(void *data, Evas_Object *obj, Evas_Object *src, Evas_Callback_Type type, void *event_info)
{
  struct _Evas_Event_Key_Down *key;
  
  //if (type ==  EVAS_CALLBACK_KEY_DOWN) {
    key = event_info;
    if (!strcmp(key->keyname, "space"))
      on_next_image(NULL, NULL, NULL);
    /*else if (!strcmp(key->keyname, "plus"))
      zoom_in_do();
    else if (!strcmp(key->keyname, "minus"))
      zoom_out_do();
    else if (!strcmp(key->keyname, "BackSpace"))
      on_prev_image(NULL, NULL, NULL);
    else if (!strcmp(key->keyname, "Delete"))
      on_delete_image(NULL, NULL, NULL);
    else if (!strcmp(key->keyname, "Escape"))
      on_done(NULL, NULL, NULL);
    else if (!strcmp(key->keyname, "o"))
      on_origscale_image(NULL, NULL, NULL);
    else if (!strcmp(key->keyname, "f"))
      on_fit_image(NULL, NULL, NULL);
    else*/ //{
      printf("keyboard! \"%s\"\n", key->keyname);
      //return EINA_TRUE;
    //}
    //return EINA_FALSE;
  //}
  
  //return EINA_TRUE;
}

Eina_Bool shortcut_elm(void *data, Evas_Object *obj, Evas_Object *src, Evas_Callback_Type type, void *event_info)
{
  int i;
  struct _Evas_Event_Key_Down *key;
  
  if (type == EVAS_CALLBACK_KEY_UP) {
    cur_key_down = 0;
    key_repeat = 0;
  }
  
  if (type ==  EVAS_CALLBACK_KEY_DOWN) {
    key = event_info;
    if (!strcmp(key->keyname, "space")) {
      if (cur_key_down == 1)
	for(i=0;i<REPEATS_ON_STEP_HOLD;i++)
	  on_next_image(NULL, NULL, NULL);
      else
	on_next_image(NULL, NULL, NULL);
      cur_key_down = 1;
      key_repeat = 1;
    }
    else if (!strcmp(key->keyname, "plus"))
      zoom_in_do();
    else if (!strcmp(key->keyname, "minus"))
      zoom_out_do();
    else if (!strcmp(key->keyname, "BackSpace")) {
      if (cur_key_down == 2)
	for(i=0;i<REPEATS_ON_STEP_HOLD;i++)
	  on_prev_image(NULL, NULL, NULL);
      else
	on_prev_image(NULL, NULL, NULL);
      cur_key_down = 2;
      key_repeat = 1;
    }
    else if (!strcmp(key->keyname, "Delete"))
      on_delete_image(NULL, NULL, NULL);
    else if (!strcmp(key->keyname, "Escape"))
      on_done(NULL, NULL, NULL);
    else if (!strcmp(key->keyname, "o"))
      on_origscale_image(NULL, NULL, NULL);
    else if (!strcmp(key->keyname, "r"))
	  workerfinish_schedule(&insert_rotation_do, (void*)(intptr_t)90, NULL, EINA_TRUE);
    else if (!strcmp(key->keyname, "l"))
      workerfinish_schedule(&insert_rotation_do, (void*)(intptr_t)270, NULL, EINA_TRUE);
    else if (!strcmp(key->keyname, "f"))
      on_fit_image(NULL, NULL, NULL);
    else if (!strcmp(key->keyname, "a"))
      on_fullscreen(NULL, NULL, NULL);
    else {
      printf("keyboard! \"%s\"\n", key->keyname);
      return EINA_TRUE;
    }
    return EINA_FALSE;
  }
  
  return EINA_TRUE;
}

Evas_Object *elm_button_add_pack(Evas_Object *p, const char *text, void (*cb)(void *data, Evas_Object *obj, void *event_info))
{
  Evas_Object *btn = elm_button_add(p);
  evas_object_size_hint_weight_set(btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_object_text_set(btn, text);
  elm_box_pack_end(p, btn);
  evas_object_show(btn);
  evas_object_smart_callback_add(btn, "clicked", cb, NULL);
  
  return btn;
}

Evas_Object *elm_fsb_add_pack(Evas_Object *p, const char *text, void (*cb)(void *data, Evas_Object *obj, void *event_info), char *path)
{
  Evas_Object *btn = elm_fileselector_button_add(p);
  elm_fileselector_button_folder_only_set(btn, EINA_TRUE);
  evas_object_size_hint_weight_set(btn, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
  if (path)
    elm_fileselector_button_path_set(btn, path);
  elm_object_text_set(btn, text);
  elm_box_pack_end(p, btn);
  evas_object_show(btn);
  evas_object_smart_callback_add(btn, "file,chosen", cb, NULL);
  
  return btn;
}

void fc_connect_from_list(Eina_List *filters)
{
  Filter *f, *last;
  Eina_List *list_iter;
  Filter_Chain *fc;
  
  last = NULL;
  EINA_LIST_FOREACH(filters, list_iter, f) {
    //filter graph
    if (last)
      filter_connect(last, 0, f, 0);
    
    last = f;
  }
}

void fc_gui_from_list(Eina_List *filters)
{
  Filter *f, *last;
  Eina_List *list_iter;
  Filter_Chain *fc;
  
  assert(filters);
  assert(config_curr);
  
  last = NULL;
  EINA_LIST_FOREACH(filters, list_iter, f) {
    //filter chain
    fc = fc_new(f);
    config_curr->filter_chain = eina_list_append(config_curr->filter_chain, fc);
    
    //create gui, but not for first and last filters
    if (list_iter != filters && list_iter != eina_list_last(filters))
      fc->item = elm_list_item_append(filter_list, f->fc->name, NULL, NULL, &_on_filter_select, eina_list_last(config_curr->filter_chain));
    
    last = f;
  }
  elm_list_go(filter_list);
}

void print_help(void)
{
  printf("usage: limeview - scale invariant image editor/viewer\n");
  printf("limeview [options] [filter1[:set1=val1[:set2=val2]]][,filter2] ... [inputfile/dir]\n");
  printf("   where filter may be one of:\n   \"gauss\", \"sharpen\", \"denoise\", \"contrast\", \"exposure\", \"convert\", \"assert\"\n");
  printf("   source and sink filter in the chain are set by the application\n");
  printf("options:\n");
  printf("   --help,           -h  show this help\n");
  printf("   --cache-size,     -s  set cache size in megabytes (default: 100)\n");
  printf("   --cache-metric,   -m  set cache cache metric (lru/dist/time/hits), \n                         can be repeated for a combined metric (default: lru)\n");
  printf("   --cache-strategy, -f  set cache strategy (rand/rapx/prob, default rapx)\n");
  printf("   --bench,          -b  execute benchmark (global/pan/evaluate/redo/s0/s1/s2/s3)\n                         to off-screen buffer, prints resulting stats\n");
  printf("   --verbose,        -v  prints some more information, mainly cache usage statistics\n");
}

void _on_hq_delay_set(void *data, Evas_Object *obj, void *event_info)
{
  Evas_Object *other_spinner = (Evas_Object*)data;
  
  
  high_quality_delay = elm_spinner_value_get(obj);
  
  if (max_reaction_delay < high_quality_delay)
    elm_spinner_value_set(other_spinner, high_quality_delay);
}

void _on_max_reaction_set(void *data, Evas_Object *obj, void *event_info)
{ 
  Evas_Object *other_spinner = (Evas_Object*)data;
  
  max_reaction_delay = elm_spinner_value_get(obj);
  
  if (max_reaction_delay < high_quality_delay)
    elm_spinner_value_set(other_spinner, max_reaction_delay);
}

void _on_max_scaledown_set(void *data, Evas_Object *obj, void *event_info)
{ 
  max_fast_scaledown = elm_spinner_value_get(obj);
}

Evas_Object *settings_box_add(Evas_Object *parent)
{
  Evas_Object *box, *frame, *spinner_hq, *spinner_mr, *inbox, *lbl, *spinner_scale;
  
  box = elm_box_add(parent);
  evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
  
  frame = elm_frame_add(parent);
  evas_object_size_hint_weight_set(frame, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(frame, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_box_pack_end(box, frame);
  evas_object_show(frame);
  
  inbox = elm_box_add(parent);
  evas_object_size_hint_weight_set(inbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(inbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_object_content_set(frame, inbox);
  evas_object_show(inbox);
  
  spinner_hq = elm_spinner_add(parent);
  spinner_mr = elm_spinner_add(parent);
  spinner_scale = elm_spinner_add(parent);
  
  lbl = elm_label_add(parent);
  elm_object_text_set(lbl, "hq delay");
  elm_box_pack_end(inbox, lbl);
  evas_object_show(lbl);
  
  evas_object_size_hint_weight_set(spinner_hq, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(spinner_hq, EVAS_HINT_FILL, 0);
  elm_spinner_min_max_set (spinner_hq, 0, 1000);
  elm_spinner_step_set (spinner_hq, 10);
  elm_spinner_round_set(spinner_hq, 10);
  elm_spinner_value_set (spinner_hq, high_quality_delay);
  evas_object_smart_callback_add(spinner_hq, "delay,changed", _on_hq_delay_set, spinner_mr);
  elm_box_pack_end(inbox, spinner_hq);
  evas_object_show(spinner_hq);  

  lbl = elm_label_add(parent);
  elm_object_text_set(lbl, "max delay");
  elm_box_pack_end(inbox, lbl);
  evas_object_show(lbl);
  
  evas_object_size_hint_weight_set(spinner_mr, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(spinner_mr, EVAS_HINT_FILL, 0);
  elm_spinner_min_max_set (spinner_mr, 0, 1000);
  elm_spinner_step_set (spinner_mr, 10);
  elm_spinner_round_set(spinner_mr, 10);
  elm_spinner_value_set (spinner_mr, max_reaction_delay);
  evas_object_smart_callback_add(spinner_mr, "delay,changed", _on_max_reaction_set, spinner_hq);
  elm_box_pack_end(inbox, spinner_mr);
  evas_object_show(spinner_mr);
  
  lbl = elm_label_add(parent);
  elm_object_text_set(lbl, "preview scale steps");
  elm_box_pack_end(inbox, lbl);
  evas_object_show(lbl);
  
  evas_object_size_hint_weight_set(spinner_scale, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(spinner_scale, EVAS_HINT_FILL, 0);
  elm_spinner_min_max_set (spinner_scale, 0, 10);
  elm_spinner_step_set (spinner_scale, 1);
  elm_spinner_round_set(spinner_scale, 1);
  elm_spinner_value_set (spinner_scale, max_fast_scaledown);
  evas_object_smart_callback_add(spinner_scale, "delay,changed", _on_max_scaledown_set, NULL);
  elm_box_pack_end(inbox, spinner_scale);
  evas_object_show(spinner_scale);
  
  return box;
}



static void on_new_tag(void *data, Evas_Object *obj, void *event_info)
{  
  const char *new = elm_entry_entry_get((Evas_Object*)data);
  
  if (eina_hash_find(tagfiles_known_tags(files), new))
    return;
  
  tagfiles_add_tag(files, new);
}

int filtered_image_count_get(void)
{
  int count = 0;
  int i;
  
  if (!tags_filter && !eina_hash_population(tags_filter) && !tags_filter_rating)
    return tagfiles_count(files);
  
  for(i=0;i<tagfiles_count(files);i++)
    if (group_in_filters(tagfiles_nth(files, i), tags_filter))
      count++;
    
  return count;
}

static void on_tag_changed(void *data, Evas_Object *obj, void *event_info)
{
  char unit_fmt[32];
  Tags_List_Item_Data *tag = data;
  assert(cur_group);
  
  if (elm_check_state_get(obj)) {
    eina_hash_add(filegroup_tags(cur_group), tag->tag, tag->tag);
  }
  else {
    assert(eina_hash_find(filegroup_tags(cur_group), tag->tag));
    eina_hash_del_by_key(filegroup_tags(cur_group), tag->tag);
  }
  
  filtered_image_count = filtered_image_count_get();
  sprintf(unit_fmt, "%%.0f/%d", filtered_image_count);
  elm_slider_unit_format_set(file_slider, unit_fmt); 
  
  filegroup_filterchain_set(cur_group, lime_filter_chain_serialize(((Filter_Chain*)eina_list_data_get(eina_list_next(config_curr->filter_chain)))->f));
  
  if (!group_in_filters(cur_group, tags_filter))
      workerfinish_schedule(&step_image_do, NULL, NULL, EINA_TRUE);
}

static void on_tag_filter_changed(void *data, Evas_Object *obj, void *event_info)
{
  char unit_fmt[32];
  File_Group *group = cur_group;
  char *tag;
  
  tag = data;
  
  if (elm_check_state_get(obj)) {
    eina_hash_add(tags_filter, tag, tag);
  }
  else {
    assert(eina_hash_find(tags_filter, data));
    eina_hash_del_by_key(tags_filter, data);
  }
  
  filtered_image_count = filtered_image_count_get();
  sprintf(unit_fmt, "%%.0f/%d", filtered_image_count);
  elm_slider_unit_format_set(file_slider, unit_fmt); 
  
  if (!group_in_filters(group, tags_filter))
      workerfinish_schedule(&step_image_do, NULL, NULL, EINA_TRUE);
}

static void on_rating_changed(void *data, Evas_Object *obj, void *event_info)
{
  char unit_fmt[32];
  assert(cur_group);
  
  if (filegroup_rating(cur_group) == elm_segment_control_item_index_get(elm_segment_control_item_selected_get(obj)))
    return;
  
  filegroup_rating_set(cur_group, elm_segment_control_item_index_get(elm_segment_control_item_selected_get(obj)));
  
  filtered_image_count = filtered_image_count_get();
  sprintf(unit_fmt, "%%.0f/%d", filtered_image_count);
  elm_slider_unit_format_set(file_slider, unit_fmt); 
  
  if (!group_in_filters(cur_group, tags_filter))
      workerfinish_schedule(&step_image_do, NULL, NULL, EINA_TRUE);
}

static void on_filter_rating_changed(void *data, Evas_Object *obj, void *event_info)
{
  char unit_fmt[32];
  File_Group *group = cur_group;
  
  tags_filter_rating = elm_segment_control_item_index_get(elm_segment_control_item_selected_get(obj));
  
  filtered_image_count = filtered_image_count_get();
  sprintf(unit_fmt, "%%.0f/%d", filtered_image_count);
  elm_slider_unit_format_set(file_slider, unit_fmt); 
  
  if (!group_in_filters(group, tags_filter))
    workerfinish_schedule(&step_image_do, NULL, NULL, EINA_TRUE);
}

static Evas_Object *_tag_gen_cont_get(void *data, Evas_Object *obj, const char *part)
{ 
  Evas_Object *check;
  Tags_List_Item_Data *tag = data;
  
  if (strcmp(part, "elm.swallow.icon"))
    return NULL;
  
  check = elm_check_add(win);
  elm_object_focus_allow_set(check, EINA_FALSE);

  elm_object_part_text_set(check, "default", tag->tag);
  if (eina_hash_find(filegroup_tags(cur_group), tag->tag))
    elm_check_state_set(check, EINA_TRUE);

  evas_object_smart_callback_add(check, "changed", on_tag_changed, tag);
   
  return check;
}

static Evas_Object *_tag_filter_cont_get(void *data, Evas_Object *obj, const char *part)
{
  Evas_Object *check;
  
  if (strcmp(part, "elm.swallow.icon"))
    return NULL;
  
  check = elm_check_add(win);
  elm_object_focus_allow_set(check, EINA_FALSE);
  
  elm_object_part_text_set(check, "default", data);
  if (eina_hash_find(tags_filter, data))
    elm_check_state_set(check, EINA_TRUE);

  evas_object_smart_callback_add(check, "changed", on_tag_filter_changed, data);
  
  return check;
}

/*static void on_export_type_sel(void *data, Evas_Object *obj, void *event_info)
{
  
}*/

static Evas_Object *export_box_add(Evas_Object *parent)
{
  Evas_Object *btn;
  
  export_box = elm_box_add(parent);
  evas_object_size_hint_weight_set(export_box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(export_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
  
  //hoversel - which files
  /*sel = elm_hoversel_add(parent);
  evas_object_size_hint_weight_set(sel, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(sel, EVAS_HINT_FILL, 0);
  elm_hoversel_item_add(sel, "all files", NULL, ELM_ICON_NONE, &on_export_type_sel, NULL);
  elm_hoversel_item_add(sel, "filtered files", NULL, ELM_ICON_NONE, &on_export_type_sel, NULL);
  elm_hoversel_item_add(sel, "current image", NULL, ELM_ICON_NONE, &on_export_type_sel, NULL);
  elm_box_pack_end(main_box, sel);*/
  
  //file endings to process
  export_extensions = elm_entry_add(export_box);
  elm_entry_scrollable_set(export_extensions, EINA_TRUE);
  elm_entry_single_line_set(export_extensions, EINA_TRUE);
  evas_object_size_hint_weight_set(export_extensions, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(export_extensions, EVAS_HINT_FILL, 0);
  elm_box_pack_end(export_box, export_extensions);
  evas_object_show(export_extensions);
  
  /*export_path = elm_entry_add(export_box);
  elm_entry_scrollable_set(export_path, EINA_TRUE);
  elm_entry_single_line_set(export_path, EINA_TRUE);
  evas_object_size_hint_weight_set(export_path, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(export_path, EVAS_HINT_FILL, 0);
  elm_object_text_set(export_path, "caren@technik-stinkt.de:/home/caren/fotos_upload/");
  elm_box_pack_end(export_box, export_path);
  evas_object_show(export_path);*/
  
  export_path = elm_fileselector_entry_add(export_box);
  elm_fileselector_entry_folder_only_set(export_path, EINA_TRUE);
  evas_object_size_hint_weight_set(export_path, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(export_path, EVAS_HINT_FILL, 0);
  elm_box_pack_end(export_box, export_path);
  evas_object_show(export_path);
  
  /*sel = elm_hoversel_add(parent);
  evas_object_size_hint_weight_set(sel, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(sel, EVAS_HINT_FILL, 0);
  elm_hoversel_item_add(sel, "all files", NULL, ELM_ICON_NONE, &on_export_type_sel, NULL);
  elm_box_pack_end(main_box, sel);
  evas_object_show(sel);*/
  
  //file-path where to
  /*fs = elm_fileselector_entry_add(parent);
  elm_fileselector_entry_folder_only_set(fs, EINA_TRUE);
  evas_object_size_hint_weight_set(fs, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(fs, EVAS_HINT_FILL, 0);
  elm_object_text_set(fs, "target directory");
  elm_box_pack_end(main_box, fs);
  evas_object_show(fs);*/
   
  //file-name manipulation
  
  //label/list - files to process with current settings and output names
  
  //?list of file waiting?number of files/file tree with option to abort single/all files of jobs/all jobs
  
  //execute button
  btn = elm_button_add(export_box);
  evas_object_size_hint_weight_set(btn, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(btn, EVAS_HINT_FILL, 0);
  elm_object_text_set(btn, "execute");
  evas_object_smart_callback_add(btn, "clicked", &on_exe_images_rsync, NULL);
  evas_object_show(btn);
  elm_box_pack_end(export_box, btn);
 
  
  return export_box;
}

EAPI_MAIN int
elm_main(int argc, char **argv)
{
  int help;
  int cache_strategy, cache_metric, cache_size;
  char *file;
  char *dir;
  Evas_Object *hbox, *frame, *hpane, *seg_filter_rating, *entry, *btn;
  Eina_List *filters = NULL;
  select_filter_func = NULL;
  int winsize;
  preload_list = eina_array_new(64);
  
  bench_start();
  
  labelbuf = malloc(1024);
  bench = NULL;
  scale_goal = 1.0;
  
  ecore_init();
  eio_init();
  elm_init(argc, argv);
  efreet_trash_init();
  
  tagfiles_init();
  
  elm_config_scroll_thumbscroll_enabled_set(EINA_TRUE);
  
  max_workers = ecore_thread_max_get();
  ecore_thread_max_set(max_workers*2);
  max_thread_id = max_workers+PRECALC_CONFIG_RANGE+100;
  
  lime_init();
  eina_log_abort_on_critical_set(EINA_TRUE);
  
  mat_cache = mat_cache_new();
  delgrid();
  
  thread_ids = calloc(sizeof(int)*(max_thread_id+1), 1);
  
  if (parse_cli(argc, argv, &filters, &bench, &cache_size, &cache_metric, &cache_strategy, &file, &dir, &winsize, &verbose, &help))
    return EXIT_FAILURE;
  
  if (help) {
    print_help();
    return EXIT_SUCCESS;
  }
  
  //known_tags = eina_hash_stringshared_new(NULL);
  //tags_filter = eina_hash_stringshared_new(NULL);
//  known_tags = eina_hash_string_superfast_new(NULL);
  tags_filter = eina_hash_string_superfast_new(NULL);
  
  //strcpy(image_path, dir);
  //image_file = image_path + strlen(image_path);
  
  print_init_info(bench, cache_size, cache_metric, cache_strategy, file, dir);
  
  if (bench && bench[0].scale != -1)
    fit = bench[0].scale;
  else
    fit = 1;
  
  if (bench)
    elm_config_engine_set("buffer");
  
  win = elm_win_add(NULL, "scalefree viewer", ELM_WIN_BASIC);
  elm_win_modal_set(win, EINA_FALSE);
  evas_object_smart_callback_add(win, "delete,request", on_done, NULL);
  
  bg =  elm_bg_add(win);
  evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  elm_win_resize_object_add(win, bg);
  evas_object_show(bg);
  
  hpane = elm_panes_add(win);
  evas_object_size_hint_weight_set(hpane, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(hpane, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_win_resize_object_add(win, hpane);
  //elm_object_tree_focus_allow_set(hpane, EINA_FALSE);
  evas_object_show(hpane);
  
  main_vbox = elm_box_add(win);
  evas_object_size_hint_weight_set(main_vbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(main_vbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_object_part_content_set(hpane, "left", main_vbox);
  evas_object_show(main_vbox);
  
  vbox_bottom = elm_box_add(win);
  evas_object_size_hint_weight_set(vbox_bottom, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(vbox_bottom, EVAS_HINT_FILL, 0);
  elm_box_pack_end(main_vbox, vbox_bottom);
  evas_object_show(vbox_bottom);

  file_slider = elm_slider_add(win);
  elm_box_pack_end(vbox_bottom, file_slider);
  evas_object_size_hint_weight_set(file_slider, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(file_slider, EVAS_HINT_FILL, 0);
  elm_slider_min_max_set(file_slider, 0, 0);
  
  hbox = elm_box_add(win);
  elm_object_tree_focus_allow_set(hbox, EINA_FALSE);
  evas_object_size_hint_weight_set(hbox, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(hbox, EVAS_HINT_FILL, 0);
  elm_box_horizontal_set(hbox, EINA_TRUE);
  elm_box_pack_end(vbox_bottom, hbox);
  evas_object_show(hbox);
  
  fsb = elm_fsb_add_pack(hbox, "open", &on_open, dir);
  elm_button_add_pack(hbox, "exit", &on_done);
  elm_button_add_pack(hbox, "+", &on_zoom_in);
  elm_button_add_pack(hbox, "-", &on_zoom_out);
  elm_button_add_pack(hbox, "prev", &on_prev_image);
  elm_button_add_pack(hbox, "next", &on_next_image);
  elm_button_add_pack(hbox, "fit", &on_fit_image);
  elm_button_add_pack(hbox, "1:1", &on_origscale_image);
  elm_button_add_pack(hbox, "delete", &on_delete_image);
  
  pos_lbl_buf = malloc(1024);
  pos_label = elm_label_add(win);
  elm_box_pack_end(vbox_bottom, pos_label);
  evas_object_show(pos_label);

  lime_cache_set(cache_size, cache_strategy | cache_metric);
  
  scroller = elm_scroller_add(win);
  evas_object_size_hint_weight_set(scroller, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(scroller, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_box_pack_start(main_vbox, scroller);
  evas_object_smart_callback_add(scroller, "scroll", &on_scroller_move, NULL);
  evas_object_smart_callback_add(scroller, "scroll,drag,stop", &on_scroller_move, NULL);
  evas_object_event_callback_add(scroller, EVAS_CALLBACK_RESIZE, _scroller_resize_cb, NULL);
  //evas_object_event_callback_add(scroller, EVAS_CALLBACK_MOVE, _scroller_resize_cb, NULL);
  evas_object_event_callback_add(scroller, EVAS_CALLBACK_SHOW, _scroller_resize_cb, NULL);
  
  elm_object_event_callback_add(win, &shortcut_elm, NULL);
  //evas_object_key_grab(win, "space", 0, 0, EINA_TRUE);
  //evas_object_key_grab(win, "minus", 0, 0, EINA_TRUE);
  //evas_object_key_grab(win, "BackSpace", 0, 0, EINA_TRUE);
  //evas_object_key_grab(win, "Delete", 0, 0, EINA_TRUE);
  //evas_object_key_grab(win, "Escape", 0, 0, EINA_TRUE);
  //evas_object_key_grab(win, "o", 0, 0, EINA_TRUE);
  //evas_object_key_grab(win, "f", 0, 0, EINA_TRUE);
  //evas_object_event_callback_add(win, EVAS_CALLBACK_KEY_DOWN, shortcut, NULL);
  
  tab_box = elm_box_add(win);
  evas_object_size_hint_weight_set(tab_box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(tab_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_object_part_content_set(hpane, "right", tab_box);
  elm_panes_content_right_size_set(hpane, 0.2);
  evas_object_show(tab_box);
  
  group_list =  elm_list_add(win);
  evas_object_size_hint_weight_set(group_list, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(group_list, EVAS_HINT_FILL, EVAS_HINT_FILL);

  tab_group = group_list;
  evas_object_data_set(tab_group, "limeview,main_tab,refresh_cb", &refresh_group_tab);
  
  tab_tags = elm_box_add(win);
  evas_object_size_hint_weight_set(tab_tags, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(tab_tags, EVAS_HINT_FILL, EVAS_HINT_FILL);
  
  hbox = elm_box_add(win);
  evas_object_size_hint_weight_set(hbox, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(hbox, EVAS_HINT_FILL, 0);
  elm_box_pack_end(tab_tags, hbox);
  elm_box_horizontal_set(hbox, EINA_TRUE);
  evas_object_show(hbox);
  
  entry = elm_entry_add(win);
  elm_entry_scrollable_set(entry, EINA_TRUE);
  elm_entry_single_line_set(entry, EINA_TRUE);
  evas_object_size_hint_weight_set(entry, EVAS_HINT_EXPAND, EVAS_HINT_FILL);
  evas_object_size_hint_align_set(entry, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_box_pack_end(hbox, entry);
  evas_object_show(entry);
  
  btn = elm_button_add(win);
  elm_box_pack_end(hbox, btn);
  elm_object_text_set(btn, "add");
  evas_object_smart_callback_add(btn, "clicked", &on_new_tag, entry);
  evas_object_show(btn);
  
  tags_list_itc = elm_genlist_item_class_new();
  tags_list_itc->item_style = "default";
  tags_list_itc->func.text_get = NULL;
  tags_list_itc->func.content_get = _tag_gen_cont_get;
  tags_list_itc->func.state_get = NULL;
  tags_list_itc->func.del = NULL;
  
  tags_filter_itc = elm_genlist_item_class_new();
  tags_filter_itc->item_style = "default";
  tags_filter_itc->func.text_get = NULL;
  tags_filter_itc->func.content_get = _tag_filter_cont_get;
  tags_filter_itc->func.state_get = NULL;
  tags_filter_itc->func.del = NULL;
  
  tags_list =  elm_genlist_add(win);
  elm_object_tree_focus_allow_set(tags_list, EINA_FALSE);
  elm_box_pack_start(tab_tags, tags_list);
  elm_genlist_select_mode_set(tags_list, ELM_OBJECT_SELECT_MODE_NONE);
  evas_object_size_hint_weight_set(tags_list, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(tags_list, EVAS_HINT_FILL, EVAS_HINT_FILL);
  evas_object_show(tags_list);
  
  seg_rating = elm_segment_control_add(win);
  elm_segment_control_item_selected_set(elm_segment_control_item_add(seg_rating, NULL, "0"), EINA_TRUE);
  elm_segment_control_item_add(seg_rating, NULL, "1");
  elm_segment_control_item_add(seg_rating, NULL, "2");
  elm_segment_control_item_add(seg_rating, NULL, "3");
  elm_segment_control_item_add(seg_rating, NULL, "4");
  elm_segment_control_item_add(seg_rating, NULL, "5");
  evas_object_size_hint_weight_set(seg_rating, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(seg_rating, EVAS_HINT_FILL, 0);
  elm_box_pack_end(tab_tags, seg_rating);
  evas_object_show(seg_rating);
  evas_object_smart_callback_add(seg_rating, "changed", on_rating_changed, NULL);
  
  tags_filter_list =  elm_genlist_add(win);
  elm_object_tree_focus_allow_set(tags_filter_list, EINA_FALSE);
  elm_box_pack_end(tab_tags, tags_filter_list);
  elm_genlist_select_mode_set(tags_filter_list, ELM_OBJECT_SELECT_MODE_NONE);
  evas_object_size_hint_weight_set(tags_filter_list, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(tags_filter_list, EVAS_HINT_FILL, EVAS_HINT_FILL);
  evas_object_show(tags_filter_list);
  
  seg_filter_rating = elm_segment_control_add(win);
  elm_segment_control_item_selected_set(elm_segment_control_item_add(seg_filter_rating, NULL, "0"), EINA_TRUE);
  elm_segment_control_item_add(seg_filter_rating, NULL, "1");
  elm_segment_control_item_add(seg_filter_rating, NULL, "2");
  elm_segment_control_item_add(seg_filter_rating, NULL, "3");
  elm_segment_control_item_add(seg_filter_rating, NULL, "4");
  elm_segment_control_item_add(seg_filter_rating, NULL, "5");
  evas_object_size_hint_weight_set(seg_filter_rating, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(seg_filter_rating, EVAS_HINT_FILL, 0);
  elm_box_pack_end(tab_tags, seg_filter_rating);
  evas_object_show(seg_filter_rating);
  evas_object_smart_callback_add(seg_filter_rating, "changed", on_filter_rating_changed, NULL);
  
  tab_filter = elm_box_add(win);
  elm_object_tree_focus_allow_set(tab_filter, EINA_FALSE);
  evas_object_size_hint_weight_set(tab_filter, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(tab_filter, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_box_pack_end(tab_box, tab_filter);
  evas_object_show(tab_filter);
  
  tab_current = tab_filter;
  
  tab_export = export_box_add(win);
  
  tab_settings = settings_box_add(win);
  
  Evas_Object *tb = elm_toolbar_add(win);
  evas_object_size_hint_weight_set(tb, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(tb, EVAS_HINT_FILL, 0);
  elm_toolbar_item_selected_set(elm_toolbar_item_append (tb, NULL, "filter", on_tab_select, tab_filter), EINA_TRUE);
  elm_toolbar_item_append (tb, NULL, "group", on_tab_select, tab_group);
  elm_toolbar_item_append (tb, NULL, "tags", on_tab_select, tab_tags);
  elm_toolbar_item_append (tb, NULL, "export", on_tab_select, tab_export);
  elm_toolbar_item_append (tb, NULL, "settings", on_tab_select, tab_settings);
  elm_box_pack_start(tab_box, tb);
  evas_object_show(tb);
  
  hbox = elm_box_add(win);
  evas_object_size_hint_weight_set(hbox, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(hbox, EVAS_HINT_FILL, 0);
  elm_box_horizontal_set(hbox, EINA_TRUE);
  elm_box_pack_end(tab_filter, hbox);
  evas_object_show(hbox);
  
  elm_button_add_pack(hbox, "insert before", &on_insert_before);
 
  select_filter = elm_hoversel_add(win);
  evas_object_size_hint_weight_set(select_filter, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(select_filter, EVAS_HINT_FILL, EVAS_HINT_FILL);
  evas_object_size_hint_min_set (select_filter, 100, 0);
  elm_box_pack_end(hbox, select_filter);
  elm_hoversel_item_add(select_filter, "contrast", NULL, ELM_ICON_NONE, &on_select_filter_select, &filter_core_contrast);
  elm_hoversel_item_add(select_filter, "exposure", NULL, ELM_ICON_NONE, &on_select_filter_select, &filter_core_exposure);
  elm_hoversel_item_add(select_filter, "gauss", NULL, ELM_ICON_NONE, &on_select_filter_select, &filter_core_gauss);
  elm_hoversel_item_add(select_filter, "sharpen", NULL, ELM_ICON_NONE, &on_select_filter_select, &filter_core_sharpen);
  elm_hoversel_item_add(select_filter, "denoise", NULL, ELM_ICON_NONE, &on_select_filter_select, &filter_core_denoise);
  elm_hoversel_item_add(select_filter, "rotate", NULL, ELM_ICON_NONE, &on_select_filter_select, &filter_core_simplerotate);
  elm_object_text_set(select_filter, "contrast");
  select_filter_func = filter_core_contrast.filter_new_f;
  evas_object_show(select_filter);
  
  elm_button_add_pack(hbox, "insert after", &on_insert_after);
  
  frame = elm_frame_add(win);
  elm_object_text_set(frame, "filter chain");
  evas_object_size_hint_weight_set(frame, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(frame, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_box_pack_end(tab_filter, frame);
  evas_object_show(frame);
  
  filter_list = elm_list_add(win);
  elm_object_content_set(frame, filter_list);
  evas_object_show(filter_list);
   
  on_open_dir(dir);
  
  /*test_filter_config(load);
   s ize = *(*Dim*)filter_core_by_type(sink, MT_IMGSIZE);
   
   printf("image size: %dx%d\n", size.width, size.height);*/
  
  
  // now we are done, show the window
  
  // run the mainloop and process events and callbacks
  //fill_area(0,0,1024,1024,MAX_FAST_SCALEDOWN,1);
  
  evas_object_size_hint_min_set(win, 128, 128);
  
  evas_object_show(win);
  elm_scroller_region_show(scroller, 0, 0, 1, 1);
  
  //FIXME
  ecore_event_handler_add(ECORE_EXE_EVENT_DEL, &_rsync_term, NULL);
  
  if (winsize)
    evas_object_resize(win, winsize, winsize);
  else if (bench)
    evas_object_resize(win, 1024, 1024);
  else
	  elm_win_maximized_set(win, EINA_TRUE);
  
  bench_time_mark(BENCHMARK_INIT);
  elm_run();
  bench_time_mark(BENCHMARK_PROCESSING);
  if (verbose)
    cache_stats_print();
  if (bench)
    bench_report();
  lime_shutdown();
  
  tagfiles_shutdown();
  
  efreet_trash_shutdown();
  eio_shutdown();
  elm_shutdown();
  ecore_shutdown();
  
  return 0;
}
ELM_MAIN()