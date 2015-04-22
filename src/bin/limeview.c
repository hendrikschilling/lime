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
#include "settings.h"

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
#include "filter_rotate.h"
#include "filter_curves.h"
#include "filter_lrdeconv.h"
#include "filter_lensfun.h"
#include "exif_helpers.h"

#define TILE_SIZE DEFAULT_TILE_SIZE

#define PENDING_ACTIONS_BEFORE_SKIP_STEP 10
#define REPEATS_ON_STEP_HOLD 10
#define EXTRA_THREADING_FACTOR 4
#define PRELOAD_EXTRA_WORKERS 32

//#define BENCHMARK
//#define BENCHMARK_PREVIEW
#define BENCHMARK_LENGTH 2000

//FIXME adjust depending on speed!
//FIXME fix threaded config
#define PRELOAD_CONFIG_RANGE 32
#define PRELOAD_IMG_RANGE 2
#define PRELOAD_THRESHOLD 4

//#define DISABLE_CONFIG_PRELOAD
//FIXME leaks memory (configs get called again and again when stepping forth back one)
#define DISABLE_IMG_PRELOAD

#ifdef IF_FREE
# undef IF_FREE
#endif
#define IF_FREE(ptr) if (ptr) {free(ptr); } ptr = NULL;

int max_preload_workers = -1;
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
Eina_List *preload_list = NULL;
Eina_Array *preload_array = NULL;
int preload_count = 0;
int quick_preview_only = 0;
int cur_key_down = 0;
int key_repeat = 0;
int preloaded_configs = 0;

const double zoom_fac = 1.4142136;

pthread_mutex_t barrier_lock;

int config_waitfor_idx;
int config_waitfor_groupidx;

Tagfiles *files = NULL;
Tagfiles *files_new = NULL;
int last_file_step = 1;

Elm_Genlist_Item_Class *tags_list_itc;
Elm_Genlist_Item_Class *tags_filter_itc;

struct timespec *delay_cur;

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
  int running;
  Eina_List *filter_chain;
  Tagged_File *file;
  File_Group *group;
  int file_group_idx;
} Config_Data;

typedef struct {
  Eina_Matrixsparse **mats;
  Evas_Object **high_of_layer;
  Evas_Object **low_of_layer;
  int scale_max;
  Eina_Array **imgs;
} Mat_Cache;

typedef struct {
  int step;
  int group_idx;
} Tagfiles_Step;


int max_workers;
int max_thread_id;
Evas_Object *clipper, *win, *scroller, *file_slider, *filter_list, *select_filter, *pos_label, *fsb, *load_progress, *load_label, *load_notify;
Evas_Object *settings_rule_cam_lbl, *settings_rule_format_lbl, *settings_rule_list, *settings_rule_del_btn;
Evas_Object *tab_group, *tab_filter, *tab_settings, *tab_tags, *tab_current, *tab_box, *tab_export, *tab_tags, *tags_list, *tags_filter_list, *seg_rating;
Evas_Object *group_list, *export_box, *export_extensions, *export_path, *main_vbox;
Evas_Object *grid = NULL, *vbox_bottom, *bg;
char *labelbuf;
char *pos_lbl_buf;
char *cam_cur = NULL, *format_cur = NULL;
int posx, posy;
Evas_Object *slider_blur, *slider_contr, *gridbox = NULL;
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
Tagged_File *cur_file = NULL;
int cur_group_idx = 0;

Limeview_Settings *settings = NULL;

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
  struct timespec *delay;
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

Tagfiles_Step *tagfiles_step_new(int step, int group_idx)
{
  Tagfiles_Step *ts = malloc(sizeof(Tagfiles_Step));
  ts->step = step;
  ts->group_idx = group_idx;
  
  return ts;
}

Dim *config_size(Config_Data *config)
{
  if (!config || !config->sink)
    return NULL;
  
  return filter_core_by_type(config->sink, MT_IMGSIZE);
}

void exif_infos(lime_exif *exif, char **cam, char **lens)
{
  if (!exif)
    return;
  
  if (cam)
    *cam = lime_exif_model_make_string(exif);
  
  if (lens)
    *lens = lime_exif_lens_string(exif);
}

void config_exif_infos(Config_Data *config, char **cam, char **lens)
{
  lime_exif *exif;
  
  if (!config || !config->sink) {
    printf("ERROR: exif info: not config given\n");
    return;
  }
  
  exif =  filter_core_by_subtype(config->sink, MT_OBJ, "exif");
  
  exif_infos(exif, cam, lens);
}

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
    
    if (delay_cur)
      free(delay_cur);
    delay_cur = action->delay;
    action->action(action->data, action->obj);
  }
}

