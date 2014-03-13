/*
 * Copyright (C) 2014 Hendrik Siedelmann <hendrik.siedelmann@googlemail.com>
 *
 * This file is part of lime.
 * 
 * Lime is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Lime is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Lime.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "filter_loadjpeg.h"

#include <libexif/exif-data.h>
#include <jpeglib.h>
#include <setjmp.h>

#define JPEG_TILE_WIDTH 256
#define JPEG_TILE_HEIGHT 256

#include "jpeglib.h"
#include "jerror.h"
#include <libexif/exif-loader.h>


/* Expanded data source object for stdio input */

typedef struct {
  int error;
} _Common;

typedef struct {
  _Common *common;
  Meta *input;
  Meta *dim;
  int rot;
  int seekable;
  int *size_pos;
  int file_pos;
  int rst_int;
  int mcu_w, mcu_h;
  int w, h;
  int comp_count;
  int **index;
  int serve_ix;
  int serve_iy;
  int serve_minx;
  int serve_miny;
  int serve_maxx;
  int serve_maxy;
  int serve_fakejpg;
  int serve_bytes;
  int serve_width;
  int serve_height;
  int rst_next;
  int iw, ih;
#ifdef USE_UJPEG
  ujImage uimg;
#endif
  char *filename;
  pthread_mutex_t *lock;
  Meta *fliprot;
  int thumb_len;
  uint8_t *thumb_data;
} _Data;

typedef struct {
  struct jpeg_source_mgr pub; /* public fields */

  FILE * infile;    /* source stream */
  JOCTET * buffer;    /* start of buffer */
  boolean start_of_file;  /* have we gotten any data yet? */
  _Data *data;
} my_source_mgr;

typedef my_source_mgr * my_src_ptr;

#define INPUT_BUF_SIZE  4096  /* choose an efficiently fread'able size */


/*
 * Initialize source --- called by jpeg_read_header
 * before any data is actually read.
 */

#define BUF_SIZE 4096

#define ATLEAST_BUF(L)\
  if (remain < L) { \
    if (remain) memmove(buf, pos, remain); \
    pos = buf; \
    len = fread(buf+remain, 1, BUF_SIZE, f); \
    if (remain+len < L) { \
      printf("not enough data read!\n"); \
      return -1; \
    } \
    remain += len; \
    file_pos += len; \
  }
 
#define SKIP_BUF(N) \
  {\
    if (N > BUF_SIZE || N < 0) {\
      fseek(f, file_pos + N - remain, SEEK_SET); \
      file_pos += N - remain; \
      remain = 0; \
    } \
    else { \
      ATLEAST_BUF(N) \
      pos+= N; \
      remain-= N; \
    } \
  }
  
void get_exif_stuff(const char *file, uint8_t **preview, int *p_len, int *rotation)
{
  int orientation = 1;
  int len = 0;
  ExifLoader *l;
  ExifEntry *entry;
  
  /* Create an ExifLoader object to manage the EXIF loading process */
  l = exif_loader_new();
  if (l) {
    ExifData *ed;
    
    /* Load the EXIF data from the image file */
    exif_loader_write_file(l, file);
    
    /* Get a pointer to the EXIF data */
    ed = exif_loader_get_data(l);
    
    /* The loader is no longer needed--free it */
    exif_loader_unref(l);
    l = NULL;
    if (ed) {
      entry = exif_data_get_entry(ed, EXIF_TAG_ORIENTATION);
   
      if (entry) {
	
      orientation = *(short*)entry->data;
      
      if (orientation > 8)
	orientation /= 256;
      
      if (orientation > 8 || orientation < 1)
	orientation = 1;
      }
      
      *rotation = orientation;

      /* Make sure the image had a thumbnail before trying to write it */
      if (ed->data && ed->size) {
	//printf("found thumb image of size %d\n", ed->size);
	len = ed->size;
	if (!preview)
	  preview = malloc(sizeof(uint8_t*));
	else if (*preview)
	  free(*preview);
	*preview = malloc(len);
	/* Write the thumbnail image to the file */
	memcpy(*preview, ed->data, len);
      }
      /* Free the EXIF data */
      exif_data_unref(ed);
    }
  }
  
  *p_len = len;
}
  
