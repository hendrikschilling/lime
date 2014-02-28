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

#include <Eina.h>
#include <Eio.h>
#include <exempi/xmp.h>
#include <exempi/xmpconsts.h>
#include <assert.h>
#include "tagfiles.h"

#define MAX_XMP_FILE 1024*1024

/*Eina_Hash *tags_filter;
int tags_filter_rating = 0;
char *dir;*/

struct _Tagged_File {
  const char *dirname;
  const char *filename;
  const char *sidecar;
  Eina_Array *tags;
};

struct _File_Group {
  Eina_Inarray *files; //tagged files
  const char *sidecar; //sidecar file used for the image group
  char *last_fc; //serialization of the last filter chain
  Eina_Hash *tags; //all tags of the group or a file in the group
  int32_t tag_rating;
  //Eina_Array *group_tags; //tags that are assigned specifically to the group
};

struct _Tagfiles {
  int idx;
  int files_sorted;
  int unsorted_insert;
  Eina_Inarray *dirs_ls;
  Eina_Inarray *files_ls;
  Eina_Inarray *files;
  Eina_Hash *files_hash;
  void (*progress_cb)(Tagfiles *files, void *data);
  void (*done_cb)(Tagfiles *files, void *data);
  Eina_Hash *known_tags;
  void *cb_data;
};

int filegroup_cmp(File_Group **a, File_Group **b)
{
  Tagged_File *fa = NULL, *fb = NULL;
  
  if (eina_inarray_count((*a)->files))
    fa = eina_inarray_nth((*a)->files, 0);
  if (eina_inarray_count((*b)->files))
    fb = eina_inarray_nth((*b)->files, 0);
  
  if (!fa->filename)
    fa = NULL;
  if (!fb->filename)
    fb = NULL;
  
  if (!fa) return -1;
  else if (!fb) return 1;
  
  return strcmp(fa->filename, fb->filename);
}

int filegroup_cmp_neg(File_Group **a, File_Group **b)
{
  return -filegroup_cmp(a, b);
}


static void _files_check_sort(Tagfiles *files)
{
  if (files->files_sorted)
    return;
  
  eina_inarray_sort(files->files, filegroup_cmp);
  files->files_sorted = EINA_TRUE;
}

File_Group *tagfiles_get(Tagfiles *tagfiles)
{
  File_Group *group;
  
  if (tagfiles->idx < eina_inarray_count(tagfiles->files)) {
    _files_check_sort(tagfiles);
    return *(File_Group**)eina_inarray_nth(tagfiles->files, tagfiles->idx);
  }
  else {
    //we have to take files from files_ls
    assert(tagfiles->idx < eina_inarray_count(tagfiles->files) + eina_inarray_count(tagfiles->files_ls));
    
    //we have already inserte files from this dir: always need to sort after insert
    if (tagfiles->unsorted_insert) {
      while (eina_inarray_count(tagfiles->files_ls)) {
	group = *(File_Group**)eina_inarray_pop(tagfiles->files_ls);
	eina_inarray_push(tagfiles->files, &group);
      }
      tagfiles->files_sorted = EINA_FALSE;
      //eina_inarray_sort(tagfiles->files, filegroup_cmp);
    }
    //we have not yet inserted any files from the dir - can sort before!
    else {
      assert(tagfiles->files_sorted == EINA_TRUE);
      
      eina_inarray_sort(tagfiles->files_ls, filegroup_cmp_neg);
      while (eina_inarray_count(tagfiles->files_ls)) {
	group = *(File_Group**)eina_inarray_pop(tagfiles->files_ls);
	eina_inarray_push(tagfiles->files, &group);
      }
    }
    tagfiles->unsorted_insert = EINA_TRUE;
    
    _files_check_sort(tagfiles);
    assert(tagfiles->idx < eina_inarray_count(tagfiles->files));
    return *(File_Group**)eina_inarray_nth(tagfiles->files, tagfiles->idx);
  }
}

int tagfiles_idx(Tagfiles *files)
{ 
  return files->idx;
}

int tagfiles_idx_set(Tagfiles *files, int idx)
{
  //FIXME check if we are still listing files?
  
  while (idx < 0)
    idx += tagfiles_count(files);
  
  files->idx = idx % tagfiles_count(files);
  
  return idx;
}

int tagfiles_step(Tagfiles *files, int step)
{
  tagfiles_idx_set(files, tagfiles_idx(files)+step);
  
  return tagfiles_idx(files);
}

