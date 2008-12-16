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

/*
  http://josm.openstreetmap.de/svn/trunk/styles/standard/elemstyles.xml
*/

#include "appdata.h"

#include <libxml/parser.h>
#include <libxml/tree.h>

#ifndef LIBXML_TREE_ENABLED
#error "Tree not enabled in libxml"
#endif

// ratio conversions

// Scaling constants. Our "zoom" is a screenpx:canvasunit ratio, and the figure
// given by an elemstyles.xml is the denominator of a screen:real ratio.

#define N810_PX_PER_METRE (800 / 0.09)
    // XXX should probably ask the windowing system for DPI and 
    // work from that instead

inline float scaledn_to_zoom(const float scaledn) {
  return N810_PX_PER_METRE / scaledn;
}

inline float zoom_to_scaledn(const float zoom) {
  return N810_PX_PER_METRE / zoom;
}


/* --------------------- elemstyles.xml parsing ----------------------- */

gboolean parse_color(xmlNode *a_node, char *name, 
		     elemstyle_color_t *color) {
  char *color_str = (char*)xmlGetProp(a_node, BAD_CAST name);
  if(color_str) {
    GdkColor gdk_color;
    if(gdk_color_parse(color_str, &gdk_color)) {
      *color = 
	((gdk_color.red   << 8) & 0xff0000) |
	((gdk_color.green     ) & 0xff00) |
	((gdk_color.blue  >> 8) & 0xff);
      
      xmlFree(color_str);
      return TRUE;
    }
    xmlFree(color_str);
  }
  return FALSE;
}

static gboolean parse_gint(xmlNode *a_node, char *name, gint *val) {
  char *num_str = (char*)xmlGetProp(a_node, BAD_CAST name);
  if(num_str) {
    *val = strtoul(num_str, NULL, 10);
    xmlFree(num_str);
    return TRUE;
  }
  return FALSE;
}

static gboolean parse_scale_max(xmlNode *a_node, float *val) {
    xmlChar *val_str = xmlNodeGetContent(a_node);
    if (val_str) {
        *val = scaledn_to_zoom(strtod((char *)val_str, NULL));
        xmlFree(val_str);
        return TRUE;
    }
    return FALSE;
}

static gboolean parse_gboolean(xmlNode *a_node, char *name, gboolean *val) {
  char *bool_str = (char*)xmlGetProp(a_node, BAD_CAST name);
  if (!bool_str) {
    *val = FALSE;
    return FALSE;
  }
  static const char *true_str[]  = { "1", "yes", "true", 0 };
  int i;
  for (i=0; true_str[i]; ++i) {
    if (strcasecmp(bool_str, true_str[i])==0) {
      *val = TRUE;
      return TRUE;
    }
  }
  *val = FALSE;
  return TRUE;
}

static elemstyle_line_t *parse_line(xmlDocPtr doc, xmlNode *a_node) {
  elemstyle_line_t *line = g_new0(elemstyle_line_t, 1);

  /* these have to be present */
  g_assert(parse_color(a_node, "colour", &line->color));
  g_assert(parse_gint(a_node, "width", &line->width));

  line->real.valid = 
    parse_gint(a_node, "realwidth", &line->real.width);

  line->bg.valid = 
    parse_gint(a_node, "width_bg", &line->bg.width) &&
    parse_color(a_node, "colour_bg", &line->bg.color);

  parse_gboolean(a_node, "dashed", &line->dashed);

  return line;
}

/* parse "+123", "-123" and "123%" */
static void parse_width_mod(xmlNode *a_node, char *name, 
			    elemstyle_width_mod_t *value) {
  char *mod_str = (char*)xmlGetProp(a_node, BAD_CAST name);
  if(mod_str && strlen(mod_str) > 0) {
    if(mod_str[0] == '+') {
      value->mod = ES_MOD_ADD;
      value->width = strtoul(mod_str+1, NULL, 10);
    } else if(mod_str[0] == '-') {
      value->mod = ES_MOD_SUB;
      value->width = strtoul(mod_str+1, NULL, 10);
    } else if(mod_str[strlen(mod_str)-1] == '%') {
      value->mod = ES_MOD_PERCENT;
      value->width = strtoul(mod_str, NULL, 10);
    } else
      printf("warning: unable to parse modifier %s\n", mod_str);
  }
}

