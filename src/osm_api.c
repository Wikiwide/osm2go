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

#include "appdata.h"

#include <curl/curl.h>
#include <curl/types.h> /* new for v7 */
#include <curl/easy.h> /* new for v7 */
#include <unistd.h>

static struct http_message_s {
  int id;
  char *msg;
} http_messages [] = {
  { 200, "Ok" },
  { 400, "Bad Request" },
  { 401, "Unauthorized" },
  { 403, "Forbidden" },
  { 404, "Not Found" },
  { 405, "Method Not Allowed" },
  { 410, "Gone" },
  { 412, "Precondition Failed" },
  { 417, "(Expect rejected)" },
  { 500, "Internal Server Error" },
  { 503, "Service Unavailable" },
  { 0,   NULL }
};

static char *osm_http_message(int id) {
  struct http_message_s *msg = http_messages;

  while(msg->id) {
    if(msg->id == id) return _(msg->msg);
    msg++;
  }

  return NULL;
}

typedef struct {
  appdata_t *appdata;
  GtkWidget *dialog;
  osm_t *osm;
  project_t *project;

  struct log_s {
    GtkTextBuffer *buffer;
    GtkWidget *view;
  } log;

} osm_upload_context_t;

gboolean osm_download(GtkWidget *parent, project_t *project) {
  printf("download osm ...\n");

  char minlon[G_ASCII_DTOSTR_BUF_SIZE], minlat[G_ASCII_DTOSTR_BUF_SIZE];
  char maxlon[G_ASCII_DTOSTR_BUF_SIZE], maxlat[G_ASCII_DTOSTR_BUF_SIZE];

  g_ascii_dtostr(minlon, sizeof(minlon), project->min.lon);
  g_ascii_dtostr(minlat, sizeof(minlat), project->min.lat);
  g_ascii_dtostr(maxlon, sizeof(maxlon), project->max.lon);
  g_ascii_dtostr(maxlat, sizeof(maxlat), project->max.lat);

  char *url = g_strdup_printf("%s/map?bbox=%s,%s,%s,%s",
		project->server, minlon, minlat, maxlon, maxlat);

  gboolean result = net_io_download_file(parent, url, project->osm);

  g_free(url);
  return result;
}

typedef struct {
  char *ptr;
  int len;
} curl_data_t;

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
  curl_data_t *p = (curl_data_t*)stream;

  //  printf("request to read %d items of size %d, pointer = %p\n", 
  //  nmemb, size, p->ptr);

  if(nmemb*size > p->len) 
    nmemb = p->len/size;
  
  memcpy(ptr, p->ptr, size*nmemb);
  p->ptr += size*nmemb;
  p->len -= size*nmemb;

  return nmemb;
}

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
  curl_data_t *p = (curl_data_t*)stream;

  p->ptr = g_realloc(p->ptr, p->len + size*nmemb + 1);
  if(p->ptr) {
    memcpy(p->ptr+p->len, ptr, size*nmemb);
    p->len += size*nmemb;
    p->ptr[p->len] = 0;
  }
  return nmemb;
}

static void appendf(struct log_s *log, const char *fmt, ...) {
  va_list args;
  va_start( args, fmt );
  char *buf = g_strdup_vprintf(fmt, args);
  va_end( args );

  GtkTextIter end;
  gtk_text_buffer_get_end_iter(log->buffer, &end);
  gtk_text_buffer_insert(log->buffer, &end, buf, -1);

  g_free(buf);

  gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(log->view),
			       &end, 0.0, FALSE, 0, 0); 

  while(gtk_events_pending())
    gtk_main_iteration();
}

#define MAX_TRY 5