int tagfiles_count(Tagfiles *files)
{
  return eina_inarray_count(files->files)+eina_inarray_count(files->files_ls);
}

void tagfiles_del(Tagfiles *files)
{
  abort();
}

const char *filegroup_nth(File_Group *g, int n)
{
  assert(n < eina_inarray_count(g->files));
  
  return ((Tagged_File*)eina_inarray_nth(g->files, n))->filename;
}

int filegroup_count(File_Group *g)
{
  return eina_inarray_count(g->files);
}

char *filegroup_filterchain(File_Group *g)
{
  return g->last_fc;
}

static Eina_Bool
_ls_filter_cb(void *data, Eio_File *handler, const Eina_File_Direct_Info *info)
{  
  if (info->type == EINA_FILE_REG || info->type == EINA_FILE_LNK || info->type == EINA_FILE_UNKNOWN || info->type == EINA_FILE_DIR)
    return EINA_TRUE;
    
  return EINA_FALSE;
}


Tagged_File tag_file_new(Tagfiles *tagfiles, File_Group *group, const char *name)
{
  Tagged_File file = {NULL, NULL, NULL};
  
  if (eina_str_has_extension(name, ".xmp")) {
    //FIXME only one sidecar per group for now
    if (group->sidecar)
      printf("FIXME multiple sidecar: %s\n", name);
    else
      group->sidecar = file.sidecar;
    assert(!file.sidecar);
    file.sidecar = name;
    //xmp_gettags(tagfiles, name, group);
  }
  else 
    file.filename = name;

  return file;
}

File_Group *file_group_new(Tagfiles *tagfiles, const char *name, const char *basename)
{
  Tagged_File file_new;
  File_Group *group = calloc(sizeof(File_Group), 1);
  
  group->files = eina_inarray_new(sizeof(Tagged_File), 2);
  group->tags = eina_hash_string_superfast_new(NULL);
  
  eina_hash_add(tagfiles->files_hash, basename, group);
  
  file_new = tag_file_new(tagfiles, group, name);
  eina_inarray_push(group->files, &file_new);
  
  return group;
}

void file_group_add(Tagfiles *tagfiles, File_Group *group, const char *name)
{
  Tagged_File file_new;
  //FIXME handel fitting sidecar and images
  
  file_new = tag_file_new(tagfiles, group, name);
  eina_inarray_push(group->files, &file_new);
}

void insert_file(Tagfiles *tagfiles, const char *file)
{
  const char *basename;
  File_Group *group;
  
  if (strchr(file, '.'))
    basename = eina_stringshare_add_length(file, strchr(file, '.')-file);
  else
    basename = eina_stringshare_add(file);
  
  group = eina_hash_find(tagfiles->files_hash, basename);
  
  if (!group) {
    group = file_group_new(tagfiles, file, basename);
    eina_inarray_push(tagfiles->files_ls, &group);
  }
  else
    file_group_add(tagfiles, group, file);
  
  return;
}

static void _ls_main_cb(void *data, Eio_File *handler, const Eina_File_Direct_Info *info)
{
  Tagfiles *tagfiles = data;
  const char *file;
  
  file = eina_stringshare_add(info->path);
  if (!file)
    return;

  if (info->type != EINA_FILE_DIR) {
    insert_file(tagfiles, file);
    
    tagfiles->progress_cb(tagfiles, tagfiles->cb_data);
  }
  else
    eina_inarray_push(tagfiles->dirs_ls, &file);
}

int tagfiles_init(void)
{
  return xmp_init();
}

void tagfiles_shutdown(void)
{
  xmp_terminate();
}