int jpeg_read_infos(FILE *f, _Data *data)
{
  int i;
  int len;
  int last_interval;
  int curr_interval;
  int last_jump;
  int remain = 0;
  int file_pos = 0;
  int next_restart;
  unsigned char buf[2*BUF_SIZE];
  unsigned char *pos = buf;
  int *index;
  int ix, iy;
  
  fseek(f, 0, SEEK_SET);
  ATLEAST_BUF(BUF_SIZE);
  
  if ((pos[0] != 0xFF) | (pos[1] != 0xD8))
    return -1;
  
  SKIP_BUF(2)
  ATLEAST_BUF(4)
    
  while (1) {
    if (pos[0] != 0xFF) {
      return -1;
    }
    switch (pos[1]) {
      case 0xC0:
        ATLEAST_BUF(9)
       //FIXME get lengths and quit
        *data->size_pos = file_pos - remain + 5;
        if (pos[4] != 8) {
          printf("jpg Syntax error!\n");
          return -1;
        }
        len = pos[2] * 256 + pos[3];
        SKIP_BUF(len+2);
        break;
      case 0xDA:
	index = calloc(sizeof(int)*data->iw*data->ih, 1);
        //2B Marker + 2B Length + 1B comp_count (FIXME compare/use) + 2B*comp_count+ marker length
        len = pos[2] * 256 + pos[3];
        assert(pos[4] == data->comp_count);
        SKIP_BUF(len+2);
        //FIXME does the image start with the first restart marker or here?
        next_restart = 0;
        //FIXME index[0] = ...
        ATLEAST_BUF(2)
        ix = 0;
        iy = 0;
        i = 0;
        index[0] = file_pos - remain;
        last_interval = 0;
        curr_interval = 0;
        while(1) {
          if (pos[i] == 0xFF && (pos[i+1] & 0xF0) == 0xD0)
          {
            if ((pos[i+1] & 0x0F) != next_restart) {
              assert(last_jump > 0);
              SKIP_BUF(-curr_interval+1)
              //printf("back-skip %d\n", curr_interval+1);
              curr_interval = 0;
              ATLEAST_BUF(i+4)
              last_jump = 0;
              continue;
            }
            ix++;
            if (ix >= data->iw) {
              ix = 0;
              iy++;
            }
            //printf("%4dx%4d ", ix*data->mcu_w, iy*data->mcu_h);
            //we point to after the restart marker
            index[iy*data->iw + ix] = file_pos - remain + i+2; 
            //printf("found %d\n", next_restart);
            next_restart = (next_restart+1) % 8;
            
            if (iy == data->ih-1 && ix == data->iw-1) {
              break;
            }
            
            last_interval = curr_interval;
            curr_interval = 0;
            //printf("marker interval: %d\n", last_interval);
            /*SKIP_BUF(i)
            i = 0;
            ATLEAST_BUF(4)
            curr_interval = last_interval*0.5;
            last_jump = curr_interval;
            SKIP_BUF(last_jump)
            ATLEAST_BUF(4)*/
          }
          
          i++;
          curr_interval++;
          if (i == remain-1) {
            SKIP_BUF(i)
            ATLEAST_BUF(4)
            i = 0;
          }
        }
        *data->index = index;
        return 0;
      case 0xC4:
      case 0xDB:
      case 0xDD:
      case 0xFE:
      case 0xE1: //FIXME get exif info!
        len = pos[2] * 256 + pos[3];
        SKIP_BUF(len+2);
        break;
      default :
        if ((pos[1] & 0xF0) != 0xE0) {
          len = (pos[2] << 8) | pos[3];
          SKIP_BUF(len+2);
          break;
        }
        else {
        len = (pos[2] << 8) | pos[3];
        SKIP_BUF(len+2);
        break;
        }
    }
    ATLEAST_BUF(4)
  }
  
  return 0;
}

METHODDEF(void)
init_source (j_decompress_ptr cinfo)
{
  my_src_ptr src = (my_src_ptr) cinfo->src;

  /* We reset the empty-input-file flag for each image,
   * but we don't clear the input buffer.
   * This is correct behavior for reading a series of images from one source.
   */
  src->start_of_file = TRUE;
}


