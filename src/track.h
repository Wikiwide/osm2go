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

#ifndef TRACK_H
#define TRACK_H

typedef struct track_point_s {
  pos_t pos;               /* position in lat/lon format */
  time_t time;
  float altitude;
  struct track_point_s *next;
} track_point_t;

typedef struct track_item_chain_s {
  canvas_item_t *item;
  struct track_item_chain_s *next;
} track_item_chain_t;

typedef struct track_seg_s {
  track_point_t *track_point;
  struct track_seg_s *next;
  track_item_chain_t *item_chain;
} track_seg_t;

typedef struct track_s {
  track_seg_t *track_seg;
  gboolean dirty;
  track_seg_t *cur_seg;
} track_t;

gint track_seg_points(track_seg_t *seg);

/* used internally to save and restore the currently displayed track */
void track_save(project_t *project, track_t *track);
track_t *track_restore(appdata_t *appdata, project_t *project);

/* accessible via the menu */
void track_clear(appdata_t *appdata, track_t *track);
void track_export(appdata_t *appdata, char *filename);
track_t *track_import(appdata_t *appdata, char *filename);

void track_enable_gps(appdata_t *appdata, gboolean enable);

#endif // TRACK_H