void xmp_gettags(Tagfiles *tagfiles, const char *file, File_Group *group)
{
  int len;
  FILE *f;
  XmpPtr xmp;
  char *buf;
  const char *tag;
  XmpIteratorPtr iter;
  XmpStringPtr propValue;
  
  buf = malloc(MAX_XMP_FILE);
  
  f = fopen(file, "r");
  
  if (!f)
    return;
  
  len = fread(buf, 1, MAX_XMP_FILE, f);
    
  fclose(f);
  
  xmp = xmp_new(buf, len);
  
  if (!xmp) {
    printf("parse failed\n");
    xmp_free(xmp);
    free(buf);
    return;
  }
  
 

  if (xmp_prefix_namespace_uri("lr", NULL))
  {
    propValue = xmp_string_new();
    iter = xmp_iterator_new(xmp, "http://ns.adobe.com/lightroom/1.0/", "lr:hierarchicalSubject", XMP_ITER_JUSTLEAFNODES);
  
    while (xmp_iterator_next(iter, NULL, NULL, propValue, NULL)) {
      tag = strdup(xmp_string_cstr(propValue));
      if (!eina_hash_find(group->tags, tag))
			eina_hash_add(group->tags, tag, tag);
      if (!eina_hash_find(tagfiles->known_tags, tag))
			eina_hash_add(tagfiles->known_tags, tag, tag);
    }

    xmp_iterator_free(iter); 
    xmp_string_free(propValue);
  }
  
  if (xmp_prefix_namespace_uri("xmp", NULL))
  {
    if (!xmp_get_property_int32(xmp, "http://ns.adobe.com/xap/1.0/", "xmp:Rating", &group->tag_rating, NULL))
      group->tag_rating = 0;
  }
  
  if (xmp_prefix_namespace_uri("lime", NULL))
  {
    propValue = xmp_string_new();
    if (xmp_get_property(xmp, "http://technik-stinkt.de/lime/0.1/", "lime:lastFilterChain", propValue, NULL))
      group->last_fc = strdup(xmp_string_cstr(propValue));
    else
      group->last_fc = NULL;
    xmp_string_free(propValue);
  }
  
  
  xmp_free(xmp);
  free(buf);
  
  return;
}

static void _ls_error_cb(void *data, Eio_File *handler, int error)
{
  fprintf(stderr, "error: [%s]\n", strerror(error));
  abort();
  //FIXME implement error cb!
}

int dir_strcmp_neg(const char **a, const char **b)
{
  return -strcmp(*a, *b);
}

static void _ls_done_cb(void *data, Eio_File *handler)
{
  Tagfiles *tagfiles = data;
  File_Group *group;

  //we have already inserted files from this dir: always need to sort after insert
  if (tagfiles->unsorted_insert) {
    while (eina_inarray_count(tagfiles->files_ls)) {
      group = *(File_Group**)eina_inarray_pop(tagfiles->files_ls);
      eina_inarray_push(tagfiles->files, &group);
    }
    tagfiles->files_sorted = EINA_FALSE;
  }
  //we have not yet inserted any files from the dir - can sort before!
  else {
    if (tagfiles->files_sorted)
      eina_inarray_sort(tagfiles->files_ls, filegroup_cmp_neg);
    while (eina_inarray_count(tagfiles->files_ls)) {
      group = *(File_Group**)eina_inarray_pop(tagfiles->files_ls);
      eina_inarray_push(tagfiles->files, &group);
    }
  }
  _files_check_sort(tagfiles);
    
  //have finished dir - next dir is guaranteed to come after all files already seen
  tagfiles->unsorted_insert = EINA_FALSE;
  
  if (eina_inarray_count(tagfiles->dirs_ls)) {
    //dirs are sorted before scanning so we do not need to sort all files at once!
    eina_inarray_sort(tagfiles->dirs_ls, dir_strcmp_neg);
    printf("scan %s\n", *(char**)eina_inarray_nth(tagfiles->dirs_ls, eina_inarray_count(tagfiles->dirs_ls)-1));
    eio_file_direct_ls(*(char**)eina_inarray_pop(tagfiles->dirs_ls), &_ls_filter_cb, &_ls_main_cb,&_ls_done_cb, &_ls_error_cb, tagfiles);
  }
  else
    tagfiles->done_cb(tagfiles, tagfiles->cb_data);
}


Tagfiles *tagfiles_new_from_dir(const char *path, void (*progress_cb)(Tagfiles *files, void *data), void (*done_cb)(Tagfiles *files, void *data))
{
  Tagfiles *files = calloc(sizeof(Tagfiles), 1);
  
  files->progress_cb = progress_cb;
  files->done_cb = done_cb;
  files->known_tags = eina_hash_stringshared_new(NULL);
  files->files = eina_inarray_new(sizeof(File_Group*), 128);
  files->files_hash = eina_hash_string_superfast_new(NULL);
  files->files_ls = eina_inarray_new(sizeof(File_Group*), 128);
  files->dirs_ls = eina_inarray_new(sizeof(char *), 32);
  files->files_sorted = EINA_TRUE;
  
  eio_file_direct_ls(path, &_ls_filter_cb, &_ls_main_cb,&_ls_done_cb, &_ls_error_cb, files);
  
  return files;
}

void save_sidecar(File_Group *group);