/*
 * Fill the input buffer --- called whenever buffer is emptied.
 *
 * In typical applications, this should read fresh data into the buffer
 * (ignoring the current state of next_input_byte & bytes_in_buffer),
 * reset the pointer & count to the start of the buffer, and return TRUE
 * indicating that the buffer has been reloaded.  It is not necessary to
 * fill the buffer entirely, only to obtain at least one more byte.
 *
 * There is no such thing as an EOF return.  If the end of the file has been
 * reached, the routine has a choice of ERREXIT() or inserting fake data into
 * the buffer.  In most cases, generating a warning message and inserting a
 * fake EOI marker is the best course of action --- this will allow the
 * decompressor to output however much of the image is there.  However,
 * the resulting error message is misleading if the real problem is an empty
 * input file, so we handle that case specially.
 *
 * In applications that need to be able to suspend compression due to input
 * not being available yet, a FALSE return indicates that no more data can be
 * obtained right now, but more may be forthcoming later.  In this situation,
 * the decompressor will return to its caller (with an indication of the
 * number of scanlines it has read, if any).  The application should resume
 * decompression after it has loaded more data into the input buffer.  Note
 * that there are substantial restrictions on the use of suspension --- see
 * the documentation.
 *
 * When suspending, the decompressor will back up to a convenient restart point
 * (typically the start of the current MCU). next_input_byte & bytes_in_buffer
 * indicate where the restart point will be if the current call returns FALSE.
 * Data beyond this point must be rescanned after resumption, so move it to
 * the front of the buffer rather than discarding it.
 */

METHODDEF(boolean)
fill_input_buffer (j_decompress_ptr cinfo)
{
  my_src_ptr src = (my_src_ptr) cinfo->src;
  size_t nbytes;
  int size;
  
  //makes sure we have the whole jpeg size memory area in one piece
  if (src->data->serve_fakejpg) {
    if (src->data->serve_iy == src->data->serve_maxy-1
        && src->data->serve_ix == src->data->serve_maxx-1)
      size = INPUT_BUF_SIZE;
    else
      size = (*src->data->index)[src->data->serve_iy*src->data->iw+src->data->serve_ix+1]-(*src->data->index)[src->data->serve_iy*src->data->iw+src->data->serve_ix];
    fseek(src->infile, (*src->data->index)[src->data->serve_iy*src->data->iw+src->data->serve_ix], SEEK_SET);
    assert(2*INPUT_BUF_SIZE >= size);
    nbytes = fread(src->buffer, 1, size, src->infile);
    //FIXME check size?
    if (size != INPUT_BUF_SIZE) {
      src->buffer[nbytes-1] = 0xd0 | src->data->rst_next;
      src->data->rst_next = (src->data->rst_next+1) % 8;
    }
    src->data->file_pos = (*src->data->index)[src->data->serve_iy*src->data->iw+src->data->serve_ix] + nbytes;
    src->data->serve_ix++;
    if (src->data->serve_ix >= src->data->serve_maxx) {
      src->data->serve_ix = src->data->serve_minx;
      src->data->serve_iy++;
      if (src->data->serve_iy == src->data->serve_maxy) {
        src->data->serve_fakejpg = 0;
        //printf("we have served the whole area!\n");
      }
    }
  }
  else if (src->data->file_pos < *src->data->size_pos 
      && src->data->file_pos + INPUT_BUF_SIZE >= *src->data->size_pos) {
    if (src->data->file_pos + 2*INPUT_BUF_SIZE >= (*src->data->index)[0])
      nbytes = fread(src->buffer, 1, (*src->data->index)[0]-src->data->file_pos, src->infile);
    else
      nbytes = fread(src->buffer, 1, 2*INPUT_BUF_SIZE, src->infile);
    //FIXME
    int i = *src->data->size_pos - src->data->file_pos;
    //printf("size on fill: %dx%d\n%x %x %x %x %x %x\n%d\n", src->buffer[i+2]*256+src->buffer[i+3],src->buffer[i]*256+src->buffer[i+1],src->buffer[i],src->buffer[i+1],src->buffer[i+2],src->buffer[i+3],src->buffer[i+4],src->buffer[i+5],src->data->size_pos);
    src->buffer[i+2] = src->data->serve_width / 256; //pretend size to be 256!
    src->buffer[i+3] = src->data->serve_width % 256;
  }
  else if (src->data->file_pos < (*src->data->index)[0]
      && src->data->file_pos + INPUT_BUF_SIZE >= (*src->data->index)[0]) {
    nbytes = fread(src->buffer, 1, (*src->data->index)[0]-src->data->file_pos, src->infile);
  }
  else if (src->data->file_pos == (*src->data->index)[0]) {
    //printf("wohoo now feed fake jpeg\n");
    src->data->serve_fakejpg = 1;
    src->data->rst_next = 0;
    size = (*src->data->index)[src->data->serve_iy*src->data->iw+src->data->serve_ix+1]-(*src->data->index)[src->data->serve_iy*src->data->iw+src->data->serve_ix];
    fseek(src->infile, (*src->data->index)[src->data->serve_iy*src->data->iw+src->data->serve_ix], SEEK_SET);
    assert(INPUT_BUF_SIZE > size);
    nbytes = fread(src->buffer, 1, size, src->infile);
    src->buffer[nbytes-1] = 0xd0 | src->data->rst_next;
    src->data->rst_next = (src->data->rst_next+1) % 8;
    assert(nbytes == size);
    src->data->file_pos = (*src->data->index)[src->data->serve_iy*src->data->iw+src->data->serve_ix] + nbytes;
    src->data->serve_ix++;
    if (src->data->serve_ix >= src->data->serve_maxx) {
      src->data->serve_ix = src->data->serve_minx;
      src->data->serve_iy++;
      if (src->data->serve_iy == src->data->serve_maxy) {
        src->data->serve_fakejpg = 0;
        //printf("we have served the whole area!\n");
      }
    }
  }
  else
    nbytes = fread(src->buffer, 1, INPUT_BUF_SIZE, src->infile);
  

  if (nbytes <= 0) {
    if (src->start_of_file) /* Treat empty input file as fatal error */
      ERREXIT(cinfo, JERR_INPUT_EMPTY);
    WARNMS(cinfo, JWRN_JPEG_EOF);
    /* Insert a fake EOI marker */
    src->buffer[0] = (JOCTET) 0xFF;
    src->buffer[1] = (JOCTET) JPEG_EOI;
    nbytes = 2;
  }

  src->pub.next_input_byte = src->buffer;
  src->pub.bytes_in_buffer = nbytes;
  src->start_of_file = FALSE;
  src->data->file_pos += nbytes;

  return TRUE;
}