static gboolean osm_update_item(struct log_s *log, char *xml_str, 
			    char *url, char *user, item_id_t *id) {
  int retry = MAX_TRY;
  char buffer[CURL_ERROR_SIZE];

  CURL *curl;
  CURLcode res;

  curl_data_t read_data;
  curl_data_t write_data;

  while(retry >= 0) {

    if(retry != MAX_TRY)
      appendf(log, _("Retry %d/%d "), MAX_TRY-retry, MAX_TRY-1);

    /* get a curl handle */
    curl = curl_easy_init();
    if(!curl) {
      appendf(log, _("CURL init error\n"));
      return FALSE;
    }

    read_data.ptr = xml_str;
    read_data.len = strlen(xml_str);
    write_data.ptr = NULL;
    write_data.len = 0;	    

    /* we want to use our own read/write functions */
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    
    /* enable uploading */
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    
    /* specify target URL, and note that this URL should include a file
       name, not only a directory */
    curl_easy_setopt(curl, CURLOPT_URL, url);
    
    /* now specify which file to upload */
    curl_easy_setopt(curl, CURLOPT_READDATA, &read_data);
    
    /* provide the size of the upload, we specicially typecast the value
       to curl_off_t since we must be sure to use the correct data size */
    curl_easy_setopt(curl, CURLOPT_INFILESIZE, (long)strlen(xml_str));
    
    /* we pass our 'chunk' struct to the callback function */
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_data);
    
    /* some servers don't like requests that are made without a user-agent
       field, so we provide one */
    curl_easy_setopt(curl, CURLOPT_USERAGENT, 
		     PACKAGE "-libcurl/" VERSION); 
    
    struct curl_slist *slist=NULL;
    slist = curl_slist_append(slist, "Expect:");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
    
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, buffer);
    
    /* set user name and password for the authentication */
    curl_easy_setopt(curl, CURLOPT_USERPWD, user);
    
    /* Now run off and do what you've been told! */
    res = curl_easy_perform(curl);
    
    long response;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
    
    /* always cleanup */
    curl_slist_free_all(slist);
    curl_easy_cleanup(curl);
    
    printf("reply is \"%s\"\n", write_data.ptr);
    
    /* this will return the id on a successful create */
    if(id && (res == 0) && (response == 200)) {
      printf("request to parse successful reply as an id\n");
      *id = strtoul(write_data.ptr, NULL, 10);
    }
    
    g_free(write_data.ptr);
    
    if(res != 0) 
      appendf(log, _("failed: %s\n"), buffer);
    else if(response != 200)
      appendf(log, _("failed, code: %ld %s\n"), 
	      response, osm_http_message(response));
    else {
      if(!id) appendf(log, _("ok\n"));
      else    appendf(log, _("ok: #%ld\n"), *id);
    }
    
    /* don't retry unless we had an "internal server error" */
    if(response != 500) 
      return((res == 0)&&(response == 200));

    retry--;
  }

  return FALSE;
}

static gboolean osm_delete_item(struct log_s *log, char *url, char *user) {
  int retry = MAX_TRY;
  char buffer[CURL_ERROR_SIZE];

  CURL *curl;
  CURLcode res;

  while(retry >= 0) {

    if(retry != MAX_TRY)
      appendf(log, _("Retry %d/%d "), MAX_TRY-retry, MAX_TRY-1);

    /* get a curl handle */
    curl = curl_easy_init();
    if(!curl) {
      appendf(log, _("CURL init error\n"));
      return FALSE;
    }

    /* no read/write functions required */
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE"); 
  
    /* specify target URL, and note that this URL should include a file
       name, not only a directory */
    curl_easy_setopt(curl, CURLOPT_URL, url);
    
    /* some servers don't like requests that are made without a user-agent
       field, so we provide one */
    curl_easy_setopt(curl, CURLOPT_USERAGENT, PACKAGE "-libcurl/" VERSION); 
    
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, buffer);
    
    /* set user name and password for the authentication */
    curl_easy_setopt(curl, CURLOPT_USERPWD, user);
    
    /* Now run off and do what you've been told! */
    res = curl_easy_perform(curl);
    
    long response;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
    
    /* always cleanup */
    curl_easy_cleanup(curl);
    
    if(res != 0) 
    appendf(log, _("failed: %s\n"), buffer);
    else if(response != 200)
      appendf(log, _("failed, code: %ld %s\n"), 
	      response, osm_http_message(response));
    else
      appendf(log, _("ok\n"));
    
    /* don't retry unless we had an "internal server error" */
    if(response != 500) 
      return((res == 0)&&(response == 200));
    
    retry--;
  }
  
  return FALSE;
}

typedef struct {
  struct {
    int total, new, dirty, deleted;
  } ways, nodes, relations;
} osm_dirty_t;

static GtkWidget *table_attach_label_c(GtkWidget *table, char *str, 
				       int x1, int x2, int y1, int y2) {
  GtkWidget *label =  gtk_label_new(str);
  gtk_table_attach_defaults(GTK_TABLE(table), label, x1, x2, y1, y2);
  return label;
}

static GtkWidget *table_attach_label_l(GtkWidget *table, char *str, 
				       int x1, int x2, int y1, int y2) {
  GtkWidget *label = table_attach_label_c(table, str, x1, x2, y1, y2); 
  gtk_misc_set_alignment(GTK_MISC(label), 0.f, 0.5f);
  return label;
}

