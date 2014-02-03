#include "filter_interleave.h"

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int i, j;
  uint8_t *buf, *r, *g, *b;
  
  assert(in && ea_count(in) == 3);
  
  r = ((Tiledata*)ea_data(in, 0))->data;
  g = ((Tiledata*)ea_data(in, 1))->data;
  b = ((Tiledata*)ea_data(in, 2))->data;
  hack_tiledata_fixsize(4, ea_data(out, 0));
  buf = ((Tiledata*)ea_data(out, 0))->data;
  
  area = ((Tiledata*)ea_data(in, 0))->area;
  
  for(j=0;j<area->height;j++)
    for(i=0;i<area->width;i++) {
      buf[(j*area->width+i)*4+0] = b[j*area->width+i];
      buf[(j*area->width+i)*4+1] = g[j*area->width+i];
      buf[(j*area->width+i)*4+2] = r[j*area->width+i];
    }
}


Filter *filter_interleave_new(void)
{
  Filter *filter = filter_new(&filter_core_interleave);
  Meta *in, *out, *channel, *color, *col_out, *bitdepth;
  Meta *ch_out;

  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->worker = &_worker;
  //filter->mode_buffer->area_calc = &_area_calc;
  filter->fixme_outcount = 1;
  
  bitdepth = meta_new_data(MT_BITDEPTH, filter, malloc(sizeof(int)));
  *(int*)(bitdepth->data) = BD_U8;
  
  out = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->out, out);
  
  ch_out = meta_new_channel(filter, 1);
  col_out = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(col_out->data) = CS_INT_ARGB;
  meta_attach(ch_out, col_out);
  meta_attach(ch_out, bitdepth);
  meta_attach(out, ch_out);

  in = meta_new(MT_BUNDLE, filter);
  in->replace = out;
  eina_array_push(filter->in, in);
  
  channel = meta_new_channel(filter, 1);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_R;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(in, channel);
  channel->replace = ch_out;
  color->replace = col_out;
  
  channel = meta_new_channel(filter, 2);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_G;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(in, channel);
  channel->replace = ch_out;
  color->replace = col_out;
  
  channel = meta_new_channel(filter, 3);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_B;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(in, channel);
  channel->replace = ch_out;
  color->replace = col_out;
  
  return filter;
}

Filter_Core filter_core_interleave = {
  "interleave",
  "interleave",
  "converts from planar to packed/interleaved format",
  &filter_interleave_new
};