/*
 * Skip data --- used to skip over a potentially large amount of
 * uninteresting data (such as an APPn marker).
 *
 * Writers of suspendable-input applications must note that skip_input_data
 * is not granted the right to give a suspension return.  If the skip extends
 * beyond the data currently in the buffer, the buffer can be marked empty so
 * that the next read will cause a fill_input_buffer call that can suspend.
 * Arranging for additional bytes to be discarded before reloading the input
 * buffer is the application writer's problem.
 */

METHODDEF(void)
skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
  my_src_ptr src = (my_src_ptr) cinfo->src;

  /* Just a dumb implementation for now.  Could use fseek() except
   * it doesn't work on pipes.  Not clear that being smart is worth
   * any trouble anyway --- large skips are infrequent.
   */
  if (num_bytes > 0) {
    while (num_bytes > (long) src->pub.bytes_in_buffer) {
      num_bytes -= (long) src->pub.bytes_in_buffer;
      (void) fill_input_buffer(cinfo);
      /* note we assume that fill_input_buffer will never return FALSE,
       * so suspension need not be handled.
       */
    }
    src->pub.next_input_byte += (size_t) num_bytes;
    src->pub.bytes_in_buffer -= (size_t) num_bytes;
  }
}


/*
 * An additional method that can be provided by data source modules is the
 * resync_to_restart method for error recovery in the presence of RST markers.
 * For the moment, this source module just uses the default resync method
 * provided by the JPEG library.  That method assumes that no backtracking
 * is possible.
 */


/*
 * Terminate source --- called by jpeg_finish_decompress
 * after all data has been read.  Often a no-op.
 *
 * NB: *not* called by jpeg_abort or jpeg_destroy; surrounding
 * application must deal with any cleanup that should happen even
 * for error exit.
 */

METHODDEF(void)
term_source (j_decompress_ptr cinfo)
{
  /* no work necessary here */
}


/*
 * Prepare for input from a stdio stream.
 * The caller must have already opened the stream, and is responsible
 * for closing it after finishing decompression.
 */