static GtkWidget *table_attach_int(GtkWidget *table, int num, 
				       int x1, int x2, int y1, int y2) {
  char *str = g_strdup_printf("%d", num);
  GtkWidget *label = table_attach_label_c(table, str, x1, x2, y1, y2); 
  g_free(str);
  return label;
}

static void osm_delete_nodes(osm_upload_context_t *context) {
  node_t *node = context->osm->node;
  project_t *project = context->project;

  while(node) {
    /* make sure gui gets updated */
    while(gtk_events_pending()) gtk_main_iteration();

    if(node->flags & OSM_FLAG_DELETED) {
      printf("deleting node on server\n");

      appendf(&context->log, _("Delete node #%ld "), node->id);

      char *url = g_strdup_printf("%s/node/%lu", project->server, node->id);
      char *cred = g_strdup_printf("%s:%s", 
				   context->appdata->settings->username, 
				   context->appdata->settings->password);

      if(osm_delete_item(&context->log, url, cred)) {
	node->flags &= ~(OSM_FLAG_DIRTY | OSM_FLAG_DELETED);
	project->data_dirty = TRUE;
      }
      
      g_free(cred);
    }
    node = node->next;
  }
}

static void osm_upload_nodes(osm_upload_context_t *context) {
  node_t *node = context->osm->node;
  project_t *project = context->project;

  while(node) {
    /* make sure gui gets updated */
    while(gtk_events_pending()) gtk_main_iteration();

    if((node->flags & (OSM_FLAG_DIRTY | OSM_FLAG_NEW)) &&
       (!(node->flags & OSM_FLAG_DELETED))) {
      char *url = NULL;

      if(node->flags & OSM_FLAG_NEW) {
	url = g_strdup_printf("%s/node/create", project->server);
	appendf(&context->log, _("New node "));
      } else {
	url = g_strdup_printf("%s/node/%lu", project->server, node->id);
	appendf(&context->log, _("Modified node #%ld "), node->id);
      }

      /* upload this node */
      char *xml_str = osm_generate_xml_node(context->osm, node);
      if(xml_str) {
	printf("uploading node %s from address %p\n", url, xml_str);

	char *cred = g_strdup_printf("%s:%s", 
				     context->appdata->settings->username, context->appdata->settings->password);
	if(osm_update_item(&context->log, xml_str, url, cred, 
			   (node->flags & OSM_FLAG_NEW)?&(node->id):NULL)) {

	  node->flags &= ~(OSM_FLAG_DIRTY | OSM_FLAG_NEW);
	  project->data_dirty = TRUE;
	}
	g_free(cred);
      }
      g_free(url);
    }
    node = node->next;
  }
}

static void osm_delete_ways(osm_upload_context_t *context) {
  way_t *way = context->osm->way;
  project_t *project = context->project;

  while(way) {
    /* make sure gui gets updated */
    while(gtk_events_pending()) gtk_main_iteration();

    if(way->flags & OSM_FLAG_DELETED) {
      printf("deleting way on server\n");

      appendf(&context->log, _("Delete way #%ld "), way->id);

      char *url = g_strdup_printf("%s/way/%lu", project->server, way->id);
      char *cred = g_strdup_printf("%s:%s", 
				   context->appdata->settings->username, context->appdata->settings->password);

      if(osm_delete_item(&context->log, url, cred)) {
	way->flags &= ~(OSM_FLAG_DIRTY | OSM_FLAG_DELETED);
	project->data_dirty = TRUE;
      }
      
      g_free(cred);
    }
    way = way->next;
  }
}


static void osm_upload_ways(osm_upload_context_t *context) {
  way_t *way = context->osm->way;
  project_t *project = context->project;

  while(way) {
    /* make sure gui gets updated */
    while(gtk_events_pending()) gtk_main_iteration();

    if((way->flags & (OSM_FLAG_DIRTY | OSM_FLAG_NEW)) &&
       (!(way->flags & OSM_FLAG_DELETED))) {
      char *url = NULL;
      
      if(way->flags & OSM_FLAG_NEW) {
	url = g_strdup_printf("%s/way/create", project->server);
	appendf(&context->log, _("New way "));
      } else {
	url = g_strdup_printf("%s/way/%lu", project->server, way->id);
	appendf(&context->log, _("Modified way #%ld "), way->id);
      }
      
      /* upload this node */
      char *xml_str = osm_generate_xml_way(context->osm, way);
      if(xml_str) {
	printf("uploading way %s from address %p\n", url, xml_str);
	
	char *cred = g_strdup_printf("%s:%s", 
				     context->appdata->settings->username, 
				     context->appdata->settings->password);
	if(osm_update_item(&context->log, xml_str, url, cred, 
			   (way->flags & OSM_FLAG_NEW)?&(way->id):NULL)) {
	  way->flags &= ~(OSM_FLAG_DIRTY | OSM_FLAG_NEW);
	  project->data_dirty = TRUE;
	}
	g_free(cred);
      }
      g_free(url);
    }
    way = way->next;
  }
}