static elemstyle_line_mod_t *parse_line_mod(xmlDocPtr doc, xmlNode *a_node) {
  elemstyle_line_mod_t *line_mod = g_new0(elemstyle_line_mod_t, 1);

  parse_width_mod(a_node, "width", &line_mod->line);
  parse_width_mod(a_node, "width_bg", &line_mod->bg);

  return line_mod;
}

static elemstyle_area_t *parse_area(xmlDocPtr doc, xmlNode *a_node) {
  elemstyle_area_t *area = g_new0(elemstyle_area_t, 1);

  /* these have to be present */
  g_assert(parse_color(a_node, "colour", &area->color));
  return area;
}

static elemstyle_icon_t *parse_icon(xmlDocPtr doc, xmlNode *a_node) {
  elemstyle_icon_t *icon = g_new0(elemstyle_icon_t, 1);

  char *annotate = (char*)xmlGetProp(a_node, BAD_CAST "annotate");
  if(annotate) {
    icon->annotate = (strcasecmp(annotate, "true")==0);
    xmlFree(annotate);
  }

  icon->filename = (char*)xmlGetProp(a_node, BAD_CAST "src");
  g_assert(icon->filename);

  icon->filename = josm_icon_name_adjust(icon->filename);

  icon->zoom_max = 0;

  return icon;
}

static elemstyle_t *parse_rule(xmlDocPtr doc, xmlNode *a_node) {
  xmlNode *cur_node = NULL;
  elemstyle_t *elemstyle = g_new0(elemstyle_t, 1);

  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if(strcasecmp((char*)cur_node->name, "condition") == 0) {
	/* ------ parse condition ------ */
	elemstyle->condition.key   = (char*)xmlGetProp(cur_node, BAD_CAST "k");
	elemstyle->condition.value = (char*)xmlGetProp(cur_node, BAD_CAST "v");
	/* todo: add support for "b" (boolean) value */
      } else if(strcasecmp((char*)cur_node->name, "line") == 0) {
	/* ------ parse line ------ */
	g_assert(elemstyle->type == ES_TYPE_NONE);
	elemstyle->type = ES_TYPE_LINE;
	elemstyle->line = parse_line(doc, cur_node);
      } else if(strcasecmp((char*)cur_node->name, "linemod") == 0) {
	/* ------ parse linemod ------ */
	g_assert(elemstyle->type == ES_TYPE_NONE);
	elemstyle->type = ES_TYPE_LINE_MOD;
	elemstyle->line_mod = parse_line_mod(doc, cur_node);
      } else if(strcasecmp((char*)cur_node->name, "area") == 0) { 
	/* ------ parse area ------ */
	g_assert(elemstyle->type == ES_TYPE_NONE);
	elemstyle->type = ES_TYPE_AREA;
	elemstyle->area = parse_area(doc, cur_node);
      } else if(strcasecmp((char*)cur_node->name, "icon") == 0) { 
	elemstyle->icon = parse_icon(doc, cur_node);
      } else if(strcasecmp((char*)cur_node->name, "scale_min") == 0) { 
	/* scale_min is currently ignored */
      } else if(strcasecmp((char*)cur_node->name, "scale_max") == 0) {
	switch (elemstyle->type) {
	case ES_TYPE_LINE:
          parse_scale_max(cur_node, &elemstyle->line->zoom_max);
	  break;
	case ES_TYPE_AREA:
          parse_scale_max(cur_node, &elemstyle->area->zoom_max);
	  break;
	default:
	  if (elemstyle->icon) {
            parse_scale_max(cur_node, &elemstyle->icon->zoom_max);
	  }
	  else {
	    printf("scale_max for unhandled elemstyletype=0x02%x\n", 
		   elemstyle->type);
	  }
	  break;
	}
      } else {
	printf("found unhandled rules/rule/%s\n", cur_node->name);
      }
    }
  }  

  return elemstyle;
}

static elemstyle_t *parse_rules(xmlDocPtr doc, xmlNode *a_node) {
  xmlNode *cur_node = NULL;
  elemstyle_t *elemstyles = NULL, **elemstyle = &elemstyles;

  for (cur_node = a_node->children; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if(strcasecmp((char*)cur_node->name, "rule") == 0) {
	*elemstyle = parse_rule(doc, cur_node);
	if(*elemstyle) elemstyle = &((*elemstyle)->next);
      } else
	printf("found unhandled rules/%s\n", cur_node->name);
    }
  }  
  return elemstyles;
}

