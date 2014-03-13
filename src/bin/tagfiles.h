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

#ifndef _TAGFILES_H
#define _TAGFILES_H

#define PREREAD_SIZE 4096*8

struct _Tagged_File;
typedef struct _Tagged_File Tagged_File;

struct _File_Group;
typedef struct _File_Group File_Group;

struct _Tagfiles;
typedef struct _Tagfiles Tagfiles;

Tagfiles *tagfiles_new_from_dir(const char *path, void (*progress_cb)(Tagfiles *files, void *data), void (*done_cb)(Tagfiles *files, void *data), void (*known_tags_cb)(Tagfiles *files, void *data, const char *new_tag));
int tagfiles_step(Tagfiles *files, int step);
File_Group *tagfiles_get(Tagfiles *files);
int tagfiles_count(Tagfiles *files);
int tagfiles_pos(Tagfiles *files);
int tagfiles_idx(Tagfiles *files);
int tagfiles_idx_set(Tagfiles *files, int idx);
void tagfiles_del(Tagfiles *files);
int tagfiles_init(void);
void tagfiles_shutdown(void);
int tagfiles_scanned_dirs(Tagfiles *tagfiles);
int tagfiles_scanned_files(Tagfiles *tagfiles);
Eina_Hash *tagfiles_known_tags(Tagfiles *tagfiles);
int filegroup_rating(File_Group *group);
Eina_Hash *filegroup_tags(File_Group *group);
void tagfiles_group_changed_cb_insert(Tagfiles *tagfiles, File_Group *group, void (*filegroup_changed_cb)(File_Group *group));
void tagfiles_group_changed_cb_flush(Tagfiles *files);
void call_group_changed_cb(Tagfiles *files, File_Group *group);
void filegroup_rating_set(File_Group *group, int rating);
const char * filegroup_nth(File_Group *g, int n);
int filegroup_count(File_Group *g);
char *filegroup_filterchain(File_Group *g);
void filegroup_filterchain_set(File_Group *group, const char *fc);
Eina_Bool filegroup_tags_valid(File_Group *group);
File_Group *tagfiles_nth(Tagfiles *tagfiles, int idx);
void tagfiles_preload_headers(Tagfiles *tagfiles, int direction, int range);


void set_filterchain_save_sidecar(void);

#endif