static void osm_delete_relations(osm_upload_context_t *context) {
  relation_t *relation = context->osm->relation;
  project_t *project = context->project;

  while(relation) {
    /* make sure gui gets updated */
    while(gtk_events_pending()) gtk_main_iteration();

    if(relation->flags & OSM_FLAG_DELETED) {
      printf("deleting relation on server\n");

      appendf(&context->log, _("Delete relation #%ld "), relation->id);

      char *url = g_strdup_printf("%s/relation/%lu", 
				  project->server, relation->id);
      char *cred = g_strdup_printf("%s:%s", 
				   context->appdata->settings->username, 
				   context->appdata->settings->password);

      if(osm_delete_item(&context->log, url, cred)) {
	relation->flags &= ~(OSM_FLAG_DIRTY | OSM_FLAG_DELETED);
	project->data_dirty = TRUE;
      }
      
      g_free(cred);
    }
    relation = relation->next;
  }
}


static void osm_upload_relations(osm_upload_context_t *context) {
  relation_t *relation = context->osm->relation;
  project_t *project = context->project;

  while(relation) {
    /* make sure gui gets updated */
    while(gtk_events_pending()) gtk_main_iteration();

    if((relation->flags & (OSM_FLAG_DIRTY | OSM_FLAG_NEW)) &&
       (!(relation->flags & OSM_FLAG_DELETED))) {
      char *url = NULL;
      
      if(relation->flags & OSM_FLAG_NEW) {
	url = g_strdup_printf("%s/relation/create", project->server);
	appendf(&context->log, _("New relation "));
      } else {
	url = g_strdup_printf("%s/relation/%lu", project->server,relation->id);
	appendf(&context->log, _("Modified relation #%ld "), relation->id);
      }
      
      /* upload this relation */
      char *xml_str = osm_generate_xml_relation(context->osm, relation);
      if(xml_str) {
	printf("uploading relation %s from address %p\n", url, xml_str);
	
	char *cred = g_strdup_printf("%s:%s", 
				     context->appdata->settings->username, context->appdata->settings->password);
	if(osm_update_item(&context->log, xml_str, url, cred, 
		   (relation->flags & OSM_FLAG_NEW)?&(relation->id):NULL)) {
	  relation->flags &= ~(OSM_FLAG_DIRTY | OSM_FLAG_NEW);
	  project->data_dirty = TRUE;
	}
	g_free(cred);
      }
      g_free(url);
    }
    relation = relation->next;
  }
}


