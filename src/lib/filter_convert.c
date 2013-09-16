#include "filter_convert.h"
#include "lcms2.h"

typedef struct {
  int initialized;
  cmsHTRANSFORM transform;
} _Common;

typedef struct {
  Meta *in_color, *in_bd,
       *out_color, *out_bd;
  _Common *common;
  void *buf;
} _Data;

static void *_data_new(Filter *f, void *data)
{
  _Data *newdata = malloc(sizeof(_Data));
  
  *newdata = *(_Data*)data;
  newdata->buf = malloc(DEFAULT_TILE_AREA*3);
  
  return newdata;
}

static int _del(Filter *f)
{
  _Common *common;
  
  _Data *data;
  int i;
  for(i=0;i<ea_count(f->data);i++) {
    data = ea_data(f->data, i);
    common = data->common;
    free(data->buf);
    free(data);
  }
  
  if (common->initialized)
    cmsDeleteTransform(common->transform);
  
  free(common);
  
  return 0;
}

static void _worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  _Data *data = ea_data(f->data, thread_id);
  
  uint8_t *buf;
  
  buf = data->buf;
  
  memcpy(buf, ((Tiledata*)ea_data(in, 0))->data, DEFAULT_TILE_AREA);
  memcpy(buf+DEFAULT_TILE_AREA,  ((Tiledata*)ea_data(in, 1))->data, DEFAULT_TILE_AREA);
  memcpy(buf+2*DEFAULT_TILE_AREA,  ((Tiledata*)ea_data(in, 2))->data, DEFAULT_TILE_AREA);
  
  cmsDoTransform(data->common->transform, buf, buf, DEFAULT_TILE_AREA);
  
  memcpy(((Tiledata*)ea_data(out, 0))->data, buf, DEFAULT_TILE_AREA);
  memcpy(((Tiledata*)ea_data(out, 1))->data, buf+DEFAULT_TILE_AREA, DEFAULT_TILE_AREA);
  memcpy(((Tiledata*)ea_data(out, 2))->data, buf+2*DEFAULT_TILE_AREA, DEFAULT_TILE_AREA);
}

#ifndef TYPE_Lab_8_PLANAR
  #define TYPE_Lab_8_PLANAR             (COLORSPACE_SH(PT_Lab)|CHANNELS_SH(3)|BYTES_SH(1)|PLANAR_SH(1))
#endif

static int _tunes_fixed(Filter *f)
{
  cmsHPROFILE hInProfile, hOutProfile;
  uint32_t in_type, out_type;
  _Data *data = ea_data(f->data, 0);
  
  assert(*((Bitdepth*)data->in_bd->data) == BD_U8);
  assert(*((Bitdepth*)data->out_bd->data) == BD_U8);
  
  if (data->common->initialized)
    cmsDeleteTransform(data->common->transform);
  
  switch (*((Colorspace*)data->in_color->data)) {
    case CS_RGB :
      hInProfile = cmsCreate_sRGBProfile();
      in_type = TYPE_RGB_8_PLANAR;
      break;
    case CS_LAB : 
      hInProfile = cmsCreateLab4Profile(NULL);
      in_type = TYPE_Lab_8_PLANAR;
      break;
    default:
      printf("unhandled input color-space!\n");
      return -1;
  }
  
  switch (*((Colorspace*)data->out_color->data)) {
    case CS_RGB :
      hOutProfile = cmsCreate_sRGBProfile();
      out_type = TYPE_RGB_8_PLANAR;
      break;
    case CS_LAB : 
      hOutProfile = cmsCreateLab4Profile(NULL);
      out_type = TYPE_Lab_8_PLANAR;
      break;
    default:
      printf("unhandled output color-space!\n");
      return -1;
  }

  data->common->transform = cmsCreateTransform(hInProfile, 
					in_type, 
					hOutProfile, 
					out_type, 
					INTENT_PERCEPTUAL, 
					0);
  data->common->initialized = 1;
  
  cmsCloseProfile(hInProfile);
  cmsCloseProfile(hOutProfile);
  
  return 0;
}

