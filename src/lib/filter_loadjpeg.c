#include "filter_loadjpeg.h"
#include "ujpeg.h"

#include <libexif/exif-data.h>
#include <jpeglib.h>
#include <setjmp.h>

#define JPEG_TILE_SIZE 256

struct my_error_mgr {
  struct jpeg_error_mgr pub;	/* "public" fields */

  jmp_buf setjmp_buffer;	/* for return to caller */
};

typedef struct my_error_mgr *my_error_ptr;

METHODDEF(void)
my_error_exit (j_common_ptr cinfo)
{
  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
  my_error_ptr myerr = (my_error_ptr) cinfo->err;

  /* Return control to the setjmp point */
  longjmp(myerr->setjmp_buffer, 1);
}

typedef struct {
  Meta *input;
  Meta *dim;
  int rot;
} _Data;

void *_data_new(Filter *f, void *data)
{
  _Data *newdata = calloc(sizeof(_Data), 1);
  
  *newdata = *(_Data*)data;
  
  return newdata;
}

static int _get_exif_orientation(const char *file)
{
   int orientation = 1;
   ExifData *data;
   ExifEntry *entry;
   
   data = exif_data_new_from_file(file);
   
   if (!data)
     return orientation;
   
   entry = exif_data_get_entry(data, EXIF_TAG_ORIENTATION);
   
   if (!entry) {
    exif_data_free(data);
    return orientation;
   }
   
   orientation = *(short*)entry->data;
   
   if (orientation > 8)
     orientation /= 256;
   
   if (orientation > 8 || orientation < 1)
     orientation = 1;

   exif_data_free(data);
   return orientation;
}


void _loadjpeg_worker_ujpeg(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  _Data *data = ea_data(f->data, 0);
  
  uint8_t *r, *g, *b;
  uint8_t *rp, *gp, *bp;
  int i, j;
  int xstep, ystep;
  unsigned char *buffer;    /* Output row buffer */
  int row_stride;   /* physical row width in output buffer */
  FILE *file;
  int lines_read;
  
  ujImage uj;
  
  uj = ujDecodeFileArea(NULL, data->input->data, area->corner.x, area->corner.y, area->width, area->height);
  
  //maximum scaledown: 1/1
  assert(area->corner.scale <= 0);
  
  r = ((Tiledata*)ea_data(out, 0))->data;
  g = ((Tiledata*)ea_data(out, 1))->data;
  b = ((Tiledata*)ea_data(out, 2))->data;
  
  buffer = ujGetImageArea(uj, NULL, area->corner.x, area->corner.y, area->width, area->height);
    
  switch (data->rot) {
    case 6 : 
      rp = r + area->height - 1;
      gp = g + area->height - 1;
      bp = b + area->height - 1;
      xstep = area->height;
      ystep = -area->height*area->height - 1;
      break;
    case 8 : 
      rp = r + area->height*(area->width - 1);
      gp = g + area->height*(area->width - 1);
      bp = b + area->height*(area->width - 1);
      xstep = -area->height;
      ystep = area->height*area->width+1;
      break;
    case 1 :  
    default : //FIXME more cases!
      rp = r;
      gp = g;
      bp = b;
      xstep = 1;
      ystep = 0;
      break;
  }

    for(j=0;j<area->height;j++,rp+=ystep,gp+=ystep,bp+=ystep)
      for(i=0;i<area->width;i++,rp+=xstep,gp+=xstep,bp+=xstep) {
        rp[0] = buffer[(j*area->width+i)*3];
        gp[0] = buffer[(j*area->width+i)*3+1];
        bp[0] = buffer[(j*area->width+i)*3+2];
      }
      
  ujDestroy(uj);
}

void _loadjpeg_worker_ijg(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  _Data *data = ea_data(f->data, 0);
  
  uint8_t *r, *g, *b;
  uint8_t *rp, *gp, *bp;
  int i, j;
  int xstep, ystep;
  JSAMPARRAY buffer;		/* Output row buffer */
  int row_stride;		/* physical row width in output buffer */
  FILE *file;
  int lines_read;
  
  struct jpeg_decompress_struct cinfo;
  struct my_error_mgr jerr;
  
  assert(out && ea_count(out) == 3);
  
  //maximum scaledown: 1/8
  assert(area->corner.scale <= 3);
  
  if (area->corner.x || area->corner.y) {
    printf("FIXME: invalid tile requested in loadjpg: %dx%d\n", area->corner.x, area->corner.y);
    return;
  }
  
  r = ((Tiledata*)ea_data(out, 0))->data;
  g = ((Tiledata*)ea_data(out, 1))->data;
  b = ((Tiledata*)ea_data(out, 2))->data;
  
  file = fopen(data->input->data, "rb");
    
  if (!file)
    abort();
  
  cinfo.err = jpeg_std_error(&jerr.pub);
  //jerr.pub.error_exit = my_error_exit;
  
  if (setjmp(jerr.setjmp_buffer)) {
    jpeg_destroy_decompress(&cinfo);
    fclose(file);
    abort();
  }
  jpeg_create_decompress(&cinfo);

  jpeg_stdio_src(&cinfo, file);

  (void) jpeg_read_header(&cinfo, TRUE);

  cinfo.scale_num = 1;
  cinfo.scale_denom = 1u << area->corner.scale;
  //cinfo.out_color_space = cinfo.jpeg_color_space;
  
  /*cinfo.dct_method = JDCT_IFAST;
  cinfo.do_fancy_upsampling = FALSE;*/
  jpeg_start_decompress(&cinfo);
   
  printf("restart every %d mcus, mcus per row: %d \n", cinfo.restart_interval, cinfo.MCUs_per_row);
  
  row_stride = cinfo.output_width * cinfo.output_components;
  buffer = (*cinfo.mem->alloc_sarray)
  ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 16);
  
  switch (data->rot) {
    case 6 : 
      rp = r + cinfo.output_height - 1;
      gp = g + cinfo.output_height - 1;
      bp = b + cinfo.output_height - 1;
      xstep = cinfo.output_height;
      ystep = -cinfo.output_height*cinfo.output_width - 1;
      break;
    case 8 : 
      rp = r + cinfo.output_height*(cinfo.output_width - 1);
      gp = g + cinfo.output_height*(cinfo.output_width - 1);
      bp = b + cinfo.output_height*(cinfo.output_width - 1);
      xstep = -cinfo.output_height;
      ystep = cinfo.output_height*cinfo.output_width+1;
      break;
    case 1 :  
    default : //FIXME more cases!
      rp = r;
      gp = g;
      bp = b;
      xstep = 1;
      ystep = 0;
      break;
  }

  while (cinfo.output_scanline < cinfo.output_height) {
    lines_read = jpeg_read_scanlines(&cinfo, buffer, 16);
    for(j=0;j<lines_read;j++,rp+=ystep,gp+=ystep,bp+=ystep)
      for(i=0;i<cinfo.output_width;i++,rp+=xstep,gp+=xstep,bp+=xstep) {
        rp[0] = buffer[j][i*3];
        gp[0] = buffer[j][i*3+1];
        bp[0] = buffer[j][i*3+2];
      }
  }
  
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  
  fclose(file);
}

