/* $Id: pattern.c,v 1.3 2004-07-12 17:28:20 ensonic Exp $
 * class for an event pattern of a #BtMachine instance
 */
 
#define BT_CORE
#define BT_PATTERN_C

#include <libbtcore/core.h>

enum {
  PATTERN_SONG=1,
  PATTERN_LENGTH,
  PATTERN_VOICES,
  PATTERN_MACHINE
};

struct _BtPatternPrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;
	
	/* the song the pattern belongs to */
	BtSong *song;

  /* the number of ticks */
  glong length;
  /* the number of voices */
  glong voices;
  /* the number of dynamic params the machine provides per instance */
  glong global_params;
  /* the number of dynamic params the machine provides per instance and voice */
  glong voice_params;
  /* the machine the pattern belongs to */
  BtMachine *machine;

  /* we have one GValue* in length*(global_params+voices*voice_params) */
	GValue  *data;
  /** @todo more objects here !
  BtPatternRow *ticks;
  
  struct BtPatternRow {
    GValue *global_data;  // global_params
    GValue **voice_data;  // voices*voice_params
  }
   */
};

//-- helper methods

static void bt_pattern_init_data(const BtPattern *self) {
  g_assert(self->private->data==NULL);
  
  if(self->private->length && self->private->voices && self->private->params) {
    glong data_count=self->private->length*(self->private->voices*self->private->voice_params+self->private->global_params);

    GST_DEBUG("bt_pattern_init_data : %d*(%d*%d+%d)=%d",self->private->length,self->private->voices,self->private->voice_params,self->private->global_params,data_count);
    self->private->data=g_new0(GValue,data_count);
    // @todo depending on the type of the dparams initialize the GValues
  }
}

//-- methods

//-- wrapper

//-- class internals

/* returns a property for the given property_id for this object */
static void bt_pattern_get_property(GObject      *object,
                               guint         property_id,
                               GValue       *value,
                               GParamSpec   *pspec)
{
  BtPattern *self = BT_PATTERN(object);
  return_if_disposed();
  switch (property_id) {
    case PATTERN_SONG: {
      g_value_set_object(value, G_OBJECT(self->private->song));
    } break;
    case PATTERN_LENGTH: {
      g_value_set_long(value, self->private->length);
    } break;
    case PATTERN_VOICES: {
      g_value_set_long(value, self->private->voices);
    } break;
    case PATTERN_MACHINE: {
      g_value_set_object(value, G_OBJECT(self->private->machine));
    } break;
    default: {
      g_assert(FALSE);
      break;
    }
  }
}

/* sets the given properties for this object */
static void bt_pattern_set_property(GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BtPattern *self = BT_PATTERN(object);
  return_if_disposed();
  switch (property_id) {
    case PATTERN_SONG: {
      self->private->song = g_object_ref(G_OBJECT(g_value_get_object(value)));
      //GST_DEBUG("set the song for pattern: %p",self->private->song);
    } break;
    case PATTERN_LENGTH: {
      self->private->length = g_value_get_long(value);
      bt_pattern_init_data(self);
    } break;
    case PATTERN_VOICES: {
      self->private->voices = g_value_get_long(value);
      bt_pattern_init_data(self);
    } break;
    case PATTERN_MACHINE: {
      glong global_params,voice_params;
      self->private->machine = g_object_ref(G_OBJECT(g_value_get_object(value)));
      self->private->global_params=bt_g_object_get_long_property(G_OBJECT(self->private->machine),"global_params");
      self->private->voice_params=bt_g_object_get_long_property(G_OBJECT(self->private->machine),"voice_params");
      //GST_DEBUG("set the machine for pattern: %p",self->private->machine);
    } break;
    default: {
      g_assert(FALSE);
      break;
    }
  }
}

static void bt_pattern_dispose(GObject *object) {
  BtPattern *self = BT_PATTERN(object);
	return_if_disposed();
  self->private->dispose_has_run = TRUE;
}

static void bt_pattern_finalize(GObject *object) {
  BtPattern *self = BT_PATTERN(object);
  
	g_object_unref(G_OBJECT(self->private->song));
  g_free(self->private->data);
  g_free(self->private);
}

static void bt_pattern_init(GTypeInstance *instance, gpointer g_class) {
  BtPattern *self = BT_PATTERN(instance);

  self->private = g_new0(BtPatternPrivate,1);
  self->private->dispose_has_run = FALSE;
}

static void bt_pattern_class_init(BtPatternClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GParamSpec *g_param_spec;
  
  gobject_class->set_property = bt_pattern_set_property;
  gobject_class->get_property = bt_pattern_get_property;
  gobject_class->dispose      = bt_pattern_dispose;
  gobject_class->finalize     = bt_pattern_finalize;

  g_object_class_install_property(gobject_class,PATTERN_SONG,
                                  g_param_spec_object("song",
                                     "song contruct prop",
                                     "Song object, the pattern belongs to",
                                     BT_TYPE_SONG, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class,PATTERN_LENGTH,
																	g_param_spec_long("length",
                                     "length prop",
                                     "length of the pattern in ticks",
                                     1,
                                     G_MAXLONG,
                                     1,
                                     G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class,PATTERN_VOICES,
																	g_param_spec_long("voices",
                                     "voices prop",
                                     "number of voices in the pattern",
                                     1,
                                     G_MAXLONG,
                                     1,
                                     G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,PATTERN_MACHINE,
                                  g_param_spec_object("machine",
                                     "machine contruct prop",
                                     "Machine object, the pattern belongs to",
                                     BT_TYPE_MACHINE, /* object type */
                                     G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE));

}

GType bt_pattern_get_type(void) {
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (BtPatternClass),
      NULL, // base_init
      NULL, // base_finalize
      (GClassInitFunc)bt_pattern_class_init, // class_init
      NULL, // class_finalize
      NULL, // class_data
      sizeof (BtPattern),
      0,   // n_preallocs
	    (GInstanceInitFunc)bt_pattern_init, // instance_init
    };
		type = g_type_register_static(G_TYPE_OBJECT,"BtPattern",&info,0);
  }
  return type;
}