GLOBAL(void)
jpeg_hacked_stdio_src (j_decompress_ptr cinfo, FILE * infile, _Data *data)
{
  my_src_ptr src;

  /* The source object and input buffer are made permanent so that a series
   * of JPEG images can be read from the same file by calling jpeg_stdio_src
   * only before the first one.  (If we discarded the buffer at the end of
   * one image, we'd likely lose the start of the next one.)
   * This makes it unsafe to use this manager and a different source
   * manager serially with the same JPEG object.  Caveat programmer.
   */
  if (cinfo->src == NULL) { /* first time for this JPEG object? */
    cinfo->src = (struct jpeg_source_mgr *)
      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
          sizeof(my_source_mgr));
    src = (my_src_ptr) cinfo->src;
    src->buffer = (JOCTET *)
      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
          2*INPUT_BUF_SIZE * sizeof(JOCTET));
  }

  src = (my_src_ptr) cinfo->src;
  src->pub.init_source = init_source;
  src->pub.fill_input_buffer = fill_input_buffer;
  src->pub.skip_input_data = skip_input_data;
  src->pub.resync_to_restart = jpeg_resync_to_restart; /* use default method */
  src->pub.term_source = term_source;
  src->infile = infile;
  src->data = data;
  //FIXME should also seek to pos 0!
  src->data->file_pos = 0;
  src->pub.bytes_in_buffer = 0; /* forces fill_input_buffer on first read */
  src->pub.next_input_byte = NULL; /* until buffer loaded */
}


struct my_error_mgr {
  struct jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
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

void *_data_new(Filter *f, void *data)
{
  _Data *newdata = calloc(sizeof(_Data), 1);
  
  *newdata = *(_Data*)data;

  //FIXME reuse (create on input_fixed?)
  //newdata->index = calloc(sizeof(int**), 1);
  
  return newdata;
}

void _loadjpeg_worker_ijg(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  _Data *data = ea_data(f->data, thread_id);
  
  uint8_t *r, *g, *b;
  uint8_t *rp, *gp, *bp;
  int i, j, l;
  JSAMPARRAY buffer;
  int row_stride;
  FILE *file;
  int lines_read;
  
  struct jpeg_decompress_struct cinfo;
  struct my_error_mgr jerr;
  
  assert(out && ea_count(out) == 3);
  
  //maximum scaledown: 1/8
  assert(area->corner.scale <= 3);
  int mul = 1u << area->corner.scale;
 
  data->serve_minx = mul*area->corner.x / (data->rst_int*data->mcu_w);
  data->serve_miny = mul*area->corner.y / data->mcu_h;
  if (mul*(area->corner.x + area->width) > data->w)
      data->serve_width = data->w-mul*area->corner.x;
  else
    data->serve_width = mul*area->width;
  if (mul*(area->corner.y + area->height) > data->h)
      data->serve_height = data->h-mul*area->corner.y;
  else
    data->serve_height = mul*area->height;
  assert(data->serve_width % (data->rst_int*data->mcu_w) == 0);
  data->serve_maxx = data->serve_minx+data->serve_width / (data->rst_int*data->mcu_w);
  data->serve_maxy = data->serve_miny+data->serve_height / data->mcu_h;
  if (data->serve_maxy > data->h / data->mcu_h)
    data->serve_maxy = data->h / data->mcu_h;
  data->serve_ix = data->serve_minx;
  data->serve_iy = data->serve_miny;
  data->serve_fakejpg = 0;
  data->iw = data->w / (data->mcu_w*data->rst_int);
  data->ih = data->h / data->mcu_h;
  /*if (area->corner.x || area->corner.y) {
    printf("FIXME: invalid tile requested in loadjpg: %dx%d\n", area->corner.x, area->corner.y);
    return;
  }*/
  
  r = ((Tiledata*)ea_data(out, 0))->data;
  g = ((Tiledata*)ea_data(out, 1))->data;
  b = ((Tiledata*)ea_data(out, 2))->data;
  
  file = fopen(data->filename, "rb");
  
  if (!file) {
    printf("error opening file!\n");
    data->common->error = EINA_TRUE;
    return;
  }
  
  if (!(*data->index)) {
    pthread_mutex_lock(data->lock);
    if (!(*data->index)) {
      if (jpeg_read_infos(file, data)) {
	printf("corrupt jpeg!\n");
	data->common->error = EINA_TRUE;
	fclose(file);
	return;
      }
      fseek(file, 0, SEEK_SET);
    }
    pthread_mutex_unlock(data->lock);
  }
  
  cinfo.err = jpeg_std_error(&jerr.pub);
  
  if (setjmp(jerr.setjmp_buffer)) {
    jpeg_destroy_decompress(&cinfo);
    fclose(file);
    printf("error opening jpeg file!\n");
    data->common->error = EINA_TRUE;
    return;
  }
  jpeg_create_decompress(&cinfo);

  jpeg_hacked_stdio_src(&cinfo, file, data);
  
  (void) jpeg_read_header(&cinfo, TRUE);
  
  cinfo.scale_num = 1;
  cinfo.scale_denom = 1u << area->corner.scale;
  
  assert(cinfo.jpeg_color_space == JCS_YCbCr);
  cinfo.out_color_space   = JCS_YCbCr;
  cinfo.dct_method = JDCT_IFAST;
  cinfo.do_fancy_upsampling = FALSE;
  jpeg_start_decompress(&cinfo);
  
  assert(!cinfo.coef_bits);
    
  row_stride = cinfo.output_width * cinfo.output_components;
  buffer = (*cinfo.mem->alloc_sarray)
  ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, data->mcu_h/mul);
  
