#include "filter_assert.h"

typedef struct {
  Meta *dim_in_meta;
  Dim *out_dim;
  Meta *color_in[3], *color_out[3];
  int in_cs, out_cs;
  int scale;
} _Data;

static int _setting_changed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  
  switch (data->in_cs) {
    case 0 : 
      *(int*)(data->color_in[0]->data) = CS_RGB_R;
      *(int*)(data->color_in[1]->data) = CS_RGB_G;
      *(int*)(data->color_in[2]->data) = CS_RGB_B;
      break;
    case 1 : 
      *(int*)(data->color_in[0]->data) = CS_LAB_L;
      *(int*)(data->color_in[1]->data) = CS_LAB_A;
      *(int*)(data->color_in[2]->data) = CS_LAB_B;
      break;
    case 2 : 
      *(int*)(data->color_in[0]->data) = CS_YUV_Y;
      *(int*)(data->color_in[1]->data) = CS_YUV_U;
      *(int*)(data->color_in[2]->data) = CS_YUV_V;
      break;
    case 3 : 
      *(int*)(data->color_in[0]->data) = CS_HSV_H;
      *(int*)(data->color_in[1]->data) = CS_HSV_S;
      *(int*)(data->color_in[2]->data) = CS_HSV_V;
      break;
    default :
      abort();    
  }
  
  switch (data->out_cs) {
    case 0 : 
      *(int*)(data->color_out[0]->data) = CS_RGB_R;
      *(int*)(data->color_out[1]->data) = CS_RGB_G;
      *(int*)(data->color_out[2]->data) = CS_RGB_B;
      break;
    case 1 : 
      *(int*)(data->color_out[0]->data) = CS_LAB_L;
      *(int*)(data->color_out[1]->data) = CS_LAB_A;
      *(int*)(data->color_out[2]->data) = CS_LAB_B;
      break;
    case 2 : 
      *(int*)(data->color_out[0]->data) = CS_YUV_Y;
      *(int*)(data->color_out[1]->data) = CS_YUV_U;
      *(int*)(data->color_out[2]->data) = CS_YUV_V;
      break;
    case 3 : 
      *(int*)(data->color_out[0]->data) = CS_HSV_H;
      *(int*)(data->color_out[1]->data) = CS_HSV_S;
      *(int*)(data->color_out[2]->data) = CS_HSV_V;
      break;
    default :
      abort();    
  }
  
  return 0;
}

static int _input_fixed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  Dim *in_dim = data->dim_in_meta->data;
  
  assert(!in_dim->x);
  assert(!in_dim->y);
  
  data->out_dim->x = 0;
  data->out_dim->y = 0;
  data->out_dim->width = in_dim->width / (1u << data->scale);
  data->out_dim->height = in_dim->height / (1u << data->scale);
  data->out_dim->scaledown_max = in_dim->scaledown_max - data->scale;
  
  return 0;
}

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int ch;
  Tiledata *in_td, *out_td;
  
  for(ch=0;ch<3;ch++) {
    in_td = (Tiledata*)ea_data(in, ch);
    out_td = (Tiledata*)ea_data(out, ch);
    memcpy(out_td->data, in_td->data, out_td->area->width*out_td->area->height);
  }
}

static void _area_calc(Filter *f, Rect *in, Rect *out)
{
  _Data *data = ea_data(f->data, 0);
  
  out->corner.scale = in->corner.scale + data->scale;
  out->corner.x = in->corner.x;
  out->corner.y = in->corner.y;
  out->width = in->width; 
  out->height = in->height;

}

Filter *filter_assert_new(void)
{
  Filter *filter = filter_new(&filter_core_assert);
  Meta *in, *out, *channel, *bitdepth, *setting, *bound, *size_in, *size_out;
  Meta *ch_out[3];
  filter->fixme_outcount = 3;
  _Data *data = calloc(sizeof(_Data), 1);
  data->out_cs = 1;
  data->out_dim = calloc(sizeof(Dim), 1);
  ea_push(filter->data, data);
  filter->setting_changed = &_setting_changed;
  filter->input_fixed = &_input_fixed;
  
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = &_worker;
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->area_calc = &_area_calc;
  
  size_in = meta_new(MT_IMGSIZE, filter);
  size_out = meta_new_data(MT_IMGSIZE, filter, data->out_dim);
  data->dim_in_meta = size_in;
  size_in->replace = size_out;
  
  bitdepth = meta_new_data(MT_BITDEPTH, filter, malloc(sizeof(int)));
  *(int*)(bitdepth->data) = BD_U8;
  bitdepth->replace = bitdepth;
  
  out = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->out, out);
  
  channel = meta_new_channel(filter, 1);
  data->color_out[0] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color_out[0]->data) = CS_LAB_L;
  meta_attach(channel, data->color_out[0]);
  meta_attach(channel, size_out);
  meta_attach(channel, bitdepth);
  meta_attach(out, channel);
  ch_out[0] = channel;
  
  channel = meta_new_channel(filter, 2);
  data->color_out[1] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color_out[1]->data) = CS_LAB_A;
  meta_attach(channel, data->color_out[1]);
  meta_attach(channel, size_out);
  meta_attach(channel, bitdepth);
  meta_attach(out, channel);
  ch_out[1] = channel;
  
  channel = meta_new_channel(filter, 3);
  data->color_out[2] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color_out[2]->data) = CS_LAB_B;
  meta_attach(channel, data->color_out[2]);
  meta_attach(channel, size_out);
  meta_attach(channel, bitdepth);
  meta_attach(out, channel);
  ch_out[2] = channel;
  
  in = meta_new(MT_BUNDLE, filter);
  in->replace = out;
  eina_array_push(filter->in, in);
  
  channel = meta_new_channel(filter, 1);
  data->color_in[0] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color_in[0]->data) = CS_RGB_R;
  data->color_in[0]->replace = data->color_out[0];
  channel->replace = ch_out[0];
  meta_attach(channel, data->color_in[0]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size_in);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 2);
  data->color_in[1] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color_in[1]->data) = CS_RGB_G;
  data->color_in[1]->replace = data->color_out[1];
  channel->replace = ch_out[1];
  meta_attach(channel, data->color_in[1]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size_in);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 3);
  data->color_in[2] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(data->color_in[2]->data) = CS_RGB_B;
  data->color_in[2]->replace = data->color_out[2];
  channel->replace = ch_out[2];
  meta_attach(channel, data->color_in[2]);
  meta_attach(channel, bitdepth);
  meta_attach(channel, size_in);
  meta_attach(in, channel);
  
  setting = meta_new_data(MT_INT, filter, &data->in_cs);
  meta_name_set(setting, "input colorspace");
  eina_array_push(filter->settings, setting);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 3;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  setting = meta_new_data(MT_INT, filter, &data->out_cs);
  meta_name_set(setting, "output colorspace");
  eina_array_push(filter->settings, setting);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 0;
  meta_name_set(bound, "PARENT_SETTING_MIN");
  meta_attach(setting, bound);
  
  bound = meta_new_data(MT_INT, filter, malloc(sizeof(int)));
  *(int*)bound->data = 3;
  meta_name_set(bound, "PARENT_SETTING_MAX");
  meta_attach(setting, bound);
  
  setting = meta_new_data(MT_INT, filter, &data->scale);
  meta_name_set(setting, "scale"); 
  eina_array_push(filter->settings, setting);
  
  return filter;
}

Filter_Core filter_core_assert = {
  "Assert colorspace",
  "assert",
  "states output color space without actual conversion from the input color space",
  &filter_assert_new
};