static void color1_tune_calc(Meta *tune, Meta *m)
{
  
  Colorspace *data = malloc(sizeof(Colorspace));
  m->data = data;
  
  switch (*(Colorspace*)tune->data) {
    case CS_LAB :
      *data = CS_LAB_L;
      break;
    case CS_RGB :
      *data = CS_RGB_R;
      break;
    case CS_YUV :
      *data = CS_YUV_Y;
      break;
    case CS_HSV :
      *data = CS_HSV_V;
      break;
    default :
      abort();
  }
}

static void color2_tune_calc(Meta *tune, Meta *m)
{
  
  Colorspace *data = malloc(sizeof(Colorspace));
  m->data = data;
  
  switch (*(Colorspace*)tune->data) {
    case CS_LAB :
      *data = CS_LAB_A;
      break;
    case CS_RGB :
      *data = CS_RGB_G;
      break;
    case CS_YUV :
      *data = CS_YUV_U;
      break;
    case CS_HSV :
      *data = CS_HSV_H;
      break;
    default :
      abort();
  }
}

static void color3_tune_calc(Meta *tune, Meta *m)
{
  
  Colorspace *data = malloc(sizeof(Colorspace));
  m->data = data;
  
  switch (*(Colorspace*)tune->data) {
    case CS_LAB :
      *data = CS_LAB_B;
      break;
    case CS_RGB :
      *data = CS_RGB_B;
      break;
    case CS_YUV :
      *data = CS_YUV_V;
      break;
    case CS_HSV :
      *data = CS_HSV_S;
      break;
    default :
      abort();
  }
}