  //FIXME???
  assert(area->width >= cinfo.output_width);
  assert(data->serve_height/mul <= area->height);
  
  rp = r;
  gp = g;
  bp = b;
  l = 0;
  while (l<data->serve_height/mul) {
    lines_read = jpeg_read_scanlines(&cinfo, buffer, data->mcu_h/mul);
    l += lines_read;
    for(j=0;j<lines_read;j++,rp+=area->width,gp+=area->width,bp+=area->width)
      for(i=0;i<cinfo.output_width;i++) {
        rp[i] = buffer[j][i*3];
        gp[i] = buffer[j][i*3+1];
        bp[i] = buffer[j][i*3+2];
      }
  }
  
  jpeg_abort_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  
  fclose(file);
}

void _loadjpeg_worker_ijg_original(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  _Data *data = ea_data(f->data, thread_id);
  
  uint8_t *r, *g, *b;
  uint8_t *rp, *gp, *bp;
  int i, j;
  int xstep, ystep;
  JSAMPARRAY buffer;    /* Output row buffer */
  int row_stride;   /* physical row width in output buffer */
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
  
  file = fopen(data->filename, "rb");
    
  if (!file) {
    printf("error opening file!\n");
    data->common->error = EINA_TRUE;
    return;
  }
  
  cinfo.err = jpeg_std_error(&jerr.pub);
  
  if (setjmp(jerr.setjmp_buffer)) {
    jpeg_destroy_decompress(&cinfo);
    fclose(file); {
    printf("error opening jpeg file!\n");
    data->common->error = EINA_TRUE;
    return;
  }
  }
  jpeg_create_decompress(&cinfo);

  jpeg_stdio_src(&cinfo, file);

  (void) jpeg_read_header(&cinfo, TRUE);

  cinfo.scale_num = 1;
  cinfo.scale_denom = 1u << area->corner.scale;
  
  assert(cinfo.jpeg_color_space == JCS_YCbCr);
  cinfo.out_color_space   = JCS_YCbCr;
  cinfo.dct_method = JDCT_IFAST;
  cinfo.do_fancy_upsampling = FALSE;
  jpeg_start_decompress(&cinfo);
  
  row_stride = cinfo.output_width * cinfo.output_components;
  buffer = (*cinfo.mem->alloc_sarray)
  ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 16);
  
  rp = r;
  gp = g;
  bp = b;
  xstep = 1;
  ystep = 0;

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

void simple_scale(int src_w, int src_h, int dst_w, int dst_h, uint8_t *src, uint8_t *dst)
{
  int i, j;
  
  for(j=0;j<dst_h;j++)
    for(i=0;i<dst_w;i++,dst++) {
      *dst = src[j*src_h/dst_h*src_w+i*src_w/dst_w];
    }
}