void osm_upload(appdata_t *appdata, osm_t *osm, project_t *project) {

  printf("starting upload\n");

  /* upload config and confirmation dialog */

  /* count nodes */
  osm_dirty_t dirty;
  memset(&dirty, 0, sizeof(osm_dirty_t));

  node_t *node = osm->node;
  while(node) {
    dirty.nodes.total++;
    if(node->flags & OSM_FLAG_DELETED)     dirty.nodes.deleted++;
    else if(node->flags & OSM_FLAG_NEW)    dirty.nodes.new++;
    else if(node->flags & OSM_FLAG_DIRTY)  dirty.nodes.dirty++;

    node = node->next;
  }
  printf("nodes:     new %2d, dirty %2d, deleted %2d\n",
	 dirty.nodes.new, dirty.nodes.dirty, dirty.nodes.deleted);
  
  /* count ways */
  way_t *way = osm->way;
  while(way) {
    dirty.ways.total++;
    if(way->flags & OSM_FLAG_DELETED)      dirty.ways.deleted++;
    else if(way->flags & OSM_FLAG_NEW)     dirty.ways.new++;
    else if(way->flags & OSM_FLAG_DIRTY)   dirty.ways.dirty++;

    way = way->next;
  }
  printf("ways:      new %2d, dirty %2d, deleted %2d\n",
	 dirty.ways.new, dirty.ways.dirty, dirty.ways.deleted);

  /* count relations */
  relation_t *relation = osm->relation;
  while(relation) {
    dirty.relations.total++;
    if(relation->flags & OSM_FLAG_DELETED)      dirty.relations.deleted++;
    else if(relation->flags & OSM_FLAG_NEW)     dirty.relations.new++;
    else if(relation->flags & OSM_FLAG_DIRTY)   dirty.relations.dirty++;

    relation = relation->next;
  }
  printf("relations: new %2d, dirty %2d, deleted %2d\n",
	 dirty.relations.new, dirty.relations.dirty, dirty.relations.deleted);


  GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Upload to OSM"),
	  GTK_WINDOW(appdata->window), GTK_DIALOG_MODAL,
	  GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT, 
          GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
	  NULL);

  GtkWidget *table = gtk_table_new(4, 5, TRUE); 

  table_attach_label_c(table, _("Total"),          1, 2, 0, 1);
  table_attach_label_c(table, _("New"),            2, 3, 0, 1);
  table_attach_label_c(table, _("Modified"),       3, 4, 0, 1);
  table_attach_label_c(table, _("Deleted"),        4, 5, 0, 1);

  table_attach_label_l(table, _("Nodes:"),         0, 1, 1, 2);
  table_attach_int(table, dirty.nodes.total,       1, 2, 1, 2);
  table_attach_int(table, dirty.nodes.new,         2, 3, 1, 2);
  table_attach_int(table, dirty.nodes.dirty,       3, 4, 1, 2);
  table_attach_int(table, dirty.nodes.deleted,     4, 5, 1, 2);

  table_attach_label_l(table, _("Ways:"),          0, 1, 2, 3);
  table_attach_int(table, dirty.ways.total,        1, 2, 2, 3);
  table_attach_int(table, dirty.ways.new,          2, 3, 2, 3);
  table_attach_int(table, dirty.ways.dirty,        3, 4, 2, 3);
  table_attach_int(table, dirty.ways.deleted,      4, 5, 2, 3);

  table_attach_label_l(table, _("Relations:"),     0, 1, 3, 4);
  table_attach_int(table, dirty.relations.total,   1, 2, 3, 4);
  table_attach_int(table, dirty.relations.new,     2, 3, 3, 4);
  table_attach_int(table, dirty.relations.dirty,   3, 4, 3, 4);
  table_attach_int(table, dirty.relations.deleted, 4, 5, 3, 4);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(dialog)->vbox), table);

  /* ------------------------------------------------------ */

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
			      gtk_hseparator_new());

  /* ------- add username and password entries ------------ */

  table = gtk_table_new(2, 2, FALSE);
  table_attach_label_l(table, _("Username:"), 0, 1, 0, 1);
  GtkWidget *uentry = gtk_entry_new();
  HILDON_ENTRY_NO_AUTOCAP(uentry);
  gtk_entry_set_text(GTK_ENTRY(uentry), appdata->settings->username);
  gtk_table_attach_defaults(GTK_TABLE(table),  uentry, 1, 2, 0, 1);
  table_attach_label_l(table, _("Password:"), 0, 1, 1, 2);
  GtkWidget *pentry = gtk_entry_new();
  HILDON_ENTRY_NO_AUTOCAP(pentry);
  gtk_entry_set_text(GTK_ENTRY(pentry), appdata->settings->password);
  gtk_entry_set_visibility(GTK_ENTRY(pentry), FALSE);
  gtk_table_attach_defaults(GTK_TABLE(table),  pentry, 1, 2, 1, 2);
  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(dialog)->vbox), table);

  gtk_widget_show_all(dialog);

  if(GTK_RESPONSE_ACCEPT != gtk_dialog_run(GTK_DIALOG(dialog))) {
    printf("upload cancelled\n");
    gtk_widget_destroy(dialog);
    return;
  }

  printf("clicked ok\n");

  /* retrieve username and password */
  if(appdata->settings->username) 
    g_free(appdata->settings->username);
  appdata->settings->username = 
    g_strdup(gtk_entry_get_text(GTK_ENTRY(uentry)));

  if(appdata->settings->password) 
    g_free(appdata->settings->password);
  appdata->settings->password = 
    g_strdup(gtk_entry_get_text(GTK_ENTRY(pentry)));

  gtk_widget_destroy(dialog);
  project_save(GTK_WIDGET(appdata->window), project);

  /* osm upload itself also has a gui */
  osm_upload_context_t *context = g_new0(osm_upload_context_t, 1);
  context->appdata = appdata;
  context->osm = osm;
  context->project = project;

  context->dialog = gtk_dialog_new_with_buttons(_("Upload to OSM"),
	  GTK_WINDOW(appdata->window), GTK_DIALOG_MODAL,
	  GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, NULL);
  gtk_dialog_set_response_sensitive(GTK_DIALOG(context->dialog), 
				    GTK_RESPONSE_CLOSE, FALSE);

  /* making the dialog a little wider makes it less "crowded" */
