/* $Id: source-machine.c,v 1.3 2004-05-11 20:01:23 ensonic Exp $
 * class for a source machine
 */
 
#define BT_CORE
#define BT_SOURCE_MACHINE_C

#include <libbtcore/core.h>
#include <libbtcore/source-machine.h>

struct _BtSourceMachinePrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;
};

//-- methods

//-- wrapper

//-- class internals

/* returns a property for the given property_id for this object */
static void bt_source_machine_get_property(GObject      *object,
                               guint         property_id,
                               GValue       *value,
                               GParamSpec   *pspec)
{
  BtSourceMachine *self = BT_SOURCE_MACHINE(object);
  return_if_disposed();
  switch (property_id) {
    default: { // call super method
			BtSourceMachineClass *klass=BT_SOURCE_MACHINE_GET_CLASS(object);
			BtMachineClass *base_klass=BT_MACHINE_CLASS(klass);
			GObjectClass *base_gobject_class = G_OBJECT_CLASS(base_klass);
			
			base_gobject_class->get_property(object,property_id,value,pspec);
      break;
    }
  }
}

/* sets the given properties for this object */
static void bt_source_machine_set_property(GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BtSourceMachine *self = BT_SOURCE_MACHINE(object);
  return_if_disposed();
  switch (property_id) {
    default: { // call super method
			BtSourceMachineClass *klass=BT_SOURCE_MACHINE_GET_CLASS(object);
			BtMachineClass *base_klass=BT_MACHINE_CLASS(klass);
			GObjectClass *base_gobject_class = G_OBJECT_CLASS(base_klass);
			
			base_gobject_class->set_property(object,property_id,value,pspec);
      break;
    }
  }
}

static void bt_source_machine_dispose(GObject *object) {
  BtSourceMachine *self = BT_SOURCE_MACHINE(object);
	return_if_disposed();
  self->private->dispose_has_run = TRUE;
}

static void bt_source_machine_finalize(GObject *object) {
  BtSourceMachine *self = BT_SOURCE_MACHINE(object);
  g_free(self->private);
}

static void bt_source_machine_init(GTypeInstance *instance, gpointer g_class) {
  BtSourceMachine *self = BT_SOURCE_MACHINE(instance);
  self->private = g_new0(BtSourceMachinePrivate,1);
  self->private->dispose_has_run = FALSE;
}

static void bt_source_machine_class_init(BtSourceMachineClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	BtMachineClass *base_class = BT_MACHINE_CLASS(klass);
  
  gobject_class->set_property = bt_source_machine_set_property;
  gobject_class->get_property = bt_source_machine_get_property;
  gobject_class->dispose      = bt_source_machine_dispose;
  gobject_class->finalize     = bt_source_machine_finalize;
}

GType bt_source_machine_get_type(void) {
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (BtSourceMachineClass),
      NULL, // base_init
      NULL, // base_finalize
      (GClassInitFunc)bt_source_machine_class_init, // class_init
      NULL, // class_finalize
      NULL, // class_data
      sizeof (BtSourceMachine),
      0,   // n_preallocs
	    (GInstanceInitFunc)bt_source_machine_init, // instance_init
    };
		type = g_type_register_static(BT_MACHINE_TYPE,"BtSourceMachine",&info,0);
  }
  return type;
}

