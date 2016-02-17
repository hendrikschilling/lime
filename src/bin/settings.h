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
#ifndef _LIMEVIEW_SETTINGS_H
#define _LIMEVIEW_SETTINGS_H

#include <Eina.h>

typedef struct {
  uint32_t version;
  Eina_List *default_fc_rules;
  uint32_t cache_size; //cache size in MiB
  uint32_t high_quality_delay; //in ms
} Limeview_Settings;

typedef struct {
  char *cam;
  char *format;
  char *fc;
} Default_Fc_Rule;

void lv_settings_init(void);
void lv_settings_shutdown(void);

Limeview_Settings *lv_setting_load(void);
void lv_setting_save(Limeview_Settings *s);

#endif