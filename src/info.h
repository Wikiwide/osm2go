/*
 * Copyright (C) 2008 Till Harbaum <till@harbaum.org>.
 *
 * This file is part of OSM2Go.
 *
 * OSM2Go is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OSM2Go is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OSM2Go.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INFO_H
#define INFO_H

typedef struct {
  appdata_t *appdata;
  GtkWidget *dialog, *view;
  GtkListStore *store;
  GtkWidget *but_add, *but_remove, *but_edit;
  type_t type;
  tag_t **tag;
} tag_context_t;


void info_dialog(GtkWidget *parent, appdata_t *appdata, relation_t *relation);
void info_tags_replace(tag_context_t *context);
gboolean info_tag_key_collision(tag_t *tags, tag_t *tag);

#endif // INFO_H
