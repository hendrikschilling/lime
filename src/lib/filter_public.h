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

#ifndef _LIME_FILTER_PUBLIC_H
#define _LIME_FILTER_PUBLIC_H

struct _Filter;
typedef struct _Filter Filter;

struct _Fg_Node;
typedef struct _Fg_Node Fg_Node;

struct _Filter_Mode_Buffer;
typedef struct _Filter_Mode_Buffer Filter_Mode_Buffer;

struct _Filter_Mode_Iter;
typedef struct _Filter_Mode_Iter Filter_Mode_Iter;

struct _Filter_Iter;
typedef struct _Filter_Iter Filter_Iter;

struct _Con;
typedef struct _Con Con;

struct _Hash;
typedef struct _Hash Hash;

struct _Tilehash;
typedef struct _Tilehash Tilehash;

struct _Filter_Core;
typedef struct _Filter_Core Filter_Core;

#include "meta.h"
#include "configuration.h"

typedef int (*Filter_F)(Filter *f);
typedef void (*Prepare_F)(Filter *f, int scale);
typedef void (*Worker_Basic_F)(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id);
typedef void (*Area_Calc_F)(Filter *f, Rect *in, Rect *out);
typedef void *(*Filter_Data_F)(Filter *f, void *data);

struct _Hash {
  int len;
  char data[1024];
  uint32_t hash;
  uint32_t prevhash;
};

struct _Tilehash {
  Hash *filterhash;
  Rect area;
  int tilehash;
};

struct _Con
{
  Meta *source;
  Meta *sink;
};

struct _Fg_Node
{
  Filter *f;
  //one con for every input/output tree
  /*
   * high-level: con_trees_* give the connected trees
   */
  Eina_Array *con_trees_in;
  Eina_Array *con_trees_out;
  /*
   * low-level for actual rendering
   * con_ch_* give the actually connected channels and their connection can differ from the con_trees
   */
  Eina_Array *con_ch_in;
};

//clobbering
struct _Filter_Mode_Buffer
{
  Worker_Basic_F worker;
  Area_Calc_F area_calc;
  Filter_Data_F data_new;
  int threadsafe;
};

//self iterating
struct _Filter_Mode_Iter
{
  void (*finish)(Filter *f);
  void (*worker)(Filter *f, void *in, int channel, Eina_Array *out, Rect *area, int thread_id);
  void *(*iter_new)(Filter *f, Rect *area, Eina_Array *f_source, Pos *pos, int *channel);
  void (*iter_worker)(void *iter);
  void (*iter_next)(void *data, Pos *pos, int *channel);
  int (*iter_eoi)(void *data, Pos pos, int channel);
  void (*iter_del)(void *iter);
  Filter_Data_F data_new;
  int threadsafe;
};

struct _Filter
{
  Filter_Core *fc;
  pthread_mutex_t lock;
  Config *c;
  Eina_Array *in;
  Eina_Array *out;
  Eina_Array *core;
  Eina_Array *tune;
  Eina_Array *settings;
  Eina_Array *metas; //too free on filter del
  Fg_Node *node;
  Fg_Node *node_orig;
  Filter_F setting_changed; //settings were changed, calc input-meta
  Filter_F input_fixed; //input meta and settings are fixed, calc out-meta
  Filter_F tunes_fixed; //tunings are fixed, prepare filtering
  Filter_F del; //tunings are fixed, prepare filtering
  Eina_Array *data;
  Filter_Mode_Buffer *mode_buffer;
  Filter_Mode_Iter *mode_iter;
  int fixme_outcount;
  Hash hash;
  //pthread_mutex_t *lock;
  int tile_width;
  int tile_height;
  int *th_s;
  int *tw_s;
};

Tilehash tile_hash_calc(Filter *f, Rect *area);
int lime_setting_float_set(Filter *f, const char *setting, float value);
Fg_Node *fg_node_new(Filter *f);
char *lime_filter_chain_serialize(Filter *f);
Con *filter_connect(Filter *source, int out, Filter *sink, int in);
void filter_hash_recalc(Filter *f);
Hash *filter_hash_get(Filter *f);
Filter *filter_get_input_filter(Filter *f,  int channel);
int lime_setting_int_set(Filter *f, const char *setting, int value);
Con *filter_connect_real(Filter *source, int out, Filter *sink, int in);
void con_del_real(Con *con);
void vizp_filter(FILE *file, Filter *filter);
void filter_fill_thread_data(Filter *f, int thread_id);
void *filter_core_by_name(Filter *f, const char *name);
void *filter_core_by_type(Filter *f, int type);
void *filter_core_by_subtype(Filter *f, int type, char *subtype);
Filter_Mode_Buffer *filter_mode_buffer_new(void);
int lime_setting_string_set(Filter *f, const char *setting, const char *value);
int tw_get(Filter *f, int scale);
int th_get(Filter *f, int scale);
Filter_Mode_Iter *filter_mode_iter_new(void);
void lime_filter_connect(Filter *source, Filter *sink);
int lime_setting_type_get(Filter *f, const char *setting);

#endif