Tagged_File *tagged_file_new_from_path(const char *path)
{
  Tagged_File *file = calloc(sizeof(Tagged_File), 1);
  char *filename;
  
  filename = strrchr(path, '/');
  
  if (filename) {
    file->dirname = eina_stringshare_add_length(path, filename-path);
    file->filename = eina_stringshare_add(filename+1);
  }
  else
    file->filename = eina_stringshare_add(path);

  return file;
}

int cmp_img_file(char *a, char *b)
{
	if (strlen(a) == 12 && strlen(b) == 12 && a[0] == 'P' && b[0] == 'P' && a[8] == '.' && b[8] == '.')
		//cmp just file number not date
		return strcmp(a+4, b+4);
	
	return strcmp(a, b);
	
}

int cmp_tagged_files(Tagged_File *a, Tagged_File *b)
{
  int cmp;
   
  cmp = strcmp(a->dirname, b->dirname);
  
  if (!cmp)
    cmp = strcmp(a->filename, b->filename);
  
  return cmp;
}

Eina_Bool xmp_add_tags_lr_func(const Eina_Hash *hash, const void *key, void *data, void *fdata)
{
  const char *tag = data;
  XmpPtr xmp = fdata;
  
  xmp_append_array_item(xmp, "http://ns.adobe.com/lightroom/1.0/", "lr:hierarchicalSubject", XMP_PROP_ARRAY_IS_UNORDERED, tag, 0);
  
  return 1;
}

Eina_Bool xmp_add_tags_dc_func(const Eina_Hash *hash, const void *key, void *data, void *fdata)
{
  const char *tag = data;
  XmpPtr xmp = fdata;
  
  xmp_append_array_item(xmp, "http://purl.org/dc/elements/1.1/", "dc:subject", XMP_PROP_ARRAY_IS_UNORDERED, tag, 0);
  
  return 1;
}

Eina_Bool xmp_add_tags_dk_func(const Eina_Hash *hash, const void *key, void *data, void *fdata)
{
  char *tag = data;
  char *replace;
  XmpPtr xmp = fdata;
  
  while ((replace = strchr(tag, '|')))
    *replace = '/';
  
  xmp_append_array_item(xmp, "http://www.digikam.org/ns/1.0/", "digiKam:TagsList", XMP_PROP_ARRAY_IS_UNORDERED, tag, 0);
  
  //FIXME
  //free(tag);
  
  return 1;
}

static void new_tag_file(File_Group *group)
{
  FILE *f;
  const char *buf;
  XmpPtr xmp;
  XmpStringPtr xmp_buf = xmp_string_new();
  
  xmp = xmp_new_empty();
  xmp_register_namespace("http://ns.adobe.com/lightroom/1.0/", "lr", NULL);
  xmp_register_namespace("http://purl.org/dc/elements/1.1/", "dc", NULL);
  xmp_register_namespace("http://www.digikam.org/ns/1.0/", "digiKam", NULL);
  xmp_register_namespace("http://technik-stinkt.de/lime/0.1/", "lime", NULL);
  
  eina_hash_foreach(group->tags, xmp_add_tags_lr_func, xmp);
  eina_hash_foreach(group->tags, xmp_add_tags_dc_func, xmp);
  eina_hash_foreach(group->tags, xmp_add_tags_dk_func, xmp);
  
  if (group->tag_rating) {
    if (!xmp_prefix_namespace_uri("xmp", NULL))
      xmp_register_namespace("http://ns.adobe.com/xap/1.0/", "xmp", NULL);
    xmp_set_property_int32(xmp, "http://ns.adobe.com/xap/1.0/", "xmp:Rating", group->tag_rating, 0);
  }
  
  if (group->last_fc)
    xmp_set_property(xmp, "http://technik-stinkt.de/lime/0.1/", "lime:lastFilterChain", group->last_fc, 0);
  
  xmp_serialize(xmp, xmp_buf, XMP_SERIAL_OMITPACKETWRAPPER | XMP_SERIAL_USECOMPACTFORMAT, 0);
  
  f = fopen(group->sidecar, "w");
  assert(f);
  buf = xmp_string_cstr(xmp_buf);
  fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n%s\n", buf);
  fclose(f);
  
  xmp_string_free(xmp_buf);
  xmp_free(xmp);
  
  printf("wrote new tag file!\n");
}