void _loadjpeg_worker_ijg_thumb(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  _Data *data = ea_data(f->data, thread_id);
  
  uint8_t *r, *g, *b;
  uint8_t *rp, *gp, *bp;
  int i, j;
  JSAMPARRAY buffer;    /* Output row buffer */
  int row_stride;   /* physical row width in output buffer */
  int lines_read;
  uint8_t *rt, *gt, *bt;
  int mul = 1u << area->corner.scale;
  
  struct jpeg_decompress_struct cinfo;
  struct my_error_mgr jerr;
  
  assert(out && ea_count(out) == 3);
  
  if (area->corner.x || area->corner.y) {
    printf("FIXME: invalid tile requested in loadjpg: %dx%d\n", area->corner.x, area->corner.y);
    return;
  }
  
  r = ((Tiledata*)ea_data(out, 0))->data;
  g = ((Tiledata*)ea_data(out, 1))->data;
  b = ((Tiledata*)ea_data(out, 2))->data;

  cinfo.err = jpeg_std_error(&jerr.pub);
  
  if (setjmp(jerr.setjmp_buffer)) {
    jpeg_destroy_decompress(&cinfo); {
    printf("error opening jpeg file!\n");
    data->common->error = EINA_TRUE;
    return;
  }
  }
  jpeg_create_decompress(&cinfo);

  jpeg_mem_src(&cinfo, data->thumb_data, data->thumb_len);

  (void)jpeg_read_header(&cinfo, TRUE);
  
  assert(cinfo.jpeg_color_space == JCS_YCbCr);
  cinfo.out_color_space   = JCS_YCbCr;
  cinfo.dct_method = JDCT_IFAST;
  cinfo.do_fancy_upsampling = FALSE;
  jpeg_start_decompress(&cinfo);
     
  row_stride = cinfo.output_width * cinfo.output_components;
  buffer = (*cinfo.mem->alloc_sarray)
  ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 16);
  
  rt = malloc(cinfo.output_width*cinfo.output_height);
  gt = malloc(cinfo.output_width*cinfo.output_height);
  bt = malloc(cinfo.output_width*cinfo.output_height);
  
  rp = rt;
  gp = gt;
  bp = bt;
  
  while (cinfo.output_scanline < cinfo.output_height) {
    lines_read = jpeg_read_scanlines(&cinfo, buffer, 16);
    for(j=0;j<lines_read;j++)
      for(i=0;i<cinfo.output_width;i++,rp++,gp++,bp++) {
        rp[0] = buffer[j][i*3];
        gp[0] = buffer[j][i*3+1];
        bp[0] = buffer[j][i*3+2];
      }
  }

  simple_scale(cinfo.output_width, cinfo.output_height, data->w/mul, data->h/mul, rt, r);
  simple_scale(cinfo.output_width, cinfo.output_height, data->w/mul, data->h/mul, gt, g);
  simple_scale(cinfo.output_width, cinfo.output_height, data->w/mul, data->h/mul, bt, b);
  
  free(rt);
  free(gt);
  free(bt);
  
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
}

void _loadjpeg_worker(Filter *f, Eina_Array *in, Eina_Array *out, Rect *area, int thread_id)
{
  _Data *data = ea_data(f->data, thread_id);
  
  if (data->common->error)
    return;
  
  //FIXME use thumb only on smallest scale
  if (area->corner.scale < data->seekable)
    _loadjpeg_worker_ijg(f, in, out, area, thread_id);
  else if (area->corner.scale >= 4)
    _loadjpeg_worker_ijg_thumb(f, in, out, area, thread_id);
  else
    _loadjpeg_worker_ijg_original(f, in, out, area, thread_id);  
}

int min(a, b) 
{
  if (a<=b) return a;
  return b;
}