static elemstyle_t *parse_doc(xmlDocPtr doc) {
  /* Get the root element node */
  xmlNode *cur_node = NULL;
  elemstyle_t *elemstyles = NULL; 

  for(cur_node = xmlDocGetRootElement(doc); 
      cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if(strcasecmp((char*)cur_node->name, "rules") == 0) {
	elemstyles = parse_rules(doc, cur_node);
      } else 
	printf("found unhandled %s\n", cur_node->name);
    }
  }
    
  xmlFreeDoc(doc);
  xmlCleanupParser();
  return elemstyles;
}

elemstyle_t *josm_elemstyles_load(char *name) {
  elemstyle_t *elemstyles = NULL;

  printf("Loading JOSM elemstyles ...\n");

  char *filename = find_file(name);
  if(!filename) {
    printf("elemstyle file not found\n");
    return NULL;
  }
    
  LIBXML_TEST_VERSION;

  /* parse the file and get the DOM */
  xmlDoc *doc = NULL;
  if((doc = xmlReadFile(filename, NULL, 0)) == NULL) {
    xmlErrorPtr errP = xmlGetLastError();
    printf("elemstyles download failed: "
	   "XML error while parsing:\n"
	   "%s\n", errP->message);
  } else {
    printf("ok, parse doc tree\n");
    elemstyles = parse_doc(doc);
  }

  g_free(filename);
  return elemstyles;
}

/* ----------------------- cleaning up --------------------- */

static void free_line(elemstyle_line_t *line) {
  g_free(line);
}

static void free_line_mod(elemstyle_line_mod_t *line_mod) {
  g_free(line_mod);
}

static void free_area(elemstyle_area_t *area) {
  g_free(area);
}

static void free_icon(elemstyle_icon_t *icon) {
  if(icon->filename) xmlFree(icon->filename);
  g_free(icon);
}

static void elemstyle_free(elemstyle_t *elemstyle) {
  if(elemstyle->condition.key)   xmlFree(elemstyle->condition.key);
  if(elemstyle->condition.value) xmlFree(elemstyle->condition.value);

  switch(elemstyle->type) {
  case ES_TYPE_NONE:
    break;
  case ES_TYPE_LINE:
    free_line(elemstyle->line);
    break;
  case ES_TYPE_AREA:
    free_area(elemstyle->area);
    break;
  case ES_TYPE_LINE_MOD:
    free_line_mod(elemstyle->line_mod);
    break;
  }
  if(elemstyle->icon) free_icon(elemstyle->icon);
  g_free(elemstyle);
}

void josm_elemstyles_free(elemstyle_t *elemstyles) {
  while(elemstyles) {
    elemstyle_t *next = elemstyles->next;
    elemstyle_free(elemstyles);
    elemstyles = next;
  }
}

#define WIDTH_SCALE (1.0)

void josm_elemstyles_colorize_node(style_t *style, node_t *node) {
  node->zoom_max = style->node.zoom_max;
  elemstyle_t *elemstyle = style->elemstyles;

  while(elemstyle) {
    gboolean match = FALSE;
    
    if(elemstyle->condition.key) {
      char *value = osm_node_get_value(node, elemstyle->condition.key);
      if(value) {
	if(elemstyle->condition.value) {
	  if(strcasecmp(value, elemstyle->condition.value) == 0)
	    match = TRUE;
	} else
	  match = TRUE;
      }
    } else
      if(osm_node_has_value(node, elemstyle->condition.value)) 
	match = TRUE;
    

    if(match && elemstyle->icon) {
      char *name = g_strdup_printf("styles/%s/%s",
                   style->icon.path_prefix,
				   elemstyle->icon->filename);

      /* free old icon if there's one present */
      if(node->icon_buf)
	icon_free(style->iconP, node->icon_buf);

      node->icon_buf = icon_load(style->iconP, name);
      g_free(name);

      if (elemstyle->icon->zoom_max > 0) {
        node->zoom_max = elemstyle->icon->zoom_max;
      }
    }
    
    elemstyle = elemstyle->next;
  }
}

static void line_mod_apply(gint *width, elemstyle_width_mod_t *mod) {
  switch(mod->mod) {
  case ES_MOD_NONE:
    break;
    
  case ES_MOD_ADD:
    *width += mod->width;
    break;
    
  case ES_MOD_SUB:
    *width -= mod->width;
    break;
      
  case ES_MOD_PERCENT:
    *width = 100 * *width / mod->width;
    break;
  }
}