#ifndef USE_HILDON
  gtk_window_set_default_size(GTK_WINDOW(context->dialog), 480, 256);
#else
  gtk_window_set_default_size(GTK_WINDOW(context->dialog), 800, 480);
#endif

  /* ------- main ui elelent is this text view --------------- */

  GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), 
  				 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  context->log.buffer = gtk_text_buffer_new(NULL);

  context->log.view = gtk_text_view_new_with_buffer(context->log.buffer);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(context->log.view), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(context->log.view), FALSE);

  gtk_container_add(GTK_CONTAINER(scrolled_window), context->log.view);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window),
				      GTK_SHADOW_IN);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(context->dialog)->vbox), 
			       scrolled_window);
  gtk_widget_show_all(context->dialog);

  appendf(&context->log, _("Log generated by %s v%s using API 0.5\n"), 
	  PACKAGE, VERSION);
  appendf(&context->log, _("Uploading to %s\n"), project->server);

  /* check for dirty entries */
  appendf(&context->log, _("Uploading nodes:\n"));
  osm_upload_nodes(context);
  appendf(&context->log, _("Uploading ways:\n"));
  osm_upload_ways(context);
  appendf(&context->log, _("Uploading relations:\n"));
  osm_upload_relations(context);
  appendf(&context->log, _("Deleting relations:\n"));
  osm_delete_relations(context);
  appendf(&context->log, _("Deleting ways:\n"));
  osm_delete_ways(context);
  appendf(&context->log, _("Deleting nodes:\n"));
  osm_delete_nodes(context);

  appendf(&context->log, _("Upload done.\n"));

  gboolean reload_map = FALSE;
  if(project->data_dirty) {
    appendf(&context->log, _("Server data has been modified.\n"));
    appendf(&context->log, _("Downloading updated osm data ...\n"));

    if(osm_download(context->dialog, project)) {
      appendf(&context->log, _("Download successful!\n"));
      appendf(&context->log, _("The map will be reloaded.\n"));
      project->data_dirty = FALSE;
      reload_map = TRUE;
    } else
      appendf(&context->log, _("Download failed!\n"));

    project_save(context->dialog, project);

    if(reload_map) {
      /* this kind of rather brute force reload is useful as the moment */
      /* after the upload is a nice moment to bring everything in sync again. */
      /* we basically restart the entire map with fresh data from the server */
      /* and the diff will hopefully be empty (if the upload was successful) */

      appendf(&context->log, _("Reloading map ...\n"));
      
      if(!diff_is_clean(appdata->osm, FALSE)) {
	appendf(&context->log, _(">>>>>>>> DIFF IS NOT CLEAN <<<<<<<<\n"));
	appendf(&context->log, _("Something went wrong during upload,\n"));
	appendf(&context->log, _("proceed with care!\n"));
      }

      /* redraw the entire map by destroying all map items and redrawing them */
      appendf(&context->log, _("Cleaning up ...\n"));
      diff_save(appdata->project, appdata->osm);
      map_clear(appdata, MAP_LAYER_OBJECTS_ONLY);
      osm_free(&appdata->icon, appdata->osm);
      
      appendf(&context->log, _("Loading OSM ...\n"));
      appdata->osm = osm_parse(appdata->project->osm);
      appendf(&context->log, _("Applying diff ...\n"));
      diff_restore(appdata, appdata->project, appdata->osm);
      appendf(&context->log, _("Painting ...\n"));
      map_paint(appdata);
      appendf(&context->log, _("Done!\n"));
    }
  }

  /* tell the user that he can stop waiting ... */
  appendf(&context->log, _("Process finished.\n"));

  gtk_dialog_set_response_sensitive(GTK_DIALOG(context->dialog), 
				    GTK_RESPONSE_CLOSE, TRUE);

  gtk_dialog_run(GTK_DIALOG(context->dialog));
  gtk_widget_destroy(context->dialog);

}

