#include "filter_memsink.h"

typedef struct {
  uint8_t *data;
  int *use_alpha;
} _Memsink_Data;

void *_memsink_data_new(Filter *f, void *data)
{
  _Memsink_Data *new = calloc(sizeof(_Memsink_Data), 1);
  
  new->use_alpha = ((_Memsink_Data*)data)->use_alpha;
  
  return new;
}

void filter_memsink_buffer_set(Filter *f, uint8_t *raw_data, int thread_id)
{
    _Memsink_Data *data;
    
    filter_fill_thread_data(f, thread_id);
    
    data = ea_data(f->data, thread_id);
    
    data->data = raw_data;
}

static void _memsink_worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int i, j;
  uint8_t *buf, *r, *g, *b;
  _Memsink_Data *data = ea_data(f->data, thread_id);
  buf = data->data;
  
  assert(in && ea_count(in) == 3);
  
  r = ((Tiledata*)ea_data(in, 0))->data;
  g = ((Tiledata*)ea_data(in, 1))->data;
  b = ((Tiledata*)ea_data(in, 2))->data;
  
  area = ((Tiledata*)ea_data(in, 0))->area;
  
  if (*data->use_alpha)
    for(j=0;j<area->height;j++)
      for(i=0;i<area->width;i++) {
	buf[(j*area->width+i)*4+0] = b[j*area->width+i];
	buf[(j*area->width+i)*4+1] = g[j*area->width+i];
	buf[(j*area->width+i)*4+2] = r[j*area->width+i];
      }
  else
    for(j=0;j<area->height;j++)
      for(i=0;i<area->width;i++) {
	buf[(j*area->width+i)*3+0] = r[j*area->width+i];
	buf[(j*area->width+i)*3+1] = g[j*area->width+i];
	buf[(j*area->width+i)*3+2] = b[j*area->width+i];
      }
}

Filter *filter_memsink_new(void)
{
  Filter *filter = filter_new(&filter_core_memsink);
  Meta *in, *channel, *bitdepth, *color, *size, *setting, *bound;
  _Memsink_Data *data = calloc(sizeof(_Memsink_Data), 1);
  data->use_alpha = malloc(sizeof(int));
  *data->use_alpha = 0;
  ea_push(filter->data, data);
  
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = &_memsink_worker;
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->data_new = &_memsink_data_new;
  
  bitdepth = meta_new_data(MT_BITDEPTH, filter, malloc(sizeof(int)));
  *(int*)(bitdepth->data) = BD_U8;
  
  size = meta_new(MT_IMGSIZE, filter);
  ea_push(filter->core, size);
  
  in = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->in, in);
  
  channel = meta_new_channel(filter, 1);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_R;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 2);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_G;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 3);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_B;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size);
  meta_attach(in, channel);
  
  setting = meta_new_data(MT_INT, filter, data->use_alpha);
  meta_name_set(setting, "add alpha");
  eina_array_push(filter->settings, setting);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 1;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  return filter;
}

Filter_Core filter_core_memsink = {
  "Memory sink",
  "memsink",
  "Stores input in application provided buffer",
  &filter_memsink_new
};