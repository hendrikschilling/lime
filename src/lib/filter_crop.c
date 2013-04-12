#include "filter_crop.h"

typedef struct {
  Meta *dim_in_meta;
  Dim *out_dim;
  float cx,cy,cw,ch;
  int cx_abs, cy_abs;
} _Data;

int _crop_input_fixed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  Dim *in_dim = data->dim_in_meta->data;
  
  data->cx_abs = in_dim->x + data->cx * in_dim->width * 0.01;
  data->cy_abs = in_dim->y + data->cy * in_dim->height * 0.01;
  
  data->out_dim->x = 0;
  data->out_dim->y = 0;
  data->out_dim->width = (in_dim->width-data->cx_abs)*data->cw * 0.01;
  data->out_dim->height = (in_dim->height-data->cy_abs)*data->ch * 0.01;
  data->out_dim->scaledown_max = in_dim->scaledown_max;
  
  return 0;
}

static void _area_calc(Filter *f, Rect *in, Rect *out)
{
  int div;
  int x_orig, y_orig;
  _Data *data = ea_data(f->data, 0);
  
  div = 1u << in->corner.scale;
  
  x_orig = in->corner.x * div;
  y_orig = in->corner.y * div;
  
  //if (x_orig % div || y_orig % div) {
    out->corner.scale = in->corner.scale;
    out->corner.x = (x_orig+data->cx_abs)/div;
    out->corner.y = (y_orig+data->cy_abs)/div;
    out->width = in->width+1; //for interpolation
    out->height = in->height+1;
  /*}
  else {
    out->corner.scale = in->corner.scale;
    out->corner.x = in->corner.x+data->out_dim.x;
    out->corner.y = in->corner.y+data->out_dim.y;
    out->width = in->width;
    out->height = in->height;
  }*/
}

static uint8_t *tileptr8(Tiledata *tile, int x, int y)
{ 
  return &((uint8_t*)tile->data)[(y-tile->area->corner.y)*tile->area->width + x-tile->area->corner.x];
}

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  int div;
  int x_orig, y_orig;
  int ch;
  int i, j;
  uint32_t xsub, ysub, xsub_inv, ysub_inv;
  Tiledata *in_td, *out_td;
  Rect *in_area;
  _Data *data = ea_data(f->data, 0);
  uint8_t *buf_out;
  uint8_t *buf_in1, *buf_in2;
  
  assert(in && ea_count(in) == 3);
  assert(out && ea_count(out) == 3);

  in_area = ((Tiledata*)ea_data(in, 0))->area;
  
  div = 1u << in_area->corner.scale;
  
  x_orig = in_area->corner.x * div;
  y_orig = in_area->corner.y * div;
  
  xsub = ((x_orig+data->cx_abs) % div) * 1024/div;
  ysub = ((y_orig+data->cy_abs) % div) * 1024/div;
  xsub_inv = 1024-xsub;
  ysub_inv = 1024-ysub;
    
  for(ch=0;ch<3;ch++) {
    in_td = (Tiledata*)ea_data(in, ch);
    out_td = (Tiledata*)ea_data(out, ch);
    
    //TODO just memcpy (or pass-through/inplace) if subs = 0
    for(j=0;j<DEFAULT_TILE_SIZE;j++) {
      buf_out = tileptr8(out_td, area->corner.x, j+area->corner.y);
      buf_in1 = tileptr8(in_td, in_area->corner.x, j+in_area->corner.y);
      //buf_in[1] = tileptr8(in_td, 1+in_area->corner.x, j+in_area->corner.y);
      buf_in2 = tileptr8(in_td, in_area->corner.x, 1+j+in_area->corner.y);
      //buf_in[3] = tileptr8(in_td, 1+in_area->corner.x, 1+j+in_area->corner.y);
      for(i=0;i<DEFAULT_TILE_SIZE;i++) {
	*buf_out = 
	  ((buf_in1[0]*xsub_inv+buf_in1[1]*xsub)*ysub_inv
	  +(buf_in2[0]*xsub_inv+buf_in2[1]*xsub)*ysub
	  +(1024*1024)/2
	  ) / (1024*1024);
	buf_out++;
	buf_in1++;
	buf_in2++;
    }
  }
  }

}


Filter *filter_crop_new(void)
{
  Filter *filter = filter_new(&filter_core_crop);
  Meta *in, *out, *channel, *color[3], *size_in, *size_out, *setting;
  Meta *ch_out[3];
  _Data *data = calloc(sizeof(_Data), 1);
  data->out_dim = calloc(sizeof(Dim), 1);
  data->cx = 10.0;
  data->cy = 10.0;
  data->cw = 100.0;
  data->ch = 100.0;

  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->worker = &_worker;
  filter->mode_buffer->area_calc = &_area_calc;
  filter->fixme_outcount = 3;
  filter->input_fixed = &_crop_input_fixed;
  ea_push(filter->data, data);
  
  size_in = meta_new(MT_IMGSIZE, filter);
  size_out = meta_new_data(MT_IMGSIZE, filter, data->out_dim);
  data->dim_in_meta = size_in;
  size_in->replace = size_out;
  
  out = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->out, out);
  
  channel = meta_new_channel(filter, 1);
  color[0] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[0]->data) = CS_RGB_R;
  meta_attach(channel, color[0]);
  meta_attach(channel, size_out);
  meta_attach(out, channel);
  ch_out[0] = channel;
  
  channel = meta_new_channel(filter, 2);
  color[1] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[1]->data) = CS_RGB_G;
  meta_attach(channel, color[1]);
  meta_attach(channel, size_out);
  meta_attach(out, channel);
  ch_out[1] = channel;
  
  channel = meta_new_channel(filter, 3);
  color[2] = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color[2]->data) = CS_RGB_B;
  meta_attach(channel, color[2]);
  meta_attach(channel, size_out);
  meta_attach(out, channel);
  ch_out[2] = channel;
  
  in = meta_new(MT_BUNDLE, filter);
  in->replace = out;
  eina_array_push(filter->in, in);
  
  channel = meta_new_channel(filter, 1);
  color[0]->replace = color[0];
  channel->replace = ch_out[0];
  meta_attach(channel, color[0]);
  meta_attach(channel, size_in);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 2);
  color[1]->replace = color[1];
  channel->replace = ch_out[1];
  meta_attach(channel, color[1]);
  meta_attach(channel, size_in);
  meta_attach(in, channel);
  
  channel = meta_new_channel(filter, 3);
  color[2]->replace = color[2];
  color[2]->replace = color[2];
  channel->replace = ch_out[2];
  meta_attach(channel, color[2]);
  meta_attach(channel, size_in);
  meta_attach(in, channel);
  
  setting = meta_new_data(MT_FLOAT, filter, &data->cx);
  meta_name_set(setting, "crop left"); 
  eina_array_push(filter->settings, setting);
  setting = meta_new_data(MT_FLOAT, filter, &data->cy);
  meta_name_set(setting, "crop top");
  eina_array_push(filter->settings, setting);
  setting = meta_new_data(MT_FLOAT, filter, &data->cw);
  meta_name_set(setting, "crop width");
  eina_array_push(filter->settings, setting);
  setting = meta_new_data(MT_FLOAT, filter, &data->ch);
  meta_name_set(setting, "crop height");
  eina_array_push(filter->settings, setting);
  
  return filter;
}

Filter_Core filter_core_crop = {
  "crop",
  "crop",
  "crop image :-)",
  &filter_crop_new
};