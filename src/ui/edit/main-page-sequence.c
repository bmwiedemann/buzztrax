/* $Id: main-page-sequence.c,v 1.1 2004-08-19 17:03:44 ensonic Exp $
 * class for the editor main machines page
 */

#define BT_EDIT
#define BT_MAIN_PAGE_SEQUENCE_C

#include "bt-edit.h"

enum {
  MAIN_PAGE_SEQUENCE_APP=1,
};


struct _BtMainPageSequencePrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;
  
  /* the application */
  BtEditApplication *app;
  
  /* name of the song */
  GtkEntry *name;
  /* genre of the song  */
  GtkEntry *genre;
  /* freeform info anout the song */
  GtkTextView *info;
};

//-- event handler

static void on_song_changed(const BtEditApplication *app, gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  BtSong *song;

  GST_INFO("song has changed : app=%p, window=%p\n",song,user_data);
  // get song from app
  song=BT_SONG(bt_g_object_get_object_property(G_OBJECT(self->private->app),"song"));
  // update page
}

//-- helper methods

static gboolean bt_main_page_sequence_init_ui(const BtMainPageSequence *self, const BtEditApplication *app) {

  // @todo add toolbar and list-view

  gtk_container_add(GTK_CONTAINER(self),gtk_label_new("no sequence view yet"));

  // register event handlers
  g_signal_connect(G_OBJECT(app), "song-changed", (GCallback)on_song_changed, (gpointer)self);
  return(TRUE);
}

//-- constructor methods

/**
 * bt_main_page_sequence_new:
 * @app: the application the window belongs to
 *
 * Create a new instance
 *
 * Return: the new instance or NULL in case of an error
 */
BtMainPageSequence *bt_main_page_sequence_new(const BtEditApplication *app) {
  BtMainPageSequence *self;

  if(!(self=BT_MAIN_PAGE_SEQUENCE(g_object_new(BT_TYPE_MAIN_PAGE_SEQUENCE,"app",app,NULL)))) {
    goto Error;
  }
  // generate UI
  if(!bt_main_page_sequence_init_ui(self,app)) {
    goto Error;
  }
  return(self);
Error:
  if(self) g_object_unref(self);
  return(NULL);
}

//-- methods

//-- wrapper

//-- class internals

/* returns a property for the given property_id for this object */
static void bt_main_page_sequence_get_property(GObject      *object,
                               guint         property_id,
                               GValue       *value,
                               GParamSpec   *pspec)
{
  BtMainPageSequence *self = BT_MAIN_PAGE_SEQUENCE(object);
  return_if_disposed();
  switch (property_id) {
    case MAIN_PAGE_SEQUENCE_APP: {
      g_value_set_object(value, G_OBJECT(self->private->app));
    } break;
    default: {
 			g_assert(FALSE);
      break;
    }
  }
}

/* sets the given properties for this object */
static void bt_main_page_sequence_set_property(GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BtMainPageSequence *self = BT_MAIN_PAGE_SEQUENCE(object);
  return_if_disposed();
  switch (property_id) {
    case MAIN_PAGE_SEQUENCE_APP: {
      self->private->app = g_object_ref(G_OBJECT(g_value_get_object(value)));
      //GST_DEBUG("set the app for MAIN_PAGE_SEQUENCE: %p",self->private->app);
    } break;
    default: {
			g_assert(FALSE);
      break;
    }
  }
}

static void bt_main_page_sequence_dispose(GObject *object) {
  BtMainPageSequence *self = BT_MAIN_PAGE_SEQUENCE(object);
	return_if_disposed();
  self->private->dispose_has_run = TRUE;
}

static void bt_main_page_sequence_finalize(GObject *object) {
  BtMainPageSequence *self = BT_MAIN_PAGE_SEQUENCE(object);
  
  g_object_unref(G_OBJECT(self->private->app));
  g_free(self->private);
}

static void bt_main_page_sequence_init(GTypeInstance *instance, gpointer g_class) {
  BtMainPageSequence *self = BT_MAIN_PAGE_SEQUENCE(instance);
  self->private = g_new0(BtMainPageSequencePrivate,1);
  self->private->dispose_has_run = FALSE;
}

static void bt_main_page_sequence_class_init(BtMainPageSequenceClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GParamSpec *g_param_spec;
  
  gobject_class->set_property = bt_main_page_sequence_set_property;
  gobject_class->get_property = bt_main_page_sequence_get_property;
  gobject_class->dispose      = bt_main_page_sequence_dispose;
  gobject_class->finalize     = bt_main_page_sequence_finalize;

  g_object_class_install_property(gobject_class,MAIN_PAGE_SEQUENCE_APP,
                                  g_param_spec_object("app",
                                     "app contruct prop",
                                     "Set application object, the window belongs to",
                                     BT_TYPE_EDIT_APPLICATION, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE));
}

GType bt_main_page_sequence_get_type(void) {
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (BtMainPageSequenceClass),
      NULL, // base_init
      NULL, // base_finalize
      (GClassInitFunc)bt_main_page_sequence_class_init, // class_init
      NULL, // class_finalize
      NULL, // class_data
      sizeof (BtMainPageSequence),
      0,   // n_preallocs
	    (GInstanceInitFunc)bt_main_page_sequence_init, // instance_init
    };
		type = g_type_register_static(GTK_TYPE_VBOX,"BtMainPageSequence",&info,0);
  }
  return type;
}