void add_group_sidecar(File_Group *group)
{
  abort();
  
  if (group->sidecar)
    return;
  
  return;
  
  //FIXME find a better way to find a xmp filename!
  /*EINA_INARRAY_FOREACH(group->files, &file) {
    assert(file->filename);
    if (!strcmp(file->filename+strlen(file->filename)-4, ".jpg") || !strcmp(file->filename+strlen(file->filename)-4, ".JPG")) {
      buf = malloc(strlen(file->filename)+5);
      sprintf(buf, "%s.xmp", file->filename);
      group->sidecar = buf;
      file->sidecar = group->sidecar;
      return;
    }
  }*/

  /*file = eina_inarray_nth(group->files, 0);
  buf = malloc(strlen(file->filename+5));
  sprintf(buf, "%s.xmp", file->filename);
  group->sidecar = buf;
  file->sidecar = group->sidecar;*/
}

void save_sidecar(File_Group *group)
{
  int len;
  FILE *f;
  XmpStringPtr xmp_buf = xmp_string_new();
  char *buf = malloc(MAX_XMP_FILE);
  XmpPtr xmp;
  
  add_group_sidecar(group);
    
  f = fopen(group->sidecar, "r");
  
  if (!f) {
    new_tag_file(group);
    return;
  }
  
  len = fread(buf, 1, MAX_XMP_FILE, f);
  fclose(f);
  
  xmp = xmp_new(buf, len);
  free(buf);
  
  if (!xmp) {
    printf("xmp parse failed, overwriting with new!\n");
    xmp_free(xmp);
    new_tag_file(group);
    return;
  }
  
  if (!xmp_prefix_namespace_uri("lr", NULL))
      xmp_register_namespace("http://ns.adobe.com/lightroom/1.0/", "lr", NULL);
  
  if (!xmp_prefix_namespace_uri("dc", NULL))
    xmp_register_namespace("http://purl.org/dc/elements/1.1/", "dc", NULL);
  
  if (!xmp_prefix_namespace_uri("digiKam", NULL))
    xmp_register_namespace("http://www.digikam.org/ns/1.0/", "digiKam", NULL);
  
  if (!xmp_prefix_namespace_uri("lime", NULL))
    xmp_register_namespace("http://technik-stinkt.de/lime/0.1/", "lime", NULL);
  
  //delete all keyword tags 
  xmp_delete_property(xmp, "http://ns.adobe.com/lightroom/1.0/", "lr:hierarchicalSubject");
  xmp_delete_property(xmp, "http://www.digikam.org/ns/1.0/", "digiKam:TagsList");
  xmp_delete_property(xmp, "http://ns.microsoft.com/photo/1.0/", "MicrosoftPhoto:LastKeywordXMP");
  xmp_delete_property(xmp, "http://purl.org/dc/elements/1.1/", "dc:subject");
  xmp_delete_property(xmp, "http://ns.adobe.com/xap/1.0/", "xmp:Rating");
  xmp_delete_property(xmp, "http://technik-stinkt.de/lime/0.1/", "lime:lastFilterChain");

  eina_hash_foreach(group->tags, xmp_add_tags_lr_func, xmp);
  eina_hash_foreach(group->tags, xmp_add_tags_dc_func, xmp);
  eina_hash_foreach(group->tags, xmp_add_tags_dk_func, xmp);

  if (group->tag_rating) {
    if (!xmp_prefix_namespace_uri("xmp", NULL))
      xmp_register_namespace("http://ns.adobe.com/xap/1.0/", "xmp", NULL);
    xmp_set_property_int32(xmp, "http://ns.adobe.com/xap/1.0/", "xmp:Rating", group->tag_rating, 0);
  }

  if (group->last_fc)
    xmp_set_property(xmp, "http://technik-stinkt.de/lime/0.1/", "lime:lastFilterChain", group->last_fc, 0);
      
  xmp_serialize(xmp, xmp_buf, XMP_SERIAL_OMITPACKETWRAPPER | XMP_SERIAL_USECOMPACTFORMAT, 0);
  
  buf = (char*)xmp_string_cstr(xmp_buf);
  
  f = fopen(group->sidecar, "w");
  assert(f);
  buf = (char*)xmp_string_cstr(xmp_buf);
  fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n%s\n", buf);
  fclose(f);
  
  xmp_string_free(xmp_buf);
  xmp_free(xmp);
}

void set_filterchain_save_sidecar(void)
{
  abort();
  /*
  File_Group *group = eina_inarray_nth(files, file_idx);
  
  group->last_fc = lime_filter_chain_serialize(((Filter_Chain*)eina_list_data_get(eina_list_next(filter_chain)))->f);
  if (strrchr(group->last_fc, ','))
    *strrchr(group->last_fc, ',') = '\0';
  save_sidecar(group);*/
}