Filter *filter_convert_new(void)
{
  Filter *filter = filter_new(&filter_core_convert);
  Meta *in, *out, *color, *channel, *tune_in_bitdepth, *tune_out_bitdepth, *tune_in_color, *tune_out_color;
  Eina_Array *select_bitdepth, *select_color;
  filter->tunes_fixed = &_tunes_fixed;
  filter->del = &_del;
  _Data *data = calloc(sizeof(_Data), 1);
  data->common = calloc(sizeof(_Common), 1);
  data->buf = malloc(DEFAULT_TILE_AREA*3);
  ea_push(filter->data, data);
  Meta *ch_out_color[3];
  
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->data_new = &_data_new;
  filter->mode_buffer->worker = &_worker;
  filter->fixme_outcount = 3;
  
  Meta *ch_out[3];
  
  Eina_Array *color1, *color2, *color3;
  
  select_bitdepth = eina_array_new(2);
  pushint(select_bitdepth, BD_U8);
  pushint(select_bitdepth, BD_U16);
  
  select_color = eina_array_new(4);
  pushint(select_color, CS_LAB);
  pushint(select_color, CS_RGB);
  pushint(select_color, CS_YUV);
  pushint(select_color, CS_HSV);
  
  color1 = eina_array_new(4);
  pushint(color1, CS_LAB_L);
  pushint(color1, CS_RGB_R);
  pushint(color1, CS_YUV_Y);
  pushint(color1, CS_HSV_V);
  
  color2 = eina_array_new(4);
  pushint(color2, CS_LAB_A);
  pushint(color2, CS_RGB_G);
  pushint(color2, CS_YUV_U);
  pushint(color2, CS_HSV_H);
  
  color3 = eina_array_new(4);
  pushint(color3, CS_LAB_B);
  pushint(color3, CS_RGB_B);
  pushint(color3, CS_YUV_V);
  pushint(color3, CS_HSV_S);
  
  tune_out_bitdepth = meta_new_select(MT_BITDEPTH, filter, select_bitdepth);
  meta_name_set(tune_out_bitdepth, "Output Bitdepth");
  tune_out_bitdepth->dep = tune_out_bitdepth;
  data->out_bd = tune_out_bitdepth;
  tune_out_color = meta_new_select(MT_COLOR, filter, select_color);
  meta_name_set(tune_out_color, "Output CS");
  data->out_color = tune_out_color;
  tune_in_bitdepth = meta_new_select(MT_BITDEPTH, filter, select_bitdepth);
  meta_name_set(tune_in_bitdepth, "Input Bitdepth");
  tune_in_bitdepth->dep = tune_in_bitdepth;
  data->in_bd = tune_out_bitdepth;
  tune_in_bitdepth->replace = tune_out_bitdepth;
  tune_in_color = meta_new_select(MT_COLOR, filter, select_color);
  meta_name_set(tune_in_color, "Input CS");
  data->in_color = tune_out_color;
  tune_in_color->replace = tune_out_color;
  
  eina_array_push(filter->tune, tune_out_bitdepth);
  eina_array_push(filter->tune, tune_out_color);
  eina_array_push(filter->tune, tune_in_bitdepth);
  eina_array_push(filter->tune, tune_in_color);
  
  data->in_color = tune_in_color;
  data->out_color = tune_out_color;
  
  out = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->out, out);
  
  //first color channel
  channel = meta_new_channel(filter, 1);
  meta_attach(channel, tune_out_bitdepth);
  color = meta_new_select(MT_COLOR, filter, color1);
  color->dep = tune_out_color;
  color->meta_data_calc_cb = &color1_tune_calc;
  meta_attach(channel, color);
  meta_attach(out, channel);
  ch_out_color[0] = color;
  ch_out[0] = channel;
  
  //second color channel
  channel = meta_new_channel(filter, 2);
  meta_attach(channel, tune_out_bitdepth);
  color = meta_new_select(MT_COLOR, filter, color2);
  color->dep = tune_out_color;
  color->meta_data_calc_cb = &color2_tune_calc;
  meta_attach(channel, color);
  meta_attach(out, channel);
  ch_out_color[1] = color;
  ch_out[1] = channel;
  
  //third color channel
  channel = meta_new_channel(filter, 3);
  meta_attach(channel, tune_out_bitdepth);
  color = meta_new_select(MT_COLOR, filter, color3);
  color->dep = tune_out_color;
  color->meta_data_calc_cb = &color3_tune_calc;
  meta_attach(channel, color);
  meta_attach(out, channel);
  ch_out_color[2] = color;
  ch_out[2] = channel;
  
  in = meta_new(MT_BUNDLE, filter);
  in->replace = out;
  eina_array_push(filter->in, in);
  
  //first color channel
  channel = meta_new_channel(filter, 1);
  channel->replace = ch_out[0];
  meta_attach(channel, tune_in_bitdepth);
  color = meta_new_select(MT_COLOR, filter, color1);
  color->dep = tune_in_color;
  color->meta_data_calc_cb = &color1_tune_calc;
  color->replace = ch_out_color[0];
  meta_attach(channel, color);
  meta_attach(in, channel);
  //data->in_ch_color[0] = color;
  
  //second color channel
  channel = meta_new_channel(filter, 2);
  channel->replace = ch_out[1];
  meta_attach(channel, tune_in_bitdepth);
  color = meta_new_select(MT_COLOR, filter, color2);
  color->dep = tune_in_color;
  color->meta_data_calc_cb = &color2_tune_calc;
  color->replace = ch_out_color[1];
  meta_attach(channel, color);
  meta_attach(in, channel);
  //data->in_ch_color[1] = color;
  
  //third color channel
  channel = meta_new_channel(filter, 3);
  channel->replace = ch_out[2];
  meta_attach(channel, tune_in_bitdepth);
  color = meta_new_select(MT_COLOR, filter, color3);
  color->dep = tune_in_color;
  color->meta_data_calc_cb = &color3_tune_calc;
  color->replace = ch_out_color[2];
  meta_attach(channel, color);
  meta_attach(in, channel);
  //data->in_ch_color[2] = color;
  
  return filter;
}

Filter_Core filter_core_convert = {
  "Color space conversion",
  "convert",
  "converts color spaces",
  &filter_convert_new
};