void _loadjpeg_worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  if (!area->corner.scale)
    _loadjpeg_worker_ujpeg(f, in, out, area, thread_id);
  else
    _loadjpeg_worker_ijg(f, in, out, area, thread_id);

}

int _loadjpeg_input_fixed(Filter *f)
{
  int i;
  _Data *data = ea_data(f->data, 0);
  struct jpeg_decompress_struct cinfo;
  struct my_error_mgr jerr;
  FILE *file;
  
  file = fopen(data->input->data, "r");
  
  if (!file)
    return -1;
  
  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = my_error_exit;

  if (setjmp(jerr.setjmp_buffer)) {
    jpeg_destroy_decompress(&cinfo);
    fclose(file);
    return -1;
  }
  
  data->rot = _get_exif_orientation(data->input->data);
  
  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, file);
  jpeg_read_header(&cinfo, TRUE);
  jpeg_calc_output_dimensions(&cinfo);

  if (data->rot <= 4) {
    ((Dim*)data->dim)->width = cinfo.output_width;
    ((Dim*)data->dim)->height = cinfo.output_height;
  }
  else {
    ((Dim*)data->dim)->width = cinfo.output_height;
    ((Dim*)data->dim)->height = cinfo.output_width;
  }
  ((Dim*)data->dim)->scaledown_max = 0;
  
  f->tw_s = malloc(sizeof(int)*4);
  f->th_s = malloc(sizeof(int)*4);

  f->tw_s[0] = JPEG_TILE_SIZE;
  f->th_s[0] = JPEG_TILE_SIZE;
  
  for(i=1;i<4;i++) {
    cinfo.scale_num = 1;
    cinfo.scale_denom = 1u << i;
    jpeg_calc_output_dimensions(&cinfo);
    if (data->rot <= 4) { 
      f->tw_s[i] = cinfo.output_width;
      f->th_s[i] = cinfo.output_height;
    }
    else {
      f->tw_s[i] = cinfo.output_height;
      f->th_s[i] = cinfo.output_width;
    }
  }
  //f->tile_width = JPEG_TILE_SIZE;
  //f->tile_height = JPEG_TILE_SIZE;
  
  jpeg_destroy_decompress(&cinfo);
  
  fclose(file);

  return 0;
}

static int _del(Filter *f)
{
  _Data *data;
  int i;
  for(i=0;i<ea_count(f->data);i++) {
    data = ea_data(f->data, i);
    free(data);
  }
  
  return 0;
}

Filter *filter_loadjpeg_new(void)
{
  Filter *filter = filter_new(&filter_core_loadjpeg);
  Meta *in, *out, *channel, *bitdepth, *color, *dim;
  _Data *data = calloc(sizeof(_Data), 1);
  data->dim = calloc(sizeof(Dim), 1);
  
  filter->del = &_del;
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = &_loadjpeg_worker;
  filter->mode_buffer->threadsafe = 1;
  //filter->mode_buffer->data_new = &_data_new;
  filter->input_fixed = &_loadjpeg_input_fixed;
  filter->fixme_outcount = 3;
  ea_push(filter->data, data);
  
  bitdepth = meta_new_data(MT_BITDEPTH, filter, malloc(sizeof(int)));
  *(int*)(bitdepth->data) = BD_U8;
  
  dim = meta_new_data(MT_IMGSIZE, filter, data->dim);
  eina_array_push(filter->core, dim);
  
  out = meta_new(MT_BUNDLE, filter);
  eina_array_push(filter->out, out);
  
  in = meta_new(MT_LOADIMG, filter);
  in->replace = out;
  eina_array_push(filter->in, in);
  data->input = in;
  
  channel = meta_new_channel(filter, 1);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_R;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, dim);
  meta_attach(out, channel);
  
  channel = meta_new_channel(filter, 2);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_G;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, dim);
  meta_attach(out, channel);
  
  channel = meta_new_channel(filter, 3);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_RGB_B;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, dim);
  meta_attach(out, channel);
  
  return filter;
}

Filter_Core filter_core_loadjpeg = {
  "JPEG loader",
  "loadjpeg",
  "Loads JPEG images from a file",
  &filter_loadjpeg_new
};
