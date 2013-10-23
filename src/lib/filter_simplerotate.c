#include "filter_simplerotate.h"

typedef struct {
  Meta *dim_in_meta;
  Dim *out_dim;
  int rotation;
} _Data;

int _rot_lr_input_fixed(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  Dim *in_dim = data->dim_in_meta->data;
  
  if (abs(data->rotation) % 2 == 1) {
    data->out_dim->x = 0;
    data->out_dim->y = 0;
    data->out_dim->width = in_dim->height;
    data->out_dim->height = in_dim->width;
    data->out_dim->scaledown_max = in_dim->scaledown_max;
  }
  
  return 0;
}

static void _area_calc(Filter *f, Rect *in, Rect *out)
{
  int div;
  int x_orig, y_orig;
  _Data *data = ea_data(f->data, 0);
  
  x_orig = in->corner.x * div;
  y_orig = in->corner.y * div;
  
  if (data->rotation % 4 == 3) {
    out->corner.scale = in->corner.scale;
    if (!in->corner.scale) {
      out->corner.x = (data->out_dim->height >> in->corner.scale) - in->corner.y-DEFAULT_TILE_SIZE;
      out->corner.y = in->corner.x;
    }
    else {
      out->corner.x = (data->out_dim->height + (1u << in->corner.scale - 1) >> in->corner.scale) - in->corner.y-DEFAULT_TILE_SIZE;
      out->corner.y = in->corner.x;
    }

    out->width = in->height; //for interpolation
    out->height = in->width;
  }
  else if (data->rotation % 4 == 1) {
    out->corner.scale = in->corner.scale;
    if (!in->corner.scale) {
      out->corner.x = in->corner.y;
      out->corner.y = (data->out_dim->width >> in->corner.scale) - in->corner.x-DEFAULT_TILE_SIZE;
    }
    else {
      out->corner.x = in->corner.y;
      out->corner.y = (data->out_dim->width + (1u << in->corner.scale - 1) >> in->corner.scale) - in->corner.x-DEFAULT_TILE_SIZE;
    }
    out->width = in->height; //for interpolation
    out->height = in->width;
  }
  else abort();
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
  uint8_t *buf_in;
  
  assert(in && ea_count(in) == 3);
  assert(out && ea_count(out) == 3);

  in_area = ((Tiledata*)ea_data(in, 0))->area;
    
  for(ch=0;ch<3;ch++) {
    in_td = (Tiledata*)ea_data(in, ch);
    out_td = (Tiledata*)ea_data(out, ch);
    
    if (data->rotation % 4 == 1)
      for(j=0;j<DEFAULT_TILE_SIZE;j++) {
        buf_in = tileptr8(in_td, in_td->area->corner.x, in_td->area->corner.y+j);
        buf_out = tileptr8(out_td, out_td->area->corner.x+DEFAULT_TILE_SIZE-j-1, out_td->area->corner.y);
        for(i=0;i<DEFAULT_TILE_SIZE;i++) {
          *buf_out = *buf_in;
          buf_in++;
          buf_out += DEFAULT_TILE_SIZE;
        }
      }
    else if (data->rotation % 4 == 3)
      for(j=0;j<DEFAULT_TILE_SIZE;j++) {
        buf_in = tileptr8(in_td, in_td->area->corner.x, in_td->area->corner.y+j);
        buf_out = tileptr8(out_td, out_td->area->corner.x+j, out_td->area->corner.y+DEFAULT_TILE_SIZE-1);
        for(i=0;i<DEFAULT_TILE_SIZE;i++) {
          *buf_out = *buf_in;
          buf_in++;
          buf_out -= DEFAULT_TILE_SIZE;
        }
      }
      /*
      for(i=0;i<DEFAULT_TILE_SIZE;i++)
        *tileptr8(out_td, out_td->area->corner.x+j, out_td->area->corner.y+i)
          = *tileptr8(in_td, in_td->area->corner.x+i, in_td->area->corner.y+j);*/
  }

}


Filter *filter_simplerotate_new(void)
{
  Filter *filter = filter_new(&filter_core_simplerotate);
  Meta *in, *out, *channel, *color[3], *size_in, *size_out, *setting;
  Meta *ch_out[3];
  _Data *data = calloc(sizeof(_Data), 1);
  data->out_dim = calloc(sizeof(Dim), 1);
  data->rotation = 3;

  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->worker = &_worker;
  filter->mode_buffer->area_calc = &_area_calc;
  filter->fixme_outcount = 3;
  filter->input_fixed = &_rot_lr_input_fixed;
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
  
  setting = meta_new_data(MT_INT, filter, &data->rotation);
  meta_name_set(setting, "rotation"); 
  eina_array_push(filter->settings, setting);
  
  return filter;
}

Filter_Core filter_core_simplerotate = {
  "simple rotate",
  "rotate",
  "rotates image in multiples of 90 degrees",
  &filter_simplerotate_new
};