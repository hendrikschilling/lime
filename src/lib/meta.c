#include "meta.h"

int Cmp_Int(void *a, void *b)
{
  if (*(int*)a != *(int*)b)
    return -1;
  
  return 0;
}

int Cmp_True(void *a, void *b)
{
  return 0;
}

int meta_print_int(char *buf, void *val)
{
  return sprintf(buf, "%d", *(int*)val);
}

int meta_print_float(char *buf, void *val)
{
  return sprintf(buf, "%f", *(float*)val);
}

int meta_print_str(char *buf, void *val)
{
  return sprintf(buf, "%s", (char*)val);
}

int meta_print_intptr(char *buf, void *val)
{
  return sprintf(buf, "%ld", (intptr_t)val);
}

int meta_print_imgdim(char *buf, void *val)
{
  return sprintf(buf, "%ux%u %ux%u", ((Dim*)val)->x, ((Dim*)val)->y, ((Dim*)val)->width, ((Dim*)val)->height);
}


//every meta_spec whose data may be non-NULL, has to have either print_f or data
Meta_Spec meta_def_list[MT_MAX] = 
{
  {"MT_CHANNEL",
    &meta_print_intptr,
    &Cmp_True,
    {}	},
  {"MT_BUNDLE",
    NULL,
    NULL,
    {}	},
  {"MT_BITDEPTH",
    NULL,
    &Cmp_Int,
    {"BD_U8","BD_U16"}},
  {"MT_COLOR",
    NULL,
    &Cmp_Int,
    {"CS_LAB", "CS_RGB", "CS_YUV", "CS_HSV", "CS_LAB_L", "CS_LAB_A", "CS_LAB_B", "CS_RGB_R", "CS_RGB_G", "CS_RGB_B", "CS_YUV_Y", "CS_YUV_U", "CS_YUV_V", "CS_HSV_H", "CS_HSV_S", "CS_HSV_V"}},
  {"MT_LOADIMG",
    &meta_print_str,
    NULL,
    {}},
  {"MT_FLOAT",
    &meta_print_float,
    NULL,
    {}},
  {"MT_STRING",
    &meta_print_str,
    NULL,
    {}},
  {"MT_INT",
    &meta_print_int,
    &Cmp_Int,
    {}},
  {"MT_IMGSIZE",
    &meta_print_imgdim,
    NULL,
    {}},
  {"MT_INT",
    &meta_print_int,
    &Cmp_Int,
    {}}
};

int mt_data_snprint(char *buf, int len, Meta_Type t, void *data)
{
  //FIXME what with selects?
  if (meta_def_list[t].print_f) {
    return meta_def_list[t].print_f(buf, data);
  }
    return 0;
}

void pushint(Eina_Array *ar, int val)
{
  int *v;
  v = malloc(sizeof(int));
  
  *v = val;
  
  eina_array_push(ar, v);
}

Meta *meta_new(int type, Filter *filter)
{
  Meta *meta = calloc(sizeof(Meta), 1);
    
  assert(type != MT_CHANNEL);
  
  meta->type = type;
  meta->filter = filter;
  
  return meta;
}

Meta *meta_new_data(int type, Filter *filter, void *data)
{
  Meta *meta = calloc(sizeof(Meta), 1);
  
  assert(type != MT_CHANNEL);
  
  meta->type = type;
  meta->filter = filter;
  meta->data = data;

  return meta;
}

Meta *meta_new_channel(Filter *filter, int idx)
{
  Meta *meta = calloc(sizeof(Meta), 1);
  
  meta->type = MT_CHANNEL;
  meta->filter = filter;
  meta->data = (void*)(intptr_t)idx;

  return meta;
}

void meta_del(Meta *m)
{
  free(m);
}


Meta *meta_new_select(int type, Filter *filter, Eina_Array *select)
{
  Meta *meta = calloc(sizeof(Meta), 1);
  
  assert(type != MT_CHANNEL);
  
  meta->type = type;
  meta->filter = filter;
  meta->select = select;
  
  return meta;
}

void meta_name_set(Meta *meta, const char *name)
{
  meta->name = name;
}

