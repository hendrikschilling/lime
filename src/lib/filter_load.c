#include "filter_load.h"

typedef struct {
  Meta *m_file;
  Meta *m_out;
} _Data;

int _load_input_fixed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  
  data->m_out->data = data->m_file->data;

  return 0;
}

Filter *filter_load_new(void)
{
  Filter *filter = filter_new(&filter_core_load);
  _Data *data = calloc(sizeof(_Data), 1);
  ea_push(filter->data, data);
  
  filter->input_fixed = &_load_input_fixed;
  
  Meta *out, *setting;
  
  out = meta_new(MT_LOADIMG, filter);
  eina_array_push(filter->out, out);
  
  setting = meta_new(MT_STRING, filter);
  meta_name_set(setting, "filename");
  eina_array_push(filter->settings, setting); 
  
  data->m_file = setting;
  data->m_out = out;
  
  return filter;
}

Filter_Core filter_core_load = {
  "Load file",
  "load",
  "loads image from a file",
  &filter_load_new
};