void josm_elemstyles_colorize_way(style_t *style, way_t *way) {
  elemstyle_t *elemstyle = style->elemstyles;

  /* use dark grey/no stroke/not filled for everything unknown */
  way->draw.color = RGB2CANVAS(style->way.color);
  way->draw.width = style->way.width;
  way->draw.flags = 0;
  way->draw.zoom_max = 0;   // draw at all zoom levels

  /* during the elemstyle search a line_mod may be found. save it here */
  elemstyle_line_mod_t *line_mod = NULL;
  
  gboolean way_processed = FALSE;
  gboolean way_is_closed = 
    (osm_way_get_last_node(way) == osm_way_get_first_node(way));

  while(elemstyle) {
    //  printf("a %s %s\n", elemstyle->condition.key, 
    //                        elemstyle->condition.value);

    gboolean match = FALSE;

    if(elemstyle->condition.key) {
      char *value = osm_way_get_value(way, elemstyle->condition.key);
      if(value) {
	if(elemstyle->condition.value) {
	  if(strcasecmp(value, elemstyle->condition.value) == 0)
	    match = TRUE;
	} else
	  match = TRUE;
      }
    } else
      if(osm_way_has_value(way, elemstyle->condition.value)) 
	match = TRUE;

    if(match) {
      switch(elemstyle->type) {
      case ES_TYPE_NONE:
	/* this entry does not contain line or area descriptions and is */
	/* likely just an icon. ignore this as it doesn't make much sense */
	/* for a way */
	break;

      case ES_TYPE_LINE:
	if(!way_processed) {
	  way->draw.color = (elemstyle->line->color << 8) | 0xff;
	  way->draw.width =  WIDTH_SCALE * elemstyle->line->width;
	  if(elemstyle->line->bg.valid) {
	    way->draw.flags |= OSM_DRAW_FLAG_BG;
	    way->draw.bg.color = (elemstyle->line->bg.color << 8) | 0xff;
	    way->draw.bg.width =  WIDTH_SCALE * elemstyle->line->bg.width;
	  }
	  if (elemstyle->line->zoom_max > 0) {
	    way->draw.zoom_max = elemstyle->line->zoom_max;
	  }
	  else {
	    way->draw.zoom_max = style->way.zoom_max;
	  }
	  way->draw.dashed = elemstyle->line->dashed;
	  way_processed = TRUE;
	}
	break;

      case ES_TYPE_LINE_MOD:
	/* just save the fact that a line mod was found for later */
	line_mod = elemstyle->line_mod;
	break;

      case ES_TYPE_AREA:
	if(way_is_closed && !way_processed) {
	  way->draw.flags |= OSM_DRAW_FLAG_AREA;
	  /* comment the following line for grey border around all areas */
	  /* (potlatch style) */

	  if(style->area.has_border_color)
	    way->draw.color =  (style->area.border_color << 8) | 0xff;
	  else
	    way->draw.color =  (elemstyle->area->color << 8) | 0xff;

	  way->draw.width =  WIDTH_SCALE * style->area.border_width;
	  way->draw.area.color = (elemstyle->area->color << 8) | 
	                        style->area.opaque;
	  if (elemstyle->area->zoom_max > 0) {
	    way->draw.zoom_max = elemstyle->area->zoom_max;
	  }
	  else {
	    way->draw.zoom_max = style->area.zoom_max;
	  }
	  way_processed = TRUE;
	}
	break;
      }
    }
    elemstyle = elemstyle->next;
  }

  /* apply the last line mod entry that has been found during search */
  if(line_mod) {
    printf("applying last matching line mod to way #%ld\n", way->id);
    line_mod_apply(&way->draw.width, &line_mod->line);

    /* special case: the way does not have a background, but it is to */
    /* be modified */
    if((line_mod->bg.mod != ES_MOD_NONE) && 
       (!(way->draw.flags & OSM_DRAW_FLAG_BG))) {
      printf("forcing background\n");

      /* add a background in black color */
      way->draw.flags |= OSM_DRAW_FLAG_BG;
      way->draw.bg.color = (0) | 0xff;
      way->draw.bg.width =  way->draw.width;
    }

    line_mod_apply(&way->draw.bg.width, &line_mod->bg);
  }
}

 void josm_elemstyles_colorize_world(style_t *styles, osm_t *osm) {

  printf("preparing colors\n");

  /* colorize ways */
  way_t *way = osm->way;
  while(way) {
    josm_elemstyles_colorize_way(styles, way);
    way = way->next;
  }

  /* icons */
  node_t *node = osm->node;
  while(node) {
    josm_elemstyles_colorize_node(styles, node);
    node = node->next;
  }
}

// vim:et:ts=8:sw=2:sts=2:ai