void meta_attach(Meta *parent, Meta *child)
{
  if (!parent->childs)
    parent->childs = meta_array_new();
  
  //meta_array_append(child->parents, parent);
  meta_array_append(parent->childs, child);
}

void meta_print(Meta *m)
{
  int i;
  
  printf("Filter:%s Type:%s ", m->filter->fc->name, meta_def_list[m->type].name);
  
  char str[1024];
  if (m->data) {
    if (meta_def_list[m->type].print_f)
      meta_def_list[m->type].print_f(str, m->data);
    else
      sprintf(str, "%s", meta_def_list[m->type].data[*(int *)m->data]);
    
    printf("Data:%s ", str);
  }
  {
    if (m->select) {
      printf("  {  ");
    if (meta_def_list[m->type].print_f)
      for(i=0;i<ea_count(m->select);i++) {
	meta_def_list[m->type].print_f(str, ea_data(m->select, i));
	printf("%s ", str);
      }
    else
      for(i=0;i<ea_count(m->select);i++) {
	sprintf(str, "%s ", meta_def_list[m->type].data[*(int *)ea_data(m->select, i)]);
	printf("%s", str);
      }
      printf("  }");
    }
    
      
  }
}

void meta_data_calc(Meta *m)
{
  if (m->meta_data_calc_cb)
    m->meta_data_calc_cb(m->dep, m);
  else
    m->data = m->dep->data;
}

char *mt_type_str(Meta_Type t)
{
  return meta_def_list[t].name;
}

char *mt_data_str(Meta_Type t, void *data)
{
  char *str;
  
  if (meta_def_list[t].print_f) {
    //FIXME free/no malloc, how much?!!
    str = malloc(1024);
    meta_def_list[t].print_f(str, data);
    
    return str;
  }
  
  return meta_def_list[t].data[*(int *)data];
}


void vizp_meta(FILE *file, Meta *meta)
{
  int i;
  Meta *sub;
  char *str;
  
  fprintf(file, "\"%p\" [label = \"{<type>%s ",
	  meta, mt_type_str(meta->type));
  
  if (meta->name)
    fprintf(file, "| %s ", meta->name);
  
  if (meta->dep)
    fprintf(file, "| <dep>dep ");
  
  if (meta->data) {
    str = mt_data_str(meta->type, meta->data);
    fprintf(file, "| %s", str);
  }
  if (meta->select) {
    if (eina_array_data_get(meta->select, 0))
      str = mt_data_str(meta->type, eina_array_data_get(meta->select, 0));
    else
      str = "NULL";
    fprintf(file, "| <data>\\{%s", str);
    int i;
    for(i=1;i<ea_count(meta->select);i++) {
      if (eina_array_data_get(meta->select, i))
	str = mt_data_str(meta->type, eina_array_data_get(meta->select, i));
      else
	str = "NULL";
      fprintf(file,", %s", str);
    }
    fprintf(file, "\\}");
  }
  
  fprintf(file, "}\"]\n");
  
  if (meta->childs)
    for(i=0;i<meta->childs->count;i++) {
      sub = meta->childs->data[i];
      fprintf(file, "\"%p\":type ->\"%p\"\n", meta, sub);
      vizp_meta(file, sub);
    }
    
    if (meta->dep)
	fprintf(file, "\"%p\":dep ->\"%p\"\n", meta, meta->dep);
      
}

void vizp_ar(FILE *file, Eina_Array *ar, Filter *parent, char *label)
{
  int i;
  Meta *sub;
  
  if (!ar || !ea_count(ar))
    return;
  
  for(i=0;i<ea_count(ar);i++) {
    sub = eina_array_data_get(ar, i);
    fprintf(file, "\"%p\":%s -> \"%p\"\n", parent, label,  sub);
    vizp_meta(file, sub);
  }
  
}

void *meta_child_data_by_type(Meta *m, int type)
{
  int i;
  
  if (!m || !m->childs || !ma_count(m->childs))
    return NULL;
  
  for(i=0;i<ma_count(m->childs);i++)
    if (ma_data(m->childs, i)->type == type)
      return ma_data(m->childs, i)->data;
    
  return NULL;
}
