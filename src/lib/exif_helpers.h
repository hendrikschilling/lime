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
#ifndef _EXIF_HELPERS_H
#define _EXIF_HELPERS_H

struct _lime_exif_handle;
  
typedef struct _lime_exif lime_exif; 

#ifdef __cplusplus
extern "C" {
#endif
  
lime_exif *lime_exif_handle_new_from_file(const char *path);
lime_exif *lime_exif_handle_destroy(lime_exif *h);
float lime_exif_handle_find_float_by_tagname(lime_exif *h, const char *tagname);
const char *lime_exif_handle_find_str_by_tagname(lime_exif *h, const char *tagname);
  
#ifdef __cplusplus
}
#endif

#endif