#ifndef HAVE_META_H
#define HAVE_META_H

struct _Meta;
typedef struct _Meta Meta;

#include "common.h"
#include "meta_array.h"
#include "filter.h"

#define MT_ENUM_MAX 16 

typedef struct _Meta_Spec Meta_Spec;

typedef int (*Meta_Data_Cmp_F)(void *a, void *b);

typedef int (*Meta_Print_F)(char *buf, void *val);

struct _Meta_Spec
{
  char *name;
  Meta_Print_F print_f;
  Meta_Data_Cmp_F cmp_data;
  char *data[MT_ENUM_MAX];
};

typedef enum {
  BD_U8=0,
  BD_U16
} Bitdepth;

typedef enum {
  CS_LAB=0,
  CS_RGB,
  CS_YUV,
  CS_HSV,
  CS_LAB_L,
  CS_LAB_A,
  CS_LAB_B,
  CS_RGB_R,
  CS_RGB_G,
  CS_RGB_B,
  CS_YUV_Y,
  CS_YUV_U,
  CS_YUV_V,
  CS_HSV_H,
  CS_HSV_S,
  CS_HSV_V,
  CS_INT_RGB,
  CS_INT_ARGB
} Colorspace;

typedef enum {
 MT_CHANNEL=0,
 MT_BUNDLE,
 MT_BITDEPTH,
 MT_COLOR,
 MT_LOADIMG,
 MT_FLOAT,
 MT_STRING,
 MT_INT,
 MT_IMGSIZE,
 MT_FLIPROT,
 MT_MAX
} Meta_Type;

extern Meta_Spec meta_def_list[MT_MAX];

typedef void (*Meta_Data_Calc_F)(Meta *tune, Meta *m);

struct _Meta
{
   int type;
   const char *name;
   Meta *dep; //type: Meta, tuning this meta depends on
   Meta *replace; //the node that appears in the output instead of this input-node
   Meta_Array *childs; //type: Meta
   Eina_Array *select;
   void *data;
   Meta_Data_Calc_F meta_data_calc_cb;
   //Meta_Array *parents;
   Filter *filter;
   //TODO calc func
};

void meta_del(Meta *m);
Meta *meta_new(int type, Filter *filter);
Meta *meta_new_data(int type, Filter *filter, void *data);
Meta *meta_new_channel(Filter *filter, int idx);
Meta *meta_new_select(int type, Filter *filter, Eina_Array *select);
void meta_name_set(Meta *meta, const char *name);
void meta_attach(Meta *parent, Meta *child);
void meta_print(Meta *m);
void meta_data_calc(Meta *m);
char *mt_type_str(Meta_Type t);
char *mt_data_str(Meta_Type t, void *data);
int mt_data_snprint(char *buf, int len, Meta_Type t, void *data);
void vizp_ar(FILE *file, Eina_Array *ar, Filter *parent, char *label);
void vizp_meta(FILE *file, Meta *meta);
void *meta_child_data_by_type(Meta *m, int type);
void pushint(Eina_Array *ar, int val);

#endif