int _loadjpeg_input_fixed(Filter *f)
{
  int i;
  _Data *data = ea_data(f->data, 0);
  _Data *tdata;
  struct jpeg_decompress_struct cinfo;
  struct my_error_mgr jerr;
  FILE *file;
  
  file = fopen(data->input->data, "r");
  
  if (!file)
    return -1;
  
  if (*data->index) {
    free(*data->index);
    *data->index = NULL;
  }
  
  for(i=0;i<ea_count(f->data);i++) {
    tdata = ea_data(f->data, i);
    if (!tdata->filename || strcmp(tdata->filename, data->input->data)) {
      tdata->filename = data->input->data;
      assert(!*tdata->index);
    }
  }
  
  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = my_error_exit;

  if (setjmp(jerr.setjmp_buffer)) {
    jpeg_destroy_decompress(&cinfo);
    fclose(file);
    return -1;
  }
  
  //data->rot = _get_exif_orientation(data->filename);
  
  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, file);
  jpeg_read_header(&cinfo, TRUE);
  jpeg_calc_output_dimensions(&cinfo);
  
  if (cinfo.jpeg_color_space != JCS_YCbCr) {
    printf("implement jpeg_color_space %d\n", cinfo.jpeg_color_space);
    return -1;
  }
  
  get_exif_stuff(data->filename, &data->thumb_data, &data->thumb_len, &data->rot);
  
  
  data->mcu_w = cinfo.max_h_samp_factor*8;
  data->mcu_h = cinfo.max_v_samp_factor*8;
  data->rst_int = cinfo.restart_interval;
  data->w = cinfo.output_width;
  data->h = cinfo.output_height;
  data->comp_count = cinfo.num_components;
  
  //data->thumb_len = get_exif_preview(data->filename, &data->thumb_data);
  
  //printf("seekable tile size: %dx%d\n", data->rst_int*data->mcu_w, data->mcu_h);

  if (data->rst_int 
    && JPEG_TILE_WIDTH % (data->rst_int * data->mcu_w) == 0)
    data->seekable = 3;
  
  ((Dim*)data->dim)->scaledown_max = 3;
  
  //FIXME check wether thumbnail image is larger than image/16!
  if (data->thumb_len) {
    //FIXME do not assume thumbnail size
    while (160 < data->w / (1u << ((Dim*)data->dim)->scaledown_max))
      ((Dim*)data->dim)->scaledown_max++;
  }
  ((Dim*)data->dim)->width = cinfo.output_width;
  ((Dim*)data->dim)->height = cinfo.output_height;
  
  f->tw_s = malloc(sizeof(int)*((Dim*)data->dim)->scaledown_max);
  f->th_s = malloc(sizeof(int)*((Dim*)data->dim)->scaledown_max);
  
  for(i=0;i<data->seekable;i++) {
    cinfo.scale_num = 1;
    cinfo.scale_denom = 1u << i;
    jpeg_calc_output_dimensions(&cinfo);
    f->tw_s[i] = min(JPEG_TILE_WIDTH, cinfo.output_width);
    f->th_s[i] = min(JPEG_TILE_HEIGHT, cinfo.output_height);
  }
  for(i=data->seekable;i<4;i++) {
    cinfo.scale_num = 1;
    cinfo.scale_denom = 1u << i;
    jpeg_calc_output_dimensions(&cinfo);
    f->tw_s[i] = cinfo.output_width;
    f->th_s[i] = cinfo.output_height;
  }
  for(i=4;i<=((Dim*)data->dim)->scaledown_max;i++) {
    cinfo.scale_num = 1;
    cinfo.scale_denom = 1u << i;
    f->tw_s[i] = data->w/cinfo.scale_denom;
    f->th_s[i] = data->h/cinfo.scale_denom;
  }
  
  jpeg_destroy_decompress(&cinfo);
  
  fclose(file);

  return 0;
}

static int _del(Filter *f)
{
  _Data *data = ea_data(f->data, 0);
  int i;
  
  free(data->thumb_data);
  
  for(i=0;i<ea_count(f->data);i++) {
    data = ea_data(f->data, i);
    free(data);
  }
  
  return 0;
}

Filter *filter_loadjpeg_new(void)
{
  Filter *filter = filter_new(&filter_core_loadjpeg);
  Meta *in, *out, *channel, *bitdepth, *color, *dim, *fliprot;
  _Data *data = calloc(sizeof(_Data), 1);
  data->common = calloc(sizeof(_Common), 1);
  data->index = calloc(sizeof(int**), 1);
  data->size_pos = calloc(sizeof(int*), 1);
  data->lock = calloc(sizeof(pthread_mutex_t), 1);
  assert(pthread_mutex_init(data->lock, NULL) == 0);
  data->dim = calloc(sizeof(Dim), 1);
  
  filter->del = &_del;
  filter->mode_buffer = filter_mode_buffer_new();
  filter->mode_buffer->worker = &_loadjpeg_worker;
  filter->mode_buffer->threadsafe = 1;
  filter->mode_buffer->data_new = &_data_new;
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
  
  fliprot = meta_new(MT_FLIPROT, filter);
  meta_attach(out, fliprot);
  data->fliprot = fliprot;
  data->fliprot->data = &data->rot;
  
  channel = meta_new_channel(filter, 1);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_YUV_Y;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, dim);
  meta_attach(out, channel);
  
  channel = meta_new_channel(filter, 2);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_YUV_U;
  meta_attach(channel, color);
  meta_attach(channel, bitdepth);
  meta_attach(channel, dim);
  meta_attach(out, channel);
  
  channel = meta_new_channel(filter, 3);
  color = meta_new_data(MT_COLOR, filter, malloc(sizeof(int)));
  *(int*)(color->data) = CS_YUV_V;
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