void pending_add(void (*action)(void *data, Evas_Object *obj), void *data, Evas_Object *obj)
{
  _Pending_Action *pending = malloc(sizeof(_Pending_Action));
  
  pending->action = action;
  pending->data = data;
  pending->obj = obj;
  pending->delay = malloc(sizeof(struct timespec));
  bench_delay_start(pending->delay);
  
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

void grid_setsize(void)
{
  Dim *size;
  int x,y,w,h;
  
  if (!config_curr || !config_curr->sink )
    return;
  
  size = config_size(config_curr);
  
  if (!size)
    return;
  
  //FIXME can any of these calls result in fill_area?
  elm_grid_size_set(grid, size->width, size->height);
  elm_grid_pack_set(clipper, 0, 0, size->width, size->height);
  elm_box_recalculate(gridbox);
  
  if (fit) {
    elm_scroller_region_get(scroller,&x,&y,&w,&h);

    //FIXME!!!
    if (!w || !h) {
      printf("scroller has no area!\n");
      return;
    }
    else {
      scale_goal = (float)size->width / w;	
      if ((float)size->height / h > scale_goal)
	scale_goal = (float)size->height / h;
    }
  }
    
  evas_object_size_hint_min_set(grid,  size->width/scale_goal, size->height/scale_goal);
  elm_box_recalculate(gridbox);
}

void fc_save_change_update_scroller(void)
{
  int failed = lime_config_test(config_curr->sink);
  
  if (failed) 
    printf("FIXME change to filterchain caused config to fail!\n");
  
  tagged_file_filterchain_set(cur_file, cur_group, lime_filter_chain_serialize(((Filter_Chain*)eina_list_data_get(eina_list_next(config_curr->filter_chain)))->f));
  
  delgrid();
  
  grid_setsize();
  
  forbid_fill--;
  
  fill_scroller();
}

void int_changed_do(void *data, Evas_Object *obj)
{
  Meta *m = data;
  
  assert(!worker);
  assert(cur_group);
  assert(config_curr);

  forbid_fill++;
  
  lime_setting_int_set(m->filter, m->name, (int)elm_spinner_value_get(obj));
  
  fc_save_change_update_scroller();
}

void float_changed_do(void *data, Evas_Object *obj)
{
  Meta *m = data;
  
  assert(!worker);
  assert(cur_group);
  
  forbid_fill++;
  
  lime_setting_float_set(m->filter, m->name, (float)elm_spinner_value_get(obj));
  
  fc_save_change_update_scroller();
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
  
  fc_save_change_update_scroller();
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
  
  forbid_fill++;
  
  assert(cur_group);
  assert(config_curr);
  
  //we always have src or sink, but those might not have an elm_item!
  assert(src);
  assert(sink);
  
  delgrid();
  
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
  
  fc_save_change_update_scroller();
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
  
  if (fc->f->settings)
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
  
  evas_image_cache_flush(evas_object_evas_get(win));
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

float config_actual_scale_get(Config_Data *config)
{
  int x,y,w,h,grid_w,grid_h;
  Dim *size = config_size(config);
  
  if (!size)
    return 0.0;
  
  elm_scroller_region_get(scroller, &x, &y, &w, &h);
  evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
    
  return (float)size->width / grid_w;
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
    if (tagfiles_idx(files) >= BENCHMARK_LENGTH)
      workerfinish_schedule(&elm_exit_do, NULL, NULL, EINA_TRUE);
    else
      workerfinish_schedule(&step_image_do, tagfiles_step_new(1,0), NULL, EINA_TRUE);
  }
#endif
  
  return ECORE_CALLBACK_CANCEL;
}

void workerfinish_idle_run(void *data)
{ 
  workerfinish_idle = NULL;
  
  if (worker) {
    printf("FIXME: workerfinish_idle_run() still running workers!");
    return ECORE_CALLBACK_CANCEL;
  }
  
  pending_exe();
  
  return ECORE_CALLBACK_CANCEL;
}



void preload_add(_Img_Thread_Data *tdata)
{
  ea_push(preload_array, tdata);
}

int preload_pending(void)
{ 
  return ea_count(preload_array);
}


void preload_flush(void)
{
  eina_array_flush(preload_array);
}


_Img_Thread_Data *preload_get(void)
{
  return ea_pop(preload_array);
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
      if (delay_cur)
	free(delay_cur);
      delay_cur = malloc(sizeof(struct timespec));
      bench_delay_start(delay_cur);
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
  lime_filter_config_unref(tdata->config->load);
}

static void _process_tile_bg(void *data, Ecore_Thread *th)
{
  _Img_Thread_Data *tdata = data;
    
  eina_sched_prio_drop();
  eina_sched_prio_drop();
  lime_render_area(&tdata->area, tdata->config->sink, tdata->t_id);
  lime_filter_config_unref(tdata->config->load);
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
  //evas_object_image_data_set(tdata->img, NULL);
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
      
  if (verbose)
    printf("final delay for preview: %f\n", bench_delay_get(delay_cur));

  preview_timer = NULL;

    
  return ECORE_CALLBACK_CANCEL;
}

static void _finished_tile_blind(void *data, Ecore_Thread *th);

void run_preload_threads(void)
{
  _Img_Thread_Data *tdata;
  
  while (worker_preload<max_preload_workers && preload_pending()) {
    tdata = preload_get();
    //config was reset...
    if (!tdata->config->sink)
      continue;
    worker_preload++;
    tdata->t_id = lock_free_thread_id();
    filter_memsink_buffer_set(tdata->config->sink, NULL, tdata->t_id);
    lime_filter_config_ref(tdata->config->sink);
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
  double delay = bench_delay_get(delay_cur);
  
  assert(tdata->t_id != -1);
  
  thread_ids[tdata->t_id] = 0;
  tdata->t_id = -1;
 
#ifdef BENCHMARK_PREVIEW
  if (tagfiles_idx(files) >= BENCHMARK_LENGTH)
    workerfinish_schedule(&elm_exit_do, NULL, NULL, EINA_TRUE);
  else
    workerfinish_schedule(&step_image_do, tagfiles_step_new(1,0), NULL, EINA_TRUE);
#endif
  
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
	if (!worker) {
	  _display_preview(NULL);
	}
	
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
  
  if (!worker && verbose)
    printf("final delay: %f\n", bench_delay_get(delay_cur));
  
  _insert_image(tdata);
  
  first_preview = 0;
  
  if (!worker && pending_action()) {
    if (mat_cache_old) {
      if (verbose)
        printf("final delay preview: %f\n", bench_delay_get(delay_cur));
      _display_preview(NULL);
    }
    //this will schedule an idle enterer to only process func after we are finished with rendering
    workerfinish_idle = ecore_job_add(workerfinish_idle_run, NULL);
  }
  else if (!worker /*&& preload_pending() < PRELOAD_THRESHOLD*/) {
#ifndef  DISABLE_IMG_PRELOAD
    step_image_preload_next(PRELOAD_IMG_RANGE);
#endif
#ifndef DISABLE_CONFIG_PRELOAD
    step_image_start_configs(PRELOAD_CONFIG_RANGE);
#endif
  }
  
  run_preload_threads();
  
#ifdef BENCHMARK
  if (!worker && !idle_render) {
    if (tagfiles_idx(files) >= BENCHMARK_LENGTH)
      workerfinish_schedule(&elm_exit_do, NULL, NULL, EINA_TRUE);
    else
      workerfinish_schedule(&step_image_do, tagfiles_step_new(1,0), NULL, EINA_TRUE);
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
  Dim *size = config_size(config);

  elm_scroller_region_get(scroller, &x, &y, &w, &h);
  
  if (!w || !h || !size)
    return 0;
  
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
  
  if (minscale > size->scaledown_max)
    minscale = size->scaledown_max;
  
  scale_start = minscale + max_fast_scaledown;
  
  if (scale_start > size->scaledown_max)
    scale_start = size->scaledown_max;
  
  for(scale=scale_start;scale>=minscale;scale--) {
    //additional scaledown for preview
    scalediv = ((uint32_t)1) << scale;
    for(j=y/TILE_SIZE/scalediv;j<(y+h+TILE_SIZE*scalediv-1)/TILE_SIZE/scalediv;j++)
      for(i=x/TILE_SIZE/scalediv;i<(x+w+TILE_SIZE*scalediv-1)/TILE_SIZE/scalediv;i++) {

	if (i*TILE_SIZE*scalediv >= size->width || j*TILE_SIZE*scalediv >= size->height) {
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
	  if (maxx > size->width) {
	    maxx = size->width;
	  }
	  if (maxy > size->height) {
	    maxy = size->height;
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
  Dim *size = config_size(config_curr);
  
  if (!size)
    return 0;
  
  if (forbid_fill)
    return 0;
  
  if (first_preview && worker)
    return 0;
  
  if (worker >= max_workers)
    return 0;
  
  if (!grid)
    return 0;
  
  elm_scroller_region_get(scroller, &x, &y, &w, &h);
  
  if (!w || !h) {
    printf("FIXME avoid fill_scroller_preview: scroller does not yet have region!");
    return 0;
  }
  
  actual_scalediv = config_actual_scale_get(config_curr);
  
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
  
  if (minscale > size->scaledown_max)
    minscale = size->scaledown_max;
  
  scale_start = minscale + max_fast_scaledown;
  
  if (scale_start > size->scaledown_max)
    scale_start = size->scaledown_max;
  
  for(scale=scale_start;scale>=minscale;scale--) {
    //additional scaledown for preview
    scalediv = ((uint32_t)1) << scale;
    for(j=y/TILE_SIZE/scalediv;j<(y+h+TILE_SIZE*scalediv-1)/TILE_SIZE/scalediv;j++)
      for(i=x/TILE_SIZE/scalediv;i<(x+w+TILE_SIZE*scalediv-1)/TILE_SIZE/scalediv;i++) {

        cell = mat_cache_get(mat_cache, scale, i, j);
	
	if (i*TILE_SIZE*scalediv >= size->width || j*TILE_SIZE*scalediv >= size->height) {
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
	  if (maxx > size->width) {
	    maxx = size->width;
	  }
	  if (maxy > size->height) {
	    maxy = size->height;
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
	  
	  lime_filter_config_ref(tdata->config->sink);
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
  Dim *size;
  
  if (!config_curr || !grid)
    return;

  evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
  elm_scroller_region_get(scroller, &x, &y, &w, &h);
  size = config_size(config_curr);
  
  if (!w || !h || !size)
    return;

  if (grid_w && grid_h) {
  scale = size->width / grid_w;	
  if ((float)size->height / grid_h > scale)
    scale = (float)size->height / grid_h;
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
  size = config_size(config);
  
  if (!w || !h || !size) {
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
  step_image_do(tagfiles_step_new(0,*(int*)data), NULL);
  free(data);
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
    mat_cache_del(mat_cache_old);
    mat_cache_old = NULL;
  }
  
  //mat_cache_flush(mat_cache);
  mat_cache_old = mat_cache;
  mat_cache = mat_cache_new();
  
  finished_threads = eina_array_new(16);
}



void jump_image_do(void *data, Evas_Object *obj)
{
  step_image_config_reset_range(files, tagfiles_idx(files)-PRELOAD_CONFIG_RANGE-1, tagfiles_idx(files)+PRELOAD_CONFIG_RANGE+1);
  preloaded_configs = 0;
  //FIXME reset current config but only after processing in step_image do??? and only if outside config range...
  
  tagfiles_idx_set(files, (intptr_t)data);
  step_image_do(NULL, NULL);
}

static void on_jump_image(void *data, Evas_Object *obj, void *event_info)
{
  if (!files)
    return;
  
  int idx = elm_slider_value_get(file_slider);

  if (idx == tagfiles_idx(files))
    return;
  
  //FIXME don't reset around next/target idx
  //FIXME we still will leak configs if we are currently rendering stuff!
  //FIXME segfaults if this happens - disable for now!
  
  if (mat_cache_old && !worker)
    _display_preview(NULL);
  //FIXME detect if same actions come behind each other and skip?
  workerfinish_schedule(&jump_image_do, (intptr_t)idx, NULL, EINA_FALSE);
}


static void
on_group_select(void *data, Evas_Object *obj, void *event_info)
{
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
  
  if (!filegroup_tags_valid(group)) {
    if ((filters && eina_hash_population(filters)) || tags_filter_rating)
      return 0;
    else
      return 1;
  }
  
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

void group_config_reset(File_Group *group)
{
  int i;
  Config_Data *config;
  
  if (!group)
    return;
  
  //printf("FIXME temporary: no group_config_reset\n");
  
  //FIXME clean pending refs to config from preload_list (and other?)
  
  
  if (!filegroup_basename(group)) {
    printf("FIXME what does this mean?\n");
    return;
  }
  
  for(i=0;i<filegroup_count(group);i++)
  {
    config = filegroup_data_get(group, i);
    //FIXME if config->running -> the config thread is still waiting to execute? cancel that thread?
    //printf("del config for %s\n", tagged_file_name(filegroup_nth(group,i), config, config_curr, );
    
    if (config && config != config_curr && config->sink) {
      while(config->running) {
        pthread_mutex_lock(&barrier_lock);
        usleep(20);
        pthread_mutex_unlock(&barrier_lock);
      }
      //FIXME del filters?
      lime_config_reset(config->sink);
      config->sink = NULL;
    }
  }
  
  //FIXME delete group cb
  //printf("FIXME delete group_changed_cb for %s\n", filegroup_basename(group));
  if (config) //at least one config was created for the group
    tagfiles_group_changed_cb_delete(files, group);
}

void filegroup_changed_cb(File_Group *group)
{
  char unit_fmt[32];
  
  printf("file group changed. FIXME figure out what changed and avoid full reload!\n");
  
  //filters may have changed -> reset config
  group_config_reset(group);
  
  //if tags have changed
  if (tags_filter_rating || (tags_filter && eina_hash_population(tags_filter)))
    if (group_in_filters(group, tags_filter)) {  
      filtered_image_count = filtered_image_count_get();
      sprintf(unit_fmt, "%%.0f/%d", filtered_image_count);
      elm_slider_unit_format_set(file_slider, unit_fmt); 
    }
  
  //FIXME do we have to schedule this even if something else is running?
  if (cur_group == group)
    workerfinish_schedule(step_image_do, NULL, NULL, EINA_TRUE);
}

Config_Data *config_build(File_Group *group, int nth)
{
  Eina_List *filters;
  char *filename;
  Config_Data *config;
  
  filename = tagged_file_name(filegroup_nth(group, nth));
  //FIXME include some fast file compatibility checks!
  if (!filename)
    return NULL;
  printf("config for %s\n", filename);


  config = filegroup_data_get(group, nth);
  
  if (config && config->sink) {
    if (config->running)
      while(config->running) {
        pthread_mutex_lock(&barrier_lock);
        usleep(20);
        pthread_mutex_unlock(&barrier_lock);
      }
      
      return config;
  }
  else if (config) {
    printf("FIXME config_build: config but no sink %d\n", config->failed);
  }
  
  tagfiles_group_changed_cb_insert(files, group, filegroup_changed_cb);
  
  config = calloc(sizeof(Config_Data), 1);
  config->group = group;
  config->file = filegroup_nth(group, nth);
  config->file_group_idx = nth;
  
  if (filegroup_tags_valid(group) && tagged_file_filterchain(config->file)) {
    filters = lime_filter_chain_deserialize(tagged_file_filterchain(config->file));
    
    //FIXME this triggers sometimes on load!
    assert(filters);
    
    config->load = eina_list_data_get(filters);
    if (strcmp(config->load->fc->shortname, "load")) {
      config->load = lime_filter_new("load");
      filters = eina_list_prepend(filters, config->load);
    }
    config->sink = eina_list_data_get(eina_list_last(filters));
    if (strcmp(config->sink->fc->shortname, "sink")) {
      config->sink = lime_filter_new("memsink");
      lime_setting_int_set(config->sink, "add alpha", 1);
      filters = eina_list_append(filters, config->sink);
    }
  }
  else {
    printf("no filterchain, check defaults\n");
    lime_exif *exif = lime_exif_handle_new_from_file(filename);
    char *cam = NULL;
    Eina_List *l;
    Default_Fc_Rule *rule;
    char *format;
    
    exif_infos(exif, &cam, NULL);
    format = strrchr(filename, '.');
    
    EINA_LIST_FOREACH(settings->default_fc_rules, l, rule) {
      if ((!cam && rule->cam) || strcmp(rule->cam, cam))
        continue;
      if ((!format && rule->format) || strcmp(rule->format, format))
        continue;
      
      group_config_reset(config->group);
      
      //FIXME merge this with config_build()
      filters = lime_filter_chain_deserialize(rule->fc);
      assert(filters);
      
      config->load = eina_list_data_get(filters);
      if (strcmp(config->load->fc->shortname, "load")) {
        config->load = lime_filter_new("load");
        filters = eina_list_prepend(filters, config->load);
      }
      config->sink = eina_list_data_get(eina_list_last(filters));
      if (strcmp(config->sink->fc->shortname, "sink")) {
        config->sink = lime_filter_new("memsink");
        lime_setting_int_set(config->sink, "add alpha", 1);
        filters = eina_list_append(filters, config->sink);
      }      
      break;
    }
    lime_exif_handle_destroy(exif);
    
    if (!config->load) {
      config->load = lime_filter_new("load");
      filters = eina_list_append(NULL, config->load);
      config->sink = lime_filter_new("memsink");
      lime_setting_int_set(config->sink, "add alpha", 1);
      filters = eina_list_append(filters, config->sink);
    }
  } 
  
  //strcpy(image_file, filename);
  filegroup_data_attach(group, nth, config);
  
  fc_connect_from_list(filters);
  lime_setting_string_set(config->load, "filename", filename);
  
  return config;
}

//FIXME merge this with config_thread_start
Config_Data *config_data_get(File_Group *group, int nth)
{
  int i;
  Eina_List *filters, *l;
  Default_Fc_Rule *rule;
  char *cam = NULL,
       *format, 
       *fc;
  
  Config_Data *config = config_build(group, nth);
  
  if (!config)
    return NULL;
  
  //FIXME check if config has already been run?!
  /*if (config && config->sink)
    return config;*/

  config->failed = lime_config_test(config->sink);
  
  return config;
}

void config_exe(void *data, Ecore_Thread *thread)
{
  Config_Data *config = data;
  
  config->failed = lime_config_test(config->sink);
  
  config->running = EINA_FALSE;
}

void config_finish(void *data, Ecore_Thread *thread)
{
  int nth;
  Config_Data *config = data;
  _Img_Thread_Data *tdata = calloc(sizeof(_Img_Thread_Data), 1);
  uint8_t *buf;
  Dim *size = config_size(config);
  
  worker_config--;
  assert(config->sink);
  
  if (config->failed || !size) {
    free(tdata);
    nth = config->file_group_idx+1;
    if (nth < filegroup_count(config->group))
      config_thread_start(config->group, nth);
  }
  else {
    tdata->scale =  size->scaledown_max;
    tdata->area.corner.x = 0;
    tdata->area.corner.y = 0;
    tdata->area.corner.scale = size->scaledown_max;
    tdata->area.width = TILE_SIZE;
    tdata->area.height =  TILE_SIZE;
    tdata->config = config;
    
    preload_add(tdata);
  }
  
  run_preload_threads();
}

//FIXME unify config_thread_start and config_data_get
void config_thread_start(File_Group *group, int nth)
{
  /*char *filename;
  Config_Data *config;
  Eina_List *filters;
  
  config = filegroup_data_get(group, nth);
  if (config)
    return;
  
  filename = tagged_file_name(filegroup_nth(group, nth));
  //FIXME better file ending list (no static file endings!)
  if (!filename || (!eina_str_has_extension(filename, ".jpg") 
   && !eina_str_has_extension(filename, ".JPG")
    && !eina_str_has_extension(filename, ".tif")
    && !eina_str_has_extension(filename, ".TIF")
    && !eina_str_has_extension(filename, ".tiff")
    && !eina_str_has_extension(filename, ".TIFF"))) {
    return;
}
  
  tagfiles_group_changed_cb_insert(files, group, filegroup_changed_cb);
  
  config = calloc(sizeof(Config_Data), 1);
  if (filegroup_tags_valid(group) && tagged_file_filterchain(filegroup_nth(group, nth))) {
    filters = lime_filter_chain_deserialize(tagged_file_filterchain(filegroup_nth(group, nth)));
    
    //FIXME select group according to load file 
    config->load = eina_list_data_get(filters);
    if (strcmp(config->load->fc->shortname, "load")) {
      config->load = lime_filter_new("load");
      filters = eina_list_prepend(filters, config->load);
    }
    config->sink = eina_list_data_get(eina_list_last(filters));
    if (strcmp(config->sink->fc->shortname, "sink")) {
      config->sink = lime_filter_new("memsink");
      lime_setting_int_set(config->sink, "add alpha", 1);
      filters = eina_list_append(filters, config->sink);
    }
    fc_connect_from_list(filters);
  }
  else {
    
    config->load = lime_filter_new("load");
    filters = eina_list_append(NULL, config->load);
    config->sink = lime_filter_new("memsink");
    lime_setting_int_set(config->sink, "add alpha", 1);
    filters = eina_list_append(filters, config->sink);
    
    fc_connect_from_list(filters);
  }
  
  //strcpy(image_file, filename);
  lime_setting_string_set(config->load, "filename", filename);
  
  filegroup_data_attach(group, nth, config);
  
  worker_config++;
  config->running = EINA_TRUE;
  ecore_thread_run(&config_exe, &config_finish, NULL, config);*/
  
  Config_Data *config = filegroup_data_get(group, nth);
  if (config)
    return;
  
  config = config_build(group,  nth);
  if (!config)
    return;
  
  if (config && config->sink)
    return;
  
  worker_config++;
  config->running = EINA_TRUE;
  assert(config->sink);
  ecore_thread_run(&config_exe, &config_finish, NULL, config);
}

//FIXME parallel configs might block limited worker threads with io!?
void step_image_start_configs(int n)
{
  int i;
  int config_upto;
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
  
  group = tagfiles_nth(files, idx+step*n);
  assert(group);
  if (group_in_filters(group, tags_filter))
      config_thread_start(group, 0);
  
  /*if (n > preloaded_configs) {
    if (preloaded_configs == 0) {
      preloaded_configs = 1;
      return;
    }
    else
      config_upto = 2+preloaded_configs;
    if (config_upto > n)
      config_upto = n;
  }
  printf("preload from %d: %d (%d %d)", idx, idx + preloaded_configs*step, preloaded_configs, config_upto);
  idx = idx + preloaded_configs*step;
  for (i=preloaded_configs;i<config_upto;i++,idx+=step) {
    group = tagfiles_nth(files, idx);
    
    assert(group);
    
    if (group_in_filters(group, tags_filter)) {
      for(group_idx=0;group_idx<filegroup_count(group);group_idx++)
	config_thread_start(group, group_idx);
    }
  }
  preloaded_configs = config_upto-1;*/
}

//FIXME 
void step_image_preload_next(int n)
{
  int i;
  int group_idx;
  int idx;
  File_Group *group;
  Config_Data *config;
  int step;
  int succ;

  assert(files);
  
  if (!tagfiles_count(files))
    return;
  
  if (!last_file_step)
    step = 1;
  else
    step = last_file_step;
  
  idx = (tagfiles_idx(files)+tagfiles_count(files)+step) % tagfiles_count(files);
  
  for(i=0;i<n;i++) {
    succ = 0;
    while (!succ && idx != tagfiles_idx(files)) {
      group = tagfiles_nth(files, idx);
      if (group_in_filters(group, tags_filter)) {
	for(group_idx=0;group_idx<filegroup_count(group);group_idx++) {
	  config = config_data_get(group, group_idx);
	  if (config && !config->failed) {
	    fill_scroller_blind(config);
            if (verbose)
                printf("preload %s\n", tagged_file_name(filegroup_nth(group, group_idx)));
	    succ = 1;
	    break;
	  }
	}
      }
      idx += step;
    }
  }
}

//FIXME what about wrapping? problems with small number of files (reset before use...)
void step_image_config_reset_range(Tagfiles *files, int start, int end)
{
  int i;
  
  printf("reset range %d-%d\n", start, end);
  
  for(i=start;i<=end;i++)
    group_config_reset(tagfiles_nth(files, i));
}

//TODO we really need config cache!
void reset_all_configs(Tagfiles *files)
{
  int i;
  
  for(i=0;i<tagfiles_count(files);i++) {
    if (tagfiles_idx(files) == i)
      printf("FIXME: reset and recalc curretn config (using workefinish schedule...)\n");
    else
      group_config_reset(tagfiles_nth(files, i));
  }
}

void refresh_tab_tags(void)
{
  if (tab_current != tab_tags)
    return;
  
  elm_genlist_realized_items_update(tags_list);
  
  elm_segment_control_item_selected_set(elm_segment_control_item_get(seg_rating, filegroup_rating(cur_group)), EINA_TRUE);
}

void refresh_tab_setings(void)
{
  char *cam = NULL;
  char *buf;
  char *format = NULL;
  
  if (tab_current != tab_settings)
    return;
  
  config_exif_infos(config_curr, &cam, NULL);
  
  if (cam) {
    //FIXME stringshare...
    buf = malloc(strlen(cam)+9);
    sprintf(buf, "camera: %s", cam);
    elm_object_text_set(settings_rule_cam_lbl, buf);
    cam_cur = cam;
  }
  else {
    cam_cur = NULL;
    elm_object_text_set(settings_rule_cam_lbl, "camera: -");
  }
  
  if (config_curr)
    format = strdup(strrchr(tagged_file_name(filegroup_nth(tagfiles_get(files), cur_group_idx)), '.'));
    
  if (format) {
    //FIXME stringshare...
    buf = malloc(strlen(format)+9);
    sprintf(buf, "format: %s", format);
    elm_object_text_set(settings_rule_format_lbl, buf);
    format_cur = format;
  }
  else {
    format_cur = NULL;
    elm_object_text_set(settings_rule_format_lbl, "file type: -");
  }
}

void step_image_do(void *data, Evas_Object *obj)
{
  int i;
  Tagfiles_Step *step = data;
  int start_idx, start_group_idx;
  int group_idx;
  int *idx_cp;
  int failed;
  File_Group *group;
  const char *filename;
  Elm_Object_Item *item;
  Config_Data *config = NULL;
  
  if (verbose)
    printf("non-chancellation delay: %f\n", bench_delay_get(delay_cur));
  
  assert(!worker);
  assert(files);
  
  if (!tagfiles_count(files))
    return;
  
  //FIXME free stuff!
  preload_flush();
  
  if (step) {
    tagfiles_step(files, step->step);
    if (step->step)
      last_file_step = step->step;
  }
  
  //FIXME uncoditional reset only for memleak testing (still leaks!)
  step_image_config_reset_range(files, 0, tagfiles_count(files));
  
  if (last_file_step == 1 || last_file_step == -1)
    step_image_config_reset_range(files, tagfiles_idx(files)-last_file_step*PRELOAD_CONFIG_RANGE, tagfiles_idx(files)-last_file_step*(PRELOAD_CONFIG_RANGE+1));
  
  del_filter_settings();  
  
  forbid_fill++;
  
  start_idx = tagfiles_idx(files);
  
  group = tagfiles_get(files);
  
  while (!config || config->failed) {
    if (!step)
      group_idx = 0;
    else {
      group_idx = step->group_idx;
      free(step);
      step = NULL;
    }
    start_group_idx = group_idx;
    
    if (group_in_filters(group, tags_filter)) {
      while(!config || config->failed) {
          
	  config = config_data_get(group, group_idx);
	  
	  if (!config || config->failed) {
	    //FIXME del filters?
	    //free(config);
	    //config = NULL;
	    printf("failed to find valid configuration for %s\n", tagged_file_name(filegroup_nth(group, group_idx)));
	    group_idx = (group_idx + 1) % filegroup_count(group);
            if (group_idx == start_group_idx)
              break;
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
      //FIXME recheck config(s) on filegroup changed!
      if (step) free(step);
      return;
    }
  }
  
  if (config_curr) {
    //FIXME free ->filter_chain
    fc_del_gui(config_curr->filter_chain);
    config_curr->filter_chain = NULL;
    //config_curr->filters = NULL;
    //config_curr = NULL;
  }
  config_curr = config;
  {
    char *cam = NULL;
    printf("\n\nexif infos!\n");
    config_exif_infos(config_curr, &cam, NULL);
    if (cam)
      printf("camera: %s\n", cam);
  }
  //create gui only if necessary (tab selected) or if filter_chain is needed?
  fc_gui_from_config(config_curr);
  //FIXME free filter list
  //config_curr->filters = NULL;
  //FIXME get all filter handling from actual filter chain
  //filegroup_data_attach(group, group_idx, NULL);
  
  cur_group = group;
  cur_group_idx = group_idx;
  cur_file = filegroup_nth(cur_group, group_idx);
  delgrid();
    
  //we start as early as possible with rendering!
  forbid_fill--;
  if (quick_preview_only)
    first_preview = 1;
  
  if (verbose)
    printf("configuration delay: %f\n", bench_delay_get(delay_cur)); 
  fill_scroller();
  
  //refresh currently selected tab 
  void (*refresh)(void);
  refresh = evas_object_data_get(tab_current, "limeview,main_tab,refresh_cb");
  if (refresh)
    refresh();
  
  elm_slider_value_set(file_slider, tagfiles_idx(files)+0.1);
  
  if (step) free(step);
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
	filename = tagged_file_name(filegroup_nth(group, j));
	if (filename && (!export->extensions || (strstr(filename, export->extensions) && strlen(strstr(filename, export->extensions)) == strlen (export->extensions) ))) {
	  if (!export->list)
	    export->list = eina_array_new(32);
	  job = malloc(sizeof(Export_Job));
	  job->filename = filename;
	  job->filterchain = tagged_file_filterchain(filegroup_nth(group, j));
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
  Dim *size = config_size(config_curr);
  
  
  if (fit != 1) {
    fit = 1;
    
    if (!size)
      return;
    
    elm_scroller_region_get(scroller, &x, &y, &w, &h);
    
    evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
    
    fit = 1;
    

    if (!w || !h) {
      scale_goal = 1.0;
    }
    else {
      scale_goal = (float)size->width / w;	
      if ((float)size->height / h > scale_goal)
	scale_goal = (float)size->height / h;
    }
    
    new_scaledown = 1.0/config_actual_scale_get(config_curr)*scale_goal;
    
    trans = elm_transit_add();
    elm_transit_object_add(trans, grid);
    elm_transit_effect_add(trans, &_trans_grid_zoom_trans_cb, _trans_grid_zoom_contex_new(grid_w, grid_h, size->width/scale_goal,size->height/scale_goal,x+w/2,y+h/2,x/new_scaledown+w/(new_scaledown*2),y/new_scaledown+h/(new_scaledown*2),w,h), &_trans_grid_zoom_contex_del);
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
  Dim *size = config_size(config_curr);
  
  if (scale_goal != 1.0) {
    fit = 0;
    
    if (!size)
      return;
    
    elm_scroller_region_get(scroller, &x, &y, &w, &h);
    
    evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
    
    scale_goal = 1.0;
    new_scaledown = 1.0/config_actual_scale_get(config_curr)*scale_goal;
    
    trans = elm_transit_add();
    elm_transit_object_add(trans, grid);
    elm_transit_effect_add(trans, &_trans_grid_zoom_trans_cb, _trans_grid_zoom_contex_new(grid_w, grid_h, size->width/scale_goal,size->height/scale_goal,x+w/2,y+h/2,x/new_scaledown+w/(new_scaledown*2),y/new_scaledown+h/(new_scaledown*2),w,h), &_trans_grid_zoom_contex_del);
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
    if (mat_cache_old && !worker)
      _display_preview(NULL);
    //doesnt matter if true or false
    workerfinish_schedule(&step_image_do, tagfiles_step_new(1,0), NULL, EINA_TRUE);
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
    if (mat_cache_old && !worker)
      _display_preview(NULL);
    //FIXME doesnt matter if true or false
    workerfinish_schedule(&step_image_do, tagfiles_step_new(-1,0), NULL, EINA_TRUE);
  }
}

void zoom_in_do(void)
{
  Elm_Transit *trans;
  int x,y,w,h;
  int grid_w, grid_h;
  float new_scaledown;
  Dim *size = config_size(config_curr);
  
  if (scale_goal > 0.25) {
    fit = 0;
    
    if (!size)
      return;
    
    elm_scroller_region_get(scroller, &x, &y, &w, &h);
    
    evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
    
    
    scale_goal /= zoom_fac;
    if (scale_goal < 0.25)
      scale_goal = 0.25;
    
    new_scaledown = 1.0/config_actual_scale_get(config_curr)*scale_goal;
    
    trans = elm_transit_add();
    elm_transit_object_add(trans, grid);
    elm_transit_effect_add(trans, &_trans_grid_zoom_trans_cb, _trans_grid_zoom_contex_new(grid_w, grid_h, size->width/scale_goal,size->height/scale_goal,x+w/2,y+h/2,x/new_scaledown+w/(new_scaledown*2),y/new_scaledown+h/(new_scaledown*2),w,h), &_trans_grid_zoom_contex_del);
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
  Dim *size;
  
  sprintf(buf, "scanned %d files in %d dirs", tagfiles_scanned_files(tagfiles), tagfiles_scanned_dirs(tagfiles));
  
  elm_object_text_set(load_label, buf);
  
  if (files_new) {
    if (files)
      tagfiles_del(files);
    if (cur_group)
      cur_group = NULL;
    files = files_new;
    files_new = NULL;
  
    elm_slider_min_max_set(file_slider, 0, tagfiles_count(files)-1);
    evas_object_smart_callback_add(file_slider, "changed", &on_jump_image, NULL);
    elm_slider_value_set(file_slider, tagfiles_idx(files));
    evas_object_size_hint_weight_set(file_slider, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(file_slider, EVAS_HINT_FILL, 0);
    elm_slider_unit_format_set(file_slider, "%.0f");
    evas_object_show(file_slider);
    
    if (!gridbox) {
      size = config_size(config_curr);
      gridbox = elm_box_add(win);
      elm_object_content_set(scroller, gridbox);
      evas_object_size_hint_weight_set(gridbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
      evas_object_size_hint_align_set(gridbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
      evas_object_show(gridbox);
      
      grid = elm_grid_add(win);
      clipper = evas_object_rectangle_add(evas_object_evas_get(win));
      if (size) {
	elm_grid_pack(grid, clipper, 0, 0, size->width, size->height);
	evas_object_size_hint_min_set(grid,  size->width, size->height);
      }
      else {
	elm_grid_pack(grid, clipper, 0, 0, 200, 200);
	evas_object_size_hint_min_set(grid,  200, 200);
      }
      elm_box_recalculate(gridbox);
      elm_box_pack_start(gridbox, grid);
      evas_object_show(grid);
    }
    
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
  Dim *size;
  
  evas_object_del(load_notify);
  
  if (idle_progress_print) {
    ecore_idler_del(idle_progress_print);
    idle_progress_print = NULL;
  }
  
  if (files_new) {
    if (files)
      tagfiles_del(files);
    if (cur_group)
      cur_group = NULL;
    files = files_new;
    files_new = NULL;
  
    elm_slider_min_max_set(file_slider, 0, tagfiles_count(files)-1);
    evas_object_smart_callback_add(file_slider, "changed", &on_jump_image, NULL);
    elm_slider_value_set(file_slider, tagfiles_idx(files));
    evas_object_size_hint_weight_set(file_slider, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(file_slider, EVAS_HINT_FILL, 0);
    elm_slider_unit_format_set(file_slider, "%.0f");
    evas_object_show(file_slider);
    
    if (!gridbox) {
      size = config_size(config_curr);
      
      gridbox = elm_box_add(win);
      elm_object_content_set(scroller, gridbox);
      evas_object_size_hint_weight_set(gridbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
      evas_object_size_hint_align_set(gridbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
      evas_object_show(gridbox);
      
      grid = elm_grid_add(win);
      clipper = evas_object_rectangle_add(evas_object_evas_get(win));
      if (size) {
	elm_grid_pack(grid, clipper, 0, 0, size->width, size->height);
	evas_object_size_hint_min_set(grid,  size->width, size->height);
      }
      else {
	elm_grid_pack(grid, clipper, 0, 0, 200, 200);
	evas_object_size_hint_min_set(grid,  200, 200);
      }
      elm_box_recalculate(gridbox);
      elm_box_pack_start(gridbox, grid);
      evas_object_show(grid);
    }
    
    //elm_genlist_clear(tags_filter_list);
    //eina_hash_foreach(known_tags, tags_hash_filter_func, NULL);
    
    
    //grid_setsize();
    
    evas_object_show(scroller);
  }
  else
    elm_slider_min_max_set(file_slider, 0, tagfiles_count(files)-1);
  
  if (!cur_group)
    step_image_do(NULL, NULL);
}

void on_open_path(char *path)
{ 
  if (path) {
    printf("open path %s\n", path);
    
    if (files_new) {
      tagfiles_del(files_new);
      files_new = NULL;
    }
    
    printf("FIXME fileselector button?! set path?\n");
    //elm_fileselector_button_path_set(fsb, path);
    load_notify = elm_notify_add(win);
    load_label = elm_label_add(load_notify);
    elm_object_text_set(load_label, "found 0 files groups");
    elm_object_content_set(load_notify, load_label);
    evas_object_show(load_label);
    evas_object_show(load_notify);
    files_new = tagfiles_new_from_dir(path, _ls_progress_cb, _ls_done_cb, on_known_tags_changed);
  }
}

static void on_open(void *data, Evas_Object *obj, void *event_info)
{
  on_open_path((char*)event_info);
}

static void on_save_filterchain(void *data, Evas_Object *obj, void *event_info)
{
  FILE *f;
  char *fc;
  
  if (!event_info)
    return;
  fc = lime_filter_chain_serialize(((Filter_Chain*)eina_list_data_get(eina_list_next(config_curr->filter_chain)))->f);
  
  if (!fc)
    return;
  
  printf("save filterchain\n%s\nto %s\n", fc, (char*)event_info);
  f = fopen((char*)event_info, "w");
  fprintf(f, "#lime filter chain serialization version 0\n");
  fprintf(f, "%s\n", fc);
  
  free(fc);
}


void zoom_out_do(void)
{
  Elm_Transit *trans;
  int x,y,w,h;
  int grid_w, grid_h;
  float new_scaledown;
  Dim *size = config_size(config_curr);
  
  if (!size)
    return;
  
  elm_scroller_region_get(scroller, &x, &y, &w, &h);
  
  evas_object_size_hint_min_get(grid, &grid_w, &grid_h);
  
  fit = 0;
  
  scale_goal *= zoom_fac;
  
  new_scaledown = 1.0/config_actual_scale_get(config_curr)*scale_goal;
  
  trans = elm_transit_add();
  elm_transit_object_add(trans, grid);
  elm_transit_effect_add(trans, &_trans_grid_zoom_trans_cb, _trans_grid_zoom_contex_new(grid_w, grid_h, size->width/scale_goal,size->height/scale_goal,x+w/2,y+h/2,x/new_scaledown+w/(new_scaledown*2),y/new_scaledown+h/(new_scaledown*2),w,h), &_trans_grid_zoom_contex_del);
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
  workerfinish_schedule(&insert_before_do, NULL, NULL, EINA_TRUE);
}

static void on_insert_after(void *data, Evas_Object *obj, void *event_info)
{
  workerfinish_schedule(&insert_after_do, NULL, NULL, EINA_TRUE);
}

void refresh_group_tab(void) 
{
  int i;
  int *idx_cp;
  Elm_Object_Item *item;
  
  if (!cur_group || tab_current != tab_group)
    return;
  
  printf("refresh group tab!\n");
  
  elm_list_clear(group_list);
  for(i=0;i<filegroup_count(cur_group);i++)
    if (tagged_file_name(filegroup_nth(cur_group, i))) {
      //FIXME free on refresh!
      idx_cp = malloc(sizeof(int));
      *idx_cp = i;
      item = elm_list_item_append(group_list, tagged_file_name(filegroup_nth(cur_group, i)), NULL, NULL, &on_group_select, idx_cp);
      if (cur_group_idx == i) {
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

Evas_Object *elm_button_add_pack_data(Evas_Object *p, const char *text, void (*cb)(void *data, Evas_Object *obj, void *event_info), void *data)
{
  Evas_Object *btn = elm_button_add(p);
  evas_object_size_hint_weight_set(btn, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(btn, EVAS_HINT_FILL, 0);
  elm_object_text_set(btn, text);
  elm_box_pack_end(p, btn);
  evas_object_show(btn);
  evas_object_smart_callback_add(btn, "clicked", cb, data);
  
  return btn;
}

Evas_Object *elm_list_add_pack(Evas_Object *p)
{
  Evas_Object *list = elm_list_add(p);
  evas_object_size_hint_weight_set(list, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(list, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_box_pack_end(p, list);
  evas_object_show(list);
  
  return list;
}

Evas_Object *elm_vbox_add_pack(Evas_Object *p)
{
  Evas_Object *box = elm_box_add(p);
  evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_box_pack_end(p, box);
  evas_object_show(box);
  
  return box;
}

Evas_Object *elm_vbox_add_content(Evas_Object *p)
{
  Evas_Object *box = elm_box_add(p);
  evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
  elm_object_content_set(p, box);
  evas_object_show(box);
  
  return box;
}

Evas_Object *elm_label_add_pack(Evas_Object *p, char *text)
{
  Evas_Object *lbl = elm_label_add(p);
  evas_object_size_hint_weight_set(lbl, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(lbl, EVAS_HINT_FILL, 0);
  elm_object_text_set(lbl, text);
  elm_box_pack_end(p, lbl);
  evas_object_show(lbl);
  
  return lbl;
}


Evas_Object *elm_frame_add_pack(Evas_Object *p, const char *text)
{
  Evas_Object *frame = elm_frame_add(p);
  evas_object_size_hint_weight_set(frame, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(frame, EVAS_HINT_FILL, EVAS_HINT_FILL);
  if (text)
    elm_object_text_set(frame, text);
  elm_box_pack_end(p, frame);
  evas_object_show(frame);
  
  return frame;
}

Evas_Object *elm_button_add_pack(Evas_Object *p, const char *text, void (*cb)(void *data, Evas_Object *obj, void *event_info))
{  
  return elm_button_add_pack_data(p, text, cb, NULL);
}

typedef struct {
  char *path;
  char *title;
  int show_hidden;
  int is_save;
  void (*done_cb)(void *data, Evas_Object *obj, void *event_info);
  Evas_Object *fs, *win;
} file_selection_options;

file_selection_options *new_file_selection_options(char *title, void (*done_cb)(void *data, Evas_Object *obj, void *event_info))
{
  file_selection_options *opts = calloc(sizeof(file_selection_options), 1);
  
  opts->title = title;
  opts->done_cb = done_cb;
  
  return opts;
}

void on_done_fs(void *data, Evas_Object *obj, void *event_info)
{
  char *path;
  file_selection_options *opts = data;
  
  path = (char*)event_info;
  
  evas_object_del(opts->win);
  
  if (path)
    opts->done_cb(opts, NULL, path);
}

void show_new_fs_win(void *data, Evas_Object *obj, void *event_info)
{
  file_selection_options *opts = data;
  Evas_Object *bg;
  
  opts->win = elm_win_add(NULL, "fileselector_custom", ELM_WIN_DIALOG_BASIC);
  elm_win_title_set(opts->win, opts->title);
  elm_win_autodel_set(opts->win, EINA_TRUE);
  
  bg = elm_bg_add(opts->win);
  elm_win_resize_object_add(opts->win, bg);
  evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_show(bg);
  
  opts->fs = elm_fileselector_add(opts->win);
  elm_fileselector_is_save_set(opts->fs, opts->is_save);
  elm_fileselector_hidden_visible_set(opts->fs, opts->show_hidden);
  if (opts->path)
    elm_fileselector_selected_set(opts->fs, opts->path);
  evas_object_size_hint_weight_set(opts->fs, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(opts->fs, EVAS_HINT_FILL, EVAS_HINT_FILL);
  evas_object_smart_callback_add(opts->fs, "done", on_done_fs, opts);
  evas_object_show(opts->fs);
   
   elm_win_resize_object_add(opts->win, opts->fs);
   evas_object_show(opts->win);
}

Evas_Object *elm_fsb_add_pack(Evas_Object *p, const char *text, void (*cb)(void *data, Evas_Object *obj, void *event_info), char *path, int is_save, int show_hidden)
{
  file_selection_options *opts = new_file_selection_options(text, cb);
  Evas_Object *btn = elm_button_add_pack_data(p, text, &show_new_fs_win, opts);
  
  opts->path = path;
  opts->is_save = is_save;
  opts->show_hidden = show_hidden;
  
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


void fc_gui_from_config(Config_Data *config)
{
  Filter *f, *last;
  Eina_List *list_iter;
  Filter_Chain *fc;
  
  assert(config);
  
  f = config->load;
  while(f) {
    //filter chain
    fc = fc_new(f);
    config_curr->filter_chain = eina_list_append(config_curr->filter_chain, fc);
    
    //create gui, but not for first and last filters
    if (f != config->load && f != config->sink)
      fc->item = elm_list_item_append(filter_list, f->fc->name, NULL, NULL, &_on_filter_select, eina_list_last(config_curr->filter_chain));
    
    last = f;
    f = filter_chain_next_filter(f);
  }
  elm_list_go(filter_list);
}

void print_help(void)
{
  printf("usage: limeview - scale invariant image editor/viewer\n");
  printf("limeview [options] [inputfile/dir]\n");
  printf("options:\n");
  printf("   --help,           -h  show this help\n");
  printf("   --cache-size,     -s  set cache size in megabytes (default: 100)\n");
  printf("   --cache-metric,   -m  set cache cache metric (lru/dist/time/hits), \n                         can be repeated for a combined metric (default: lru)\n");
  printf("   --cache-strategy, -f  set cache strategy (rand/rapx/prob, default rapx)\n");
//  printf("   --bench,          -b  execute benchmark (global/pan/evaluate/redo/s0/s1/s2/s3)\n                         to off-screen buffer, prints resulting stats\n");
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

void on_cache_size_set(void *data, Evas_Object *obj, void *event_info)
{ 
  settings->cache_size = elm_spinner_value_get(obj);
  lime_cache_set(settings->cache_size, 0);
  lv_setting_save(settings);
}

void on_add_rule(void *data, Evas_Object *obj, void *event_info)
{ 
  Default_Fc_Rule *rule;
  char *buf;
  
  if (!cur_file)
    return;
  
  rule = calloc(sizeof(Default_Fc_Rule), 1);
  rule->cam = cam_cur;
  rule->format = format_cur;
  rule->fc = tagged_file_filterchain(cur_file);
  
  settings->default_fc_rules = eina_list_append(settings->default_fc_rules, rule);
  printf("added rule [%s][%s]->[%s]\n", rule->cam, rule->format, rule->fc);
  buf = malloc(1024);
  snprintf(buf, 1024, "%s/%s->%s", rule->cam, rule->format, rule->fc);
  printf("wtf %s\n", buf);
  elm_list_item_append(settings_rule_list, buf, NULL, NULL, NULL, rule);
  elm_list_go(settings_rule_list);
  printf("FIXME invalidate (all?) cached configs!\n");
  
  lv_setting_save(settings);
  reset_all_configs(files);
}

void on_del_rule(void *data, Evas_Object *obj, void *event_info)
{ 
  Elm_Object_Item *it;
  Default_Fc_Rule *rule;
  
  it = elm_list_selected_item_get(settings_rule_list);
  
  assert(it);
  
  evas_object_hide(settings_rule_del_btn);
  
  rule = elm_object_item_data_get(it);
  settings->default_fc_rules = eina_list_remove(settings->default_fc_rules, rule);
  elm_object_item_del(it);
  
  free(rule);
  
  lv_setting_save(settings);
  reset_all_configs(files);
}

void on_default_rule_selected(void *data, Evas_Object *obj, void *event_info)
{ 
  evas_object_show(settings_rule_del_btn);
}

void on_default_rule_unselected(void *data, Evas_Object *obj, void *event_info)
{ 
  evas_object_hide(settings_rule_del_btn);
}

Evas_Object *settings_box_add(Evas_Object *parent)
{
  Evas_Object *box, *frame, *spinner_hq, *spinner_mr, *inbox, *lbl, *spinner_scale, *vbox, *sp;
  
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
  elm_spinner_min_max_set (spinner_hq, 0, 0);
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
  elm_spinner_min_max_set (spinner_mr, 0, 0);
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
  
  elm_label_add_pack(inbox, "cache size in MiB");
  sp = elm_spinner_add(parent);
  evas_object_size_hint_weight_set(sp, EVAS_HINT_EXPAND, 0);
  evas_object_size_hint_align_set(sp, EVAS_HINT_FILL, 0);
  elm_spinner_min_max_set(sp, 25, 1000);
  elm_spinner_step_set (sp, 1);
  elm_spinner_round_set(sp, 1);
  elm_spinner_value_set (sp, settings->cache_size);
  evas_object_smart_callback_add(sp, "delay,changed", on_cache_size_set, NULL);
  elm_box_pack_end(inbox, sp);
  evas_object_show(sp);
  
  char *cam = NULL, *lens = NULL;  
  frame = elm_frame_add_pack(box, "default filter chains");
  
  vbox = elm_vbox_add_content(frame);
  //FIXME update!
  config_exif_infos(config_curr, &cam, &lens);
  elm_label_add_pack(vbox, "use current filterchain as default for");
  settings_rule_cam_lbl =  elm_label_add_pack(vbox, "camera: -");
  //elm_label_add_pack(vbox, "lens:");
  settings_rule_format_lbl = elm_label_add_pack(vbox, "file format: -");
  elm_button_add_pack(vbox, "add rule", &on_add_rule);
  settings_rule_list = elm_list_add_pack(vbox);
  evas_object_smart_callback_add(settings_rule_list, "selected", &on_default_rule_selected, NULL);
  evas_object_smart_callback_add(settings_rule_list, "unselected", &on_default_rule_selected, NULL);
  
  char *buf;
  Eina_List *l;
  Default_Fc_Rule *rule;
  EINA_LIST_FOREACH(settings->default_fc_rules, l, rule) {
    printf("adding %s %s %s\n", rule->cam, rule->format, rule->fc);
    buf = malloc(1024);
    snprintf(buf, 1024, "%s/%s->%s", rule->cam, rule->format, rule->fc);
    printf("wtf %s\n", buf);
    elm_list_item_append(settings_rule_list, buf, NULL, NULL, NULL, rule);
  }
  elm_list_go(settings_rule_list);
  settings_rule_del_btn = elm_button_add_pack(vbox, "remove rule", &on_del_rule);
  evas_object_hide(settings_rule_del_btn);
  
  evas_object_data_set(box, "limeview,main_tab,refresh_cb", &refresh_tab_setings);
  
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
  
  //assert(config_curr->filterchain);
  
  //we don't need this on tag changed right?
  //filegroup_filterchain_set(cur_group, lime_filter_chain_serialize(((Filter_Chain*)eina_list_data_get(eina_list_next(config_curr->filter_chain)))->f));
  
  filegroup_save_sidecars(cur_group);
  
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
  int cache_strategy, cache_metric;
  char *path;
  Evas_Object *hbox, *frame, *hpane, *seg_filter_rating, *entry, *btn;
  Eina_List *filters = NULL;
  select_filter_func = NULL;
  int winsize;
  preload_array = eina_array_new(64);
  
  delay_cur = malloc(sizeof(struct timespec));
  bench_delay_start(delay_cur);
  
  labelbuf = malloc(1024);
  bench = NULL;
  scale_goal = 1.0;
  
  ecore_init();
  eio_init();
  elm_init(argc, argv);
  efreet_trash_init();
  
  tagfiles_init();
  lv_settings_init();
  
  settings = lv_setting_load();
  
  pthread_mutex_init(&barrier_lock, NULL);
  
  elm_config_scroll_thumbscroll_enabled_set(EINA_TRUE);
  
  max_workers = ecore_thread_max_get();
  ecore_thread_max_set(max_workers*EXTRA_THREADING_FACTOR);
  max_preload_workers = max_workers*(EXTRA_THREADING_FACTOR-1);
  if (PRELOAD_EXTRA_WORKERS < max_preload_workers)
    max_preload_workers = PRELOAD_EXTRA_WORKERS;
  max_thread_id = max_workers+PRELOAD_CONFIG_RANGE+100;
  
  lime_init();
  eina_log_abort_on_critical_set(EINA_TRUE);
  
  mat_cache = mat_cache_new();
  delgrid();
  
  thread_ids = calloc(sizeof(int)*(max_thread_id+1), 1);
  
  if (parse_cli(argc, argv, &filters, &bench, NULL, &cache_metric, &cache_strategy, &path, &winsize, &verbose, &help))
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
  

  lime_cache_set(settings->cache_size, cache_strategy | cache_metric);
  //print_init_info(bench, settings->cache_size, cache_metric, cache_strategy, path);
  
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
  
  fsb = elm_fsb_add_pack(hbox, "open", &on_open, dir, EINA_FALSE, EINA_FALSE);
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
  elm_object_tree_focus_allow_set(group_list, EINA_FALSE);
  evas_object_size_hint_weight_set(group_list, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
  evas_object_size_hint_align_set(group_list, EVAS_HINT_FILL, EVAS_HINT_FILL);

  tab_group = group_list;
  evas_object_data_set(tab_group, "limeview,main_tab,refresh_cb", &refresh_group_tab);
  
  tab_tags = elm_box_add(win);
  evas_object_data_set(tab_tags, "limeview,main_tab,refresh_cb", &refresh_tab_tags);
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
  btn = elm_fsb_add_pack(tab_filter, "save filter chain", &on_save_filterchain, NULL, EINA_TRUE, EINA_TRUE);
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
  elm_hoversel_item_add(select_filter, "simple rotate", NULL, ELM_ICON_NONE, &on_select_filter_select, &filter_core_simplerotate);
  elm_hoversel_item_add(select_filter, "rotate", NULL, ELM_ICON_NONE, &on_select_filter_select, &filter_core_rotate);
  elm_hoversel_item_add(select_filter, "curves", NULL, ELM_ICON_NONE, &on_select_filter_select, &filter_core_curves);
  elm_hoversel_item_add(select_filter, "deconv", NULL, ELM_ICON_NONE, &on_select_filter_select, &filter_core_lrdeconv);
  elm_hoversel_item_add(select_filter, "lensfun", NULL, ELM_ICON_NONE, &on_select_filter_select, &filter_core_lensfun);

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
   
  on_open_path(path);
  
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
  
  //bench_time_mark(BENCHMARK_INIT);
  elm_run();
  //bench_time_mark(BENCHMARK_PROCESSING);
  
  while (ecore_thread_active_get())
    ecore_main_loop_iterate();
  
  if (mat_cache)
    mat_cache_del(mat_cache);
  if (mat_cache_old)
    mat_cache_del(mat_cache_old);
  
  if (verbose)
    cache_stats_print();
  if (bench)
    bench_report();
  lime_cache_flush();
  lime_shutdown();
  
  lv_settings_shutdown();
  tagfiles_shutdown();
  
  efreet_trash_shutdown();
  eio_shutdown();
  elm_shutdown();
  ecore_shutdown();
  
  return 0;
}
ELM_MAIN()