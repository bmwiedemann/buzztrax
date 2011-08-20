/* $Id$
 *
 * Buzztard
 * Copyright (C) 2006 Buzztard team <buzztard-devel@lists.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:btmainpagesequence
 * @short_description: the editor main sequence page
 * @see_also: #BtSequence, #BtSequenceView
 *
 * Provides an editor for #BtSequence instances.
 */

/* @todo: main-page-sequence tasks
 * - add third view for eating remaining space
 *   - or block cursor moving there
 * - shortcuts
 *   - Ctrl-<num> :  Stepping
 *     - set increment for cursor-down on edit
 *   - Duplicate pattern: make a copy of pattern under cursor and go to pattern view
 *   - New pattern: open new pattern dialog, insert pattern under cursor and go to pattern view
 * - sequence view context menu
 *   - open pattern properties
 *   - copy current pattern
 *   - allow to switch meters (off, level, scope, spectrum)
 * - when we move between tracks, switch the current-machine in pattern-view
 *   - we could expose current-machine as a property
 * - pattern list
 *   - go to next occurence when double clicking a pattern
 *   - show tick-length in pattern list (needs column in model)
 */
/* @todo: we should have a track-changed signal ((current-track property)
 *  - allows pattern to sync with selected machine and not passively syncing
 *    (bt_main_page_patterns_show_machine())
 *  - we already have a cursor-row property and we could add a cursor-column
 *    property too (and get the notify::cursor-column for free)
 */
/* @todo: handle pattern name changes
 *   - when pattern gets renamed
 *     - we need to update the pattern list (if shown) - done
 *     - we need to update the sequence (if pattern is used)
 *     - we need to catch each pattern addition to listen to notify::name
 *       - right now we only watch for pattern add/remove for current track
 *       - we need to avoid to add the handler multiple times
 */
/* @idea: add a follow playback checkbox to toolbar to en/disable sequence scrolling
 *   - the scrolling causes quite some repaints and thus slowness
 *   - it would be good if we could deoouple the scolling and the events, so
 *     that we e.g. scroll 10 times a second to the latest position
 */
/* @idea: bold row,label for cursor row
 *   - makes it easier to follow position in wide sequences
 *     (same needed for pattern view)
 */
/* @idea: have a split horizontal command
 *   - we would share the hadjustment, but have separate vadjustments
 *   - the label-menu would require that we have a focused view
 */
/* @bugs
 * - hovering the mouse over the treeview causes redraws for the whole lines
 *   - cells are asked to do prelight, even if they wouldn't draw anything else
 *     http://www.gtk.org/plan/meetings/20041025.txt
 */
/* @idea: programmable keybindings
 * - we should define an enum for the key commands
 * - we should have a tables that maps keyval+state to one of the enums
 * - we should have a utility function to do the lookups
 * - we could have primary and secondary keys (keyval+state)
 * - each enry has a description (i18n)
 * - the whole group has a name (i18n)
 * - we can register the group to a binding manager
 * - the binding manager provides a ui to:
 *   - edit the bindings
 *   - save/load bindings to/from a named preset
 */
/* @todo: undo/redo
 * - finish add/remove tracks
 */

#define BT_EDIT
#define BT_MAIN_PAGE_SEQUENCE_C

#include "bt-edit.h"
#include "gtkvumeter.h"

enum {
  MAIN_PAGE_SEQUENCE_CURSOR_ROW=1
};

/* try new sequence model
 * - todo:
 *   - kill sequence_table_refresh, where not needed anymore
 *     - 14 occurances
 *     -  2 in old code path
 *     - 12 in use
 *       - 5 add/remove track, swap track
 *       - 1 machine removal
 *         - we only need to update the columns -> refresh_columns()
 *         - ideally use gtk_tree_view_move_column_after()
 *       - 4 insert/delete (need signal from sequence to model)
 *       - 1 pattern removal
 *         - no need to do anything once the model notices  
 *       - 1 song-changed
 *         - thats the place where we recreate the model
 *   - we have split into refresh_model (should become a nop) and refresh_columns
 *   - remove sequence_model_recolorize() - already a dummy op
 *   - do we need to connect to "row-inserted"/"row-deleted"?
 * - done:
 *   - get rid of old column and pos-format enums
 *   - tell the model about bars changes
 *   - handle cursor-down expand
 *   - use the main-model for pos-menu
 *     - kill the extra enum for it
 *     - kill the manual label menu refresh
 */
#define USE_SEQUENCE_GRID_MODEL 1

struct _BtMainPageSequencePrivate {
  /* used to validate if dispose has run */
  gboolean dispose_has_run;

  /* the application */
  BtEditApplication *app;

  /* the main window */
  BtMainWindow *main_window;

  /* the sequence we are showing */
  BtSequence *sequence;
  /* machine for current column */
  BtMachine *machine;

  /* bars selection menu */
  GtkComboBox *bars_menu;
  gulong bars;

  /* label selection menu */
  GtkComboBox *label_menu;

  /* pos unit selection menu */
  GtkComboBox *pos_menu;
  BtSequenceGridModelPosFormat pos_format;

  /* the sequence table */
  GtkHBox *sequence_pos_table_header;
  GtkTreeView *sequence_pos_table;
  GtkHBox *sequence_table_header;
  GtkTreeView *sequence_table;
  /* the pattern list */
  GtkTreeView *pattern_list;

  /* position-table header label widget */
  GtkWidget *pos_header;

  /* local commands */
  GtkAccelGroup *accel_group;

  /* sequence context_menu */
  GtkMenu *context_menu;
  GtkMenuItem *context_menu_add;

  /* colors */
  GdkColor *cursor_bg;
  GdkColor *selection_bg1,*selection_bg2;
  GdkColor *source_bg1,*source_bg2;
  GdkColor *processor_bg1,*processor_bg2;
  GdkColor *sink_bg1,*sink_bg2;

  /* some internal states */
  glong tick_pos;
  /* cursor */
  glong cursor_column;
  glong cursor_row;
  /* selection range */
  glong selection_start_column;
  glong selection_start_row;
  glong selection_end_column;
  glong selection_end_row;
  /* selection first cell */
  glong selection_column;
  glong selection_row;

  /* vumeter data */
  GHashTable *level_to_vumeter;
  GstClock *clock;

  /* number of rows contained in the model, this is the length of the sequence
   * plus extra dummy lines */
  gulong sequence_length;

  /* signal handler id's */
  gulong pattern_removed_handler;

  /* playback state */
  gboolean is_playing;

  /* lock for multithreaded access */
  GMutex        *lock;

  /* cached sequence properties */
  GHashTable *properties;

  /* editor change log */
  BtChangeLog *change_log;
};

static GQuark bus_msg_level_quark=0;
static GQuark vu_meter_skip_update=0;

static GdkAtom sequence_atom;

//-- the class

static void bt_main_page_sequence_change_logger_interface_init(gpointer const g_iface, gconstpointer const iface_data);

G_DEFINE_TYPE_WITH_CODE (BtMainPageSequence, bt_main_page_sequence, GTK_TYPE_VBOX,
  G_IMPLEMENT_INTERFACE (BT_TYPE_CHANGE_LOGGER,
    bt_main_page_sequence_change_logger_interface_init));

enum {
  SEQUENCE_VIEW_POS_PLAY=0,
  SEQUENCE_VIEW_POS_LOOP_START,
  SEQUENCE_VIEW_POS_LOOP_END
};

// this only works for 4/4 meassure
//#define IS_SEQUENCE_POS_VISIBLE(pos,bars) ((pos&((bars)-1))==0)
#define IS_SEQUENCE_POS_VISIBLE(pos,bars) ((pos%bars)==0)
#define SEQUENCE_CELL_WIDTH 100
#define SEQUENCE_CELL_HEIGHT 28
#define SEQUENCE_CELL_XPAD 0
#define SEQUENCE_CELL_YPAD 0
#define POSITION_CELL_WIDTH 65
#define HEADER_SPACING 0

#define LOW_VUMETER_VAL -60.0

// when setting the HEIGHT for one column, then the focus rect is visible for
// the other (smaller) columns

enum {
  METHOD_SET_PATTERNS,
  METHOD_SET_LABELS,
  METHOD_SET_SEQUENCE_PROPERTY,
  METHOD_ADD_TRACK,
  METHOD_REM_TRACK,
  METHOD_MOVE_TRACK
};

static BtChangeLoggerMethods change_logger_methods[] = {
  BT_CHANGE_LOGGER_METHOD("set_patterns",13,"([0-9]+),([0-9]+),([0-9]+),(.*)$"),
  BT_CHANGE_LOGGER_METHOD("set_labels",11,"([0-9]+),([0-9]+),(.*)$"),
  BT_CHANGE_LOGGER_METHOD("set_sequence_property",22,"\"([-_a-zA-Z0-9 ]+)\",\"([-_a-zA-Z0-9 ]+)\"$"),
  BT_CHANGE_LOGGER_METHOD("add_track",10,"\"([a-zA-Z0-9 ]+)\",([0-9]+)$"),
  BT_CHANGE_LOGGER_METHOD("rem_track",10,"([0-9]+)$"),
  BT_CHANGE_LOGGER_METHOD("move_track",11,"([0-9]+),([0-9]+)$"),
  { NULL, }
};


static GQuark column_index_quark=0;

static void sequence_table_refresh(const BtMainPageSequence *self,const BtSong *song);

static void on_track_add_activated(GtkMenuItem *menuitem, gpointer user_data);
static void on_pattern_removed(BtMachine *machine,BtPattern *pattern,gpointer user_data);

//-- main-window helper
static void grab_main_window(const BtMainPageSequence *self) {
  GtkWidget *toplevel=gtk_widget_get_toplevel((GtkWidget *)self);
  if(gtk_widget_is_toplevel(toplevel)) {
    self->priv->main_window=BT_MAIN_WINDOW(toplevel);
    GST_DEBUG("top-level-window = %p",toplevel);
  }
}

//-- tree filter func

static gboolean step_visible_filter(GtkTreeModel *store,GtkTreeIter *iter,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gulong pos;

  // determine row number and hide or show accordingly
  gtk_tree_model_get(store,iter,BT_SEQUENCE_GRID_MODEL_POS,&pos,-1);

  if((pos<self->priv->sequence_length) && IS_SEQUENCE_POS_VISIBLE(pos,self->priv->bars))
    return TRUE;
  else
    return FALSE;
}

static gboolean label_visible_filter(GtkTreeModel *store,GtkTreeIter *iter,gpointer user_data) {
  //BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gchar *label;

  // show only columns with labels
  gtk_tree_model_get(store,iter,BT_SEQUENCE_GRID_MODEL_LABEL,&label,-1);

  if(label)
    return TRUE;
  else
    return FALSE;
}

//-- tree cell data functions

static void label_cell_data_function(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gulong row;
  GdkColor *bg_col=NULL;

  gtk_tree_model_get(model,iter,
    BT_SEQUENCE_GRID_MODEL_POS,&row,
    -1);

  if((0==self->priv->cursor_column) && (row==self->priv->cursor_row)) {
    bg_col=self->priv->cursor_bg;
  }
  else if((0>=self->priv->selection_start_column) && (0<=self->priv->selection_end_column) &&
    (row>=self->priv->selection_start_row) && (row<=self->priv->selection_end_row)
  ) {
    bg_col=((row/self->priv->bars)&1)?self->priv->selection_bg2:self->priv->selection_bg1;
  }
  if(bg_col) {
    g_object_set(renderer,
      "background-gdk",bg_col,
      "background-set",TRUE,
      NULL);
  }
  else {
    g_object_set(renderer,
      "background-set",FALSE,
      NULL);
  }
}


static void source_machine_cell_data_function(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gulong row,column;
  gboolean shade;
  GdkColor *bg_col;
#ifndef USE_SEQUENCE_GRID_MODEL
  gchar *str;
#else
  const gchar *str;
#endif

  column=1+GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(col),column_index_quark));

  gtk_tree_model_get(model,iter,
    BT_SEQUENCE_GRID_MODEL_POS,&row,
    BT_SEQUENCE_GRID_MODEL_SHADE,&shade,
    BT_SEQUENCE_GRID_MODEL_LABEL+column,&str,
    -1);

  if((column==self->priv->cursor_column) && (row==self->priv->cursor_row)) {
    bg_col=self->priv->cursor_bg;
  }
  else if((column>=self->priv->selection_start_column) && (column<=self->priv->selection_end_column) &&
    (row>=self->priv->selection_start_row) && (row<=self->priv->selection_end_row)) {
    bg_col=shade?self->priv->selection_bg1:self->priv->selection_bg2;
  }
  else {
    bg_col=shade?self->priv->source_bg2:self->priv->source_bg1;
  }
  g_object_set(renderer,
    "background-gdk",bg_col,
    "text",str,
     NULL);
#ifndef USE_SEQUENCE_GRID_MODEL
  g_free(str);
#endif
}

static void processor_machine_cell_data_function(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gulong row,column;
  gboolean shade;
  GdkColor *bg_col;
#ifndef USE_SEQUENCE_GRID_MODEL
  gchar *str;
#else
  const gchar *str;
#endif

  column=1+GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(col),column_index_quark));

  gtk_tree_model_get(model,iter,
    BT_SEQUENCE_GRID_MODEL_POS,&row,
    BT_SEQUENCE_GRID_MODEL_SHADE,&shade,
    BT_SEQUENCE_GRID_MODEL_LABEL+column,&str,
    -1);

  if((column==self->priv->cursor_column) && (row==self->priv->cursor_row)) {
    bg_col=self->priv->cursor_bg;
  }
  else if((column>=self->priv->selection_start_column) && (column<=self->priv->selection_end_column) &&
    (row>=self->priv->selection_start_row) && (row<=self->priv->selection_end_row)) {
    bg_col=shade?self->priv->selection_bg1:self->priv->selection_bg2;
  }
  else {
    bg_col=shade?self->priv->processor_bg2:self->priv->processor_bg1;
  }
  g_object_set(renderer,
    "background-gdk",bg_col,
    "text",str,
     NULL);
#ifndef USE_SEQUENCE_GRID_MODEL
  g_free(str);
#endif
}

static void sink_machine_cell_data_function(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gulong row,column;
  gboolean shade;
  GdkColor *bg_col;
#ifndef USE_SEQUENCE_GRID_MODEL
  gchar *str;
#else
  const gchar *str;
#endif

  column=1+GPOINTER_TO_UINT(g_object_get_qdata(G_OBJECT(col),column_index_quark));

  gtk_tree_model_get(model,iter,
    BT_SEQUENCE_GRID_MODEL_POS,&row,
    BT_SEQUENCE_GRID_MODEL_SHADE,&shade,
    BT_SEQUENCE_GRID_MODEL_LABEL+column,&str,
    -1);

  if((column==self->priv->cursor_column) && (row==self->priv->cursor_row)) {
    bg_col=self->priv->cursor_bg;
  }
  else if((column>=self->priv->selection_start_column) && (column<=self->priv->selection_end_column) &&
    (row>=self->priv->selection_start_row) && (row<=self->priv->selection_end_row)) {
    bg_col=shade?self->priv->selection_bg1:self->priv->selection_bg2;
  }
  else {
    bg_col=shade?self->priv->sink_bg2:self->priv->sink_bg1;
  }
  g_object_set(renderer,
    "background-gdk",bg_col,
    "text",str,
     NULL);
#ifndef USE_SEQUENCE_GRID_MODEL
  g_free(str);
#endif
}

//-- tree model helper

static gboolean sequence_view_get_cursor_pos(GtkTreeView *tree_view,GtkTreePath *path,GtkTreeViewColumn *column,gulong *col,gulong *row) {
  gboolean res=FALSE;
  GtkTreeModel *store;
  GtkTreeModelFilter *filtered_store;
  GtkTreeIter iter,filter_iter;

  g_return_val_if_fail(path,FALSE);

  if((filtered_store=GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(tree_view)))
    && (store=gtk_tree_model_filter_get_model(filtered_store))
  )  {
    if(gtk_tree_model_get_iter(GTK_TREE_MODEL(filtered_store),&filter_iter,path)) {
      if(col) {
        GList *columns=gtk_tree_view_get_columns(tree_view);
        *col=g_list_index(columns,(gpointer)column);
        g_list_free(columns);
      }
      if(row) {
        gtk_tree_model_filter_convert_iter_to_child_iter(filtered_store,&iter,&filter_iter);
        gtk_tree_model_get(store,&iter,BT_SEQUENCE_GRID_MODEL_POS,row,-1);
      }
      res=TRUE;
    }
    else {
      GST_INFO("No iter for path");
    }
  }
  else {
    GST_WARNING("Can't get tree-model");
  }
  return(res);
}

static gboolean sequence_view_set_cursor_pos(const BtMainPageSequence *self) {
  GtkTreePath *path;
  gboolean res=FALSE;

  // @todo: http://bugzilla.gnome.org/show_bug.cgi?id=498010, fixed in 2008
  if(!GTK_IS_TREE_VIEW(self->priv->sequence_table) || !gtk_tree_view_get_model(self->priv->sequence_table)) return(FALSE);

  if((path=gtk_tree_path_new_from_indices((self->priv->cursor_row/self->priv->bars),-1))) {
    GList *columns;
    if((columns=gtk_tree_view_get_columns(self->priv->sequence_table))) {
      GtkTreeViewColumn *column=g_list_nth_data(columns,self->priv->cursor_column);
      // set cell focus
      gtk_tree_view_set_cursor(self->priv->sequence_table,path,column,FALSE);

      res=TRUE;
      g_list_free(columns);
    }
    else {
      GST_WARNING("Can't get columns for pos %ld:%ld",self->priv->cursor_row,self->priv->cursor_column);
    }
    gtk_tree_path_free(path);
  }
  else {
    GST_WARNING("Can't create treepath for pos %ld:%ld",self->priv->cursor_row,self->priv->cursor_column);
  }
  gtk_widget_grab_focus_savely(GTK_WIDGET(self->priv->sequence_table));
  return res;
}

/*
 * sequence_view_get_current_pos:
 * @self: the sequence subpage
 * @time: pointer for time result
 * @track: pointer for track result
 *
 * Get the currently cursor position in the sequence table.
 * The result will be place in the respective pointers.
 * If one is NULL, no value is returned for it.
 *
 * Returns: %TRUE if the cursor is at a valid track position
 */
static gboolean sequence_view_get_current_pos(const BtMainPageSequence *self,gulong *time,gulong *track) {
  gboolean res=FALSE;
  GtkTreePath *path;
  GtkTreeViewColumn *column;

  //GST_INFO("get active sequence cell");

  gtk_tree_view_get_cursor(self->priv->sequence_table,&path,&column);
  if(column && path) {
    res=sequence_view_get_cursor_pos(self->priv->sequence_table,path,column,track,time);
  }
  else {
    GST_INFO("No cursor pos, column=%p, path=%p",column,path);
  }
  if(path) gtk_tree_path_free(path);
  return(res);
}

/*
static gboolean sequence_model_get_iter_by_position(GtkTreeModel *store,GtkTreeIter *iter,gulong that_pos) {
  gulong this_pos;
  gboolean found=FALSE;

  gtk_tree_model_get_iter_first(store,iter);
  do {
    gtk_tree_model_get(store,iter,BT_SEQUENCE_GRID_MODEL_POS,&this_pos,-1);
    if(this_pos==that_pos) {
      found=TRUE;break;
    }
  } while(gtk_tree_model_iter_next(store,iter));
  return(found);
}
*/

static GtkTreeModel *sequence_model_get_store(const BtMainPageSequence *self) {
  GtkTreeModel *store=NULL;
  GtkTreeModelFilter *filtered_store;

  if((filtered_store=GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(self->priv->sequence_table)))) {
    store=gtk_tree_model_filter_get_model(filtered_store);
  }
  return(store);
}

/*
 * sequence_model_recolorize:
 * @self: the sequence subpage
 *
 * Apply alternate coloring for visible rows
 */
static void sequence_model_recolorize(const BtMainPageSequence *self) {
#ifndef USE_SEQUENCE_GRID_MODEL
  GtkTreeModel *store;
  GtkTreeIter iter;

  GST_INFO("recolorize sequence tree view");

  if((store=sequence_model_get_store(self))) {
    if(gtk_tree_model_get_iter_first(store,&iter)) {
      gboolean odd_row=FALSE;
      do {
        if(step_visible_filter(store,&iter,(gpointer)self)) {
          gtk_list_store_set(GTK_LIST_STORE(store),&iter,BT_SEQUENCE_GRID_MODEL_SHADE,odd_row,-1);
          odd_row=!odd_row;
        }
      } while(gtk_tree_model_iter_next(store,&iter));
    }
  }
  else {
    GST_WARNING("can't get tree model");
  }
#endif
}

/*
 * sequence_calculate_visible_lines:
 * @self: the sequence subpage
 *
 * Recalculate the visible lines after length or bar-stepping changes. Also
 * updated the loop marker positions accordingly.
 */
static void sequence_calculate_visible_lines(const BtMainPageSequence *self) {
  gulong visible_rows,sequence_length;
  glong loop_start_pos,loop_end_pos;
  gdouble loop_start,loop_end;

  g_object_get(self->priv->sequence,"length",&sequence_length,"loop-start",&loop_start_pos,"loop-end",&loop_end_pos,NULL);

  if(self->priv->sequence_length<sequence_length) {
    self->priv->sequence_length=sequence_length;
  }

  visible_rows=sequence_length/self->priv->bars;
  loop_start=(loop_start_pos>-1)?(gdouble)loop_start_pos/(gdouble)sequence_length:0.0;
  loop_end  =(loop_end_pos  >-1)?(gdouble)loop_end_pos  /(gdouble)sequence_length:1.0;
  GST_INFO("visible_rows=%lu = %lu / %lu",visible_rows,sequence_length,self->priv->bars);
  g_object_set(self->priv->sequence_table,"visible-rows",visible_rows,"loop-start",loop_start,"loop-end",loop_end,NULL);
  g_object_set(self->priv->sequence_pos_table,"visible-rows",visible_rows,"loop-start",loop_start,"loop-end",loop_end,NULL);
}

#ifndef USE_SEQUENCE_GRID_MODEL
static gchar *sequence_format_positions(const BtMainPageSequence *self,gulong pos) {
  static gchar pos_str[20];

  switch(self->priv->pos_format) {
    case BT_SEQUENCE_GRID_MODEL_POS_FORMAT_TICKS:
      g_snprintf(pos_str,6,"%5lu",pos);
      break;
    case BT_SEQUENCE_GRID_MODEL_POS_FORMAT_TIME: {
      gulong msec,sec,min;
      const GstClockTime bar_time=bt_sequence_get_bar_time(self->priv->sequence);

      msec=(gulong)((pos*bar_time)/G_USEC_PER_SEC);
      min=(gulong)(msec/60000);msec-=(min*60000);
      sec=(gulong)(msec/ 1000);msec-=(sec* 1000);
      g_sprintf(pos_str,"%02lu:%02lu.%03lu",min,sec,msec);
    } break;
    default:
      *pos_str='\0';
      GST_WARNING("unimplemented time format %d",self->priv->pos_format);
  }
  return(pos_str);
}
#endif

//-- gtk helpers

static void widget_shade_bg_color(GtkWidget *widget,GtkStateType state,gfloat rf,gfloat gf,gfloat bf) {
  GtkStyle *style=gtk_widget_get_style(widget);
  GdkColor color=style->bg[state];
  gfloat c;

  c=((gfloat)color.red*rf);
  color.red=(guint16)MIN(c,65535.0);
  c=((gfloat)color.green*gf);
  color.green=(guint16)MIN(c,65535.0);
  c=((gfloat)color.blue*bf);
  color.blue=(guint16)MIN(c,65535.0);
  gtk_widget_modify_bg(widget,state,&color);

}

static GtkWidget* make_mini_button(const gchar *txt,gfloat rf,gfloat gf,gfloat bf, gboolean toggled) {
  GtkWidget *button;

// the font get smaller, but the buttons don't :/
#define USE_MARKUP 0
#if USE_MARKUP
  GtkWidget *label;
  button=gtk_toggle_button_new_with_label("");
  label=gtk_bin_get_child(GTK_BIN(button));
  if(GTK_IS_LABEL(label)) {
    gchar *str=g_strconcat("<small>",txt,"</small>",NULL);
    gtk_label_set_markup (GTK_LABEL (label),str);
    g_free(str);
  }
  else {
    GST_WARNING("expecting a GtkLabel as a first child");
  }
#else
  button=gtk_toggle_button_new_with_label(txt);
#endif
  gtk_widget_set_name(button,"mini-button");
  widget_shade_bg_color(button,GTK_STATE_ACTIVE  ,rf,gf,bf);
  widget_shade_bg_color(button,GTK_STATE_PRELIGHT,rf,gf,bf);
  gtk_container_set_border_width(GTK_CONTAINER(button),0);
  if(toggled)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),toggled);

  return(button);
}

//-- tree model helper

static BtPattern *pattern_list_model_get_pattern_by_key(GtkTreeModel *store,gchar that_key) {
  GtkTreeIter iter;
  gchar *this_key;
  BtPattern *pattern=NULL;

  GST_INFO("look up pattern for key: '%c'",that_key);

  gtk_tree_model_get_iter_first(store,&iter);
  do {
    gtk_tree_model_get(store,&iter,BT_PATTERN_MODEL_SHORTCUT,&this_key,-1);
    if(this_key[0]==that_key) {
      pattern=g_object_ref(bt_pattern_list_model_get_object((BtPatternListModel *)store,&iter));
      GST_INFO("found pattern for key : %p,ref_count=%d",pattern,G_OBJECT_REF_COUNT(pattern));
      g_free(this_key);
      break;
    }
    g_free(this_key);
  } while(gtk_tree_model_iter_next(store,&iter));
  return(pattern);
}

//-- undo/redo helpers
static void sequence_range_copy(const BtMainPageSequence *self,glong track_beg,glong track_end,glong tick_beg,glong tick_end,GString *data) {
  BtSequence *sequence=self->priv->sequence;
  BtMachine *machine;
  BtPattern *pattern;
	glong i,j,col;
	gchar *id,*str;
	gulong sequence_length;
	
	g_object_get(sequence,"length",&sequence_length,NULL);

  /* label-track */
	col=track_beg;
	if(col==0) {
		g_string_append_c(data,' ');
		for(j=tick_beg;j<=tick_end;j++) {
			if((j<sequence_length) && (str=bt_sequence_get_label(sequence,j))) {
				g_string_append_c(data,',');
				g_string_append(data,str);
				g_free(str);
			} else {
				// empty cell
				g_string_append(data,", ");
			}
		}
		g_string_append_c(data,'\n');
		col++;
	}

	/* machine-tracks */
	for(i=col;i<=track_end;i++) {
		// store machine id
		machine=bt_sequence_get_machine(sequence,i-1);
		g_object_get(machine,"id",&id,NULL);
		g_string_append(data,id);
		g_free(id);
		for(j=tick_beg;j<=tick_end;j++) {
			// store pattern id
			if((j<sequence_length) && (pattern=bt_sequence_get_pattern(sequence,j,i-1))) {
				g_object_get(pattern,"id",&id,NULL);
				g_string_append_c(data,',');
				g_string_append(data,id);
				g_free(id);
				g_object_unref(pattern);
			}
			else {
				// empty cell
				g_string_append(data,", ");
			}
		}
		g_string_append_c(data,'\n');
		g_object_unref(machine);
	}
}

static void sequence_range_log_undo_redo(const BtMainPageSequence *self,glong track_beg,glong track_end,glong tick_beg,glong tick_end,gchar *old_str,gchar *new_str) {
	gchar *undo_str,*redo_str;
	gchar *p;
	glong i,col;

	bt_change_log_start_group(self->priv->change_log);
	
  /* label-track */
	col=track_beg;
	if(col==0) {
		p=strchr(old_str,'\n');*p='\0';
		undo_str = g_strdup_printf("set_labels %lu,%lu,%s",tick_beg,tick_end,old_str);
		old_str=&p[1];
		p=strchr(new_str,'\n');*p='\0';
		redo_str = g_strdup_printf("set_labels %lu,%lu,%s",tick_beg,tick_end,new_str);
		new_str=&p[1];
		bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);
		col++;		
	}
	/* machine-tracks */
	for(i=col;i<=track_end;i++) {
		p=strchr(old_str,'\n');*p='\0';
		undo_str = g_strdup_printf("set_patterns %lu,%lu,%lu,%s",i-1,tick_beg,tick_end,old_str);
		old_str=&p[1];
		p=strchr(new_str,'\n');*p='\0';
		redo_str = g_strdup_printf("set_patterns %lu,%lu,%lu,%s",i-1,tick_beg,tick_end,new_str);
		new_str=&p[1];
		bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);
	}
  bt_change_log_end_group(self->priv->change_log);
}

//-- event handlers

static void on_page_switched(GtkNotebook *notebook, GParamSpec *arg, gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  guint page_num;
  static gint prev_page_num=-1;

  g_object_get(notebook,"page",&page_num,NULL);

  if(page_num==BT_MAIN_PAGES_SEQUENCE_PAGE) {
    // only do this if the page really has changed
    if(prev_page_num != BT_MAIN_PAGES_SEQUENCE_PAGE) {
      GST_DEBUG("enter sequence page");
      // this is emmitted before we are mapped etc.
      if(!self->priv->main_window)
        grab_main_window(self);
      if(self->priv->main_window) {
        // add local commands
        gtk_window_add_accel_group(GTK_WINDOW(self->priv->main_window),self->priv->accel_group);
      }
    }
  }
  else {
    // only do this if the page was BT_MAIN_PAGES_SEQUENCE_PAGE
    if(prev_page_num == BT_MAIN_PAGES_SEQUENCE_PAGE) {
      GST_DEBUG("leave sequence page");
      if(self->priv->main_window) {
        // remove local commands
        gtk_window_remove_accel_group(GTK_WINDOW(self->priv->main_window),self->priv->accel_group);
        bt_child_proxy_set(self->priv->main_window,"statusbar::status",NULL,NULL);
      }
    }
  }
  prev_page_num = page_num;
}

static void on_machine_id_changed(BtMachine *machine,GParamSpec *arg,gpointer user_data) {
  GtkLabel *label=GTK_LABEL(user_data);
  gchar *str;

  g_object_get(machine,"id",&str,NULL);
  GST_INFO("machine id changed to \"%s\"",str);
  gtk_label_set_text(label,str);
  g_free(str);
}

static void on_machine_id_changed_seq(BtMachine *machine,GParamSpec *arg,gpointer user_data) {
  on_machine_id_changed(machine,arg,user_data);
}

static void on_machine_id_changed_menu(BtMachine *machine,GParamSpec *arg,gpointer user_data) {
  on_machine_id_changed(machine,arg,user_data);
}

/*
 * on_header_size_allocate:
 *
 * Adjusts the height of the header widget of the first treeview (pos) to the
 * height of the second treeview.
 */
static void on_header_size_allocate(GtkWidget *widget,GtkAllocation *allocation,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);

  GST_DEBUG("#### header label size %d x %d",allocation->width,allocation->height);

  gtk_widget_set_size_request(self->priv->pos_header,-1,allocation->height);
}

/* DEBUG
static void on_sequence_header_size_allocate(GtkWidget *widget,GtkAllocation *allocation,gpointer user_data) {
  GtkRequisition requisition;

  gtk_widget_size_request(widget,&requisition);
  GST_WARNING("#### header %s alloc:  %d x %d, req: %d x %d",
    (gchar *)user_data,
    allocation->width,allocation->height,
    requisition.width,requisition.height
    );
}
// DEBUG */

static void on_mute_toggled(GtkToggleButton *togglebutton,gpointer user_data) {
  BtMachine *machine=BT_MACHINE(user_data);

  if(gtk_toggle_button_get_active(togglebutton)) {
    g_object_set(machine,"state",BT_MACHINE_STATE_MUTE,NULL);
  }
  else {
    g_object_set(machine,"state",BT_MACHINE_STATE_NORMAL,NULL);
  }
}

static void on_solo_toggled(GtkToggleButton *togglebutton,gpointer user_data) {
  BtMachine *machine=BT_MACHINE(user_data);

  if(gtk_toggle_button_get_active(togglebutton)) {
    g_object_set(machine,"state",BT_MACHINE_STATE_SOLO,NULL);
  }
  else {
    g_object_set(machine,"state",BT_MACHINE_STATE_NORMAL,NULL);
  }
}

static void on_bypass_toggled(GtkToggleButton *togglebutton,gpointer user_data) {
  BtMachine *machine=BT_MACHINE(user_data);

  if(gtk_toggle_button_get_active(togglebutton)) {
    g_object_set(machine,"state",BT_MACHINE_STATE_BYPASS,NULL);
  }
  else {
    g_object_set(machine,"state",BT_MACHINE_STATE_NORMAL,NULL);
  }
}

static void on_machine_state_changed_mute(BtMachine *machine,GParamSpec *arg,gpointer user_data) {
  GtkToggleButton *button=GTK_TOGGLE_BUTTON(user_data);
  BtMachineState state;

  g_object_get(machine,"state",&state,NULL);
  g_signal_handlers_block_matched(button,G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA,0,0,NULL,on_mute_toggled,(gpointer)machine);
  gtk_toggle_button_set_active(button,(state==BT_MACHINE_STATE_MUTE));
  g_signal_handlers_unblock_matched(button,G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA,0,0,NULL,on_mute_toggled,(gpointer)machine);
}

static void on_machine_state_changed_solo(BtMachine *machine,GParamSpec *arg,gpointer user_data) {
  GtkToggleButton *button=GTK_TOGGLE_BUTTON(user_data);
  BtMachineState state;

  g_object_get(machine,"state",&state,NULL);
  g_signal_handlers_block_matched(button,G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA,0,0,NULL,on_solo_toggled,(gpointer)machine);
  gtk_toggle_button_set_active(button,(state==BT_MACHINE_STATE_SOLO));
  g_signal_handlers_unblock_matched(button,G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA,0,0,NULL,on_solo_toggled,(gpointer)machine);
}

static void on_machine_state_changed_bypass(BtMachine *machine,GParamSpec *arg,gpointer user_data) {
  GtkToggleButton *button=GTK_TOGGLE_BUTTON(user_data);
  BtMachineState state;

  g_object_get(machine,"state",&state,NULL);
  g_signal_handlers_block_matched(button,G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA,0,0,NULL,on_bypass_toggled,(gpointer)machine);
  gtk_toggle_button_set_active(button,(state==BT_MACHINE_STATE_BYPASS));
  g_signal_handlers_unblock_matched(button,G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA,0,0,NULL,on_bypass_toggled,(gpointer)machine);
}

typedef struct {
  BtMainPageSequence *self;
  GtkVUMeter *vumeter;
  gint peak, decay;
} BtUpdateIdleData;

#define MAKE_UPDATE_IDLE_DATA(data,self,vumeter,peak,decay) G_STMT_START { \
  data=g_slice_new(BtUpdateIdleData); \
  data->self=self; \
  data->vumeter=vumeter; \
  data->peak=(gint)(peak+0.5); \
  data->decay=(gint)(decay+0.5); \
  g_mutex_lock(self->priv->lock); \
  g_object_add_weak_pointer((GObject *)self,(gpointer *)(&data->self)); \
  g_object_add_weak_pointer((GObject *)vumeter,(gpointer *)(&data->vumeter)); \
  g_mutex_unlock(self->priv->lock); \
} G_STMT_END

#define FREE_UPDATE_IDLE_DATA(data) G_STMT_START { \
  if(data->self) { \
    g_mutex_lock(data->self->priv->lock); \
    g_object_remove_weak_pointer((gpointer)data->self,(gpointer *)(&data->self)); \
    if(data->vumeter) g_object_remove_weak_pointer((gpointer)data->vumeter,(gpointer *)(&data->vumeter)); \
    g_mutex_unlock(data->self->priv->lock); \
  } \
  g_slice_free(BtUpdateIdleData,data); \
} G_STMT_END


static gboolean on_delayed_idle_track_level_change(gpointer user_data) {
  BtUpdateIdleData *data=(BtUpdateIdleData *)user_data;
  BtMainPageSequence *self=data->self;

  if(self && self->priv->is_playing && data->vumeter) {
    gtk_vumeter_set_levels(data->vumeter, data->peak, data->decay);
  }
  FREE_UPDATE_IDLE_DATA(data);
  return(FALSE);
}

static gboolean on_delayed_track_level_change(GstClock *clock,GstClockTime time,GstClockID id,gpointer user_data) {
  // the callback is called from a clock thread
  if(GST_CLOCK_TIME_IS_VALID(time))
    g_idle_add(on_delayed_idle_track_level_change,user_data);
  else {
    BtUpdateIdleData *data=(BtUpdateIdleData *)user_data;
    FREE_UPDATE_IDLE_DATA(data);
  }
  return(TRUE);
}

static void on_track_level_change(GstBus * bus, GstMessage * message, gpointer user_data) {
  const GstStructure *structure=gst_message_get_structure(message);
  const GQuark name_id=gst_structure_get_name_id(structure);

  if(name_id==bus_msg_level_quark) {
    BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
    GstElement *level=GST_ELEMENT(GST_MESSAGE_SRC(message));
    GtkVUMeter *vumeter;

    // check if its our element (we can have multiple level meters)
    if((vumeter=g_hash_table_lookup(self->priv->level_to_vumeter,level))) {
      GstClockTime timestamp, duration;
      GstClockTime waittime=GST_CLOCK_TIME_NONE;

      if(gst_structure_get_clock_time (structure, "running-time", &timestamp) &&
        gst_structure_get_clock_time (structure, "duration", &duration)) {
        /* wait for middle of buffer */
        waittime=timestamp+duration/2;
      }
      else if(gst_structure_get_clock_time (structure, "endtime", &timestamp)) {
        /* level send endtime as stream_time and not as running_time */
        waittime=gst_segment_to_running_time(&GST_BASE_TRANSFORM(level)->segment, GST_FORMAT_TIME, timestamp);
      }
      if(GST_CLOCK_TIME_IS_VALID(waittime)) {
        const GValue *l_decay,*l_peak;
        gdouble decay=0.0, peak=0.0;
        guint i,size;
        gint new_skip=FALSE,old_skip=FALSE;

        l_decay=(GValue *)gst_structure_get_value(structure, "decay");
        l_peak=(GValue *)gst_structure_get_value(structure, "peak");
        size=gst_value_list_get_size(l_decay);
        for(i=0;i<size;i++) {
          decay+=g_value_get_double(gst_value_list_get_value(l_decay,i));
          peak+=g_value_get_double(gst_value_list_get_value(l_peak,i));
        }
        if(G_UNLIKELY(isinf(decay) || isnan(decay))) {
          //GST_WARNING_OBJECT(level,"decay was INF or NAN, %lf",decay);
          decay=LOW_VUMETER_VAL;
        }
        else decay/=size;
        if(G_UNLIKELY(isinf(peak) || isnan(peak)))  {
          //GST_WARNING_OBJECT(level,"peak was INF or NAN, %lf",peak);
          peak=LOW_VUMETER_VAL;
        }
        else peak/=size;
        // check if we a silent or very loud
        if(decay<=LOW_VUMETER_VAL && peak<=LOW_VUMETER_VAL)
          new_skip=1; // below min level
        else if(decay>=0.0 && peak>=0.0)
          new_skip=2; // beyond max level
        // skip *updates* if we are still below LOW_VUMETER_VAL or beyond 0.0
        old_skip=GPOINTER_TO_INT(g_object_get_qdata((GObject *)vumeter,vu_meter_skip_update));
        g_object_set_qdata((GObject *)vumeter,vu_meter_skip_update,GINT_TO_POINTER(new_skip));
        if(!old_skip || !new_skip || old_skip!=new_skip) {
          BtUpdateIdleData *data;
          GstClockID clock_id;
          GstClockTime basetime=gst_element_get_base_time(level);

          MAKE_UPDATE_IDLE_DATA(data,self,vumeter,peak,decay);
          clock_id=gst_clock_new_single_shot_id(self->priv->clock,waittime+basetime);
          if(gst_clock_id_wait_async(clock_id,on_delayed_track_level_change,(gpointer)data)!=GST_CLOCK_OK) {
            FREE_UPDATE_IDLE_DATA(data);
          }
          gst_clock_id_unref(clock_id);
        }
        // just for counting
        //else GST_WARNING_OBJECT(level,"skipping level update");
      }
    }
  }
}

static void on_sequence_label_edited(GtkCellRendererText *cellrenderertext,gchar *path_string,gchar *new_text,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  GtkTreeModelFilter *filtered_store;
  GtkTreeModel *store;
  gulong pos;
  gchar *old_text;

  GST_INFO("label edited: '%s': '%s'",path_string,new_text);

  if((filtered_store=GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(self->priv->sequence_table))) &&
    (store=gtk_tree_model_filter_get_model(filtered_store))
  ) {
    GtkTreeIter iter,filter_iter;

    if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(filtered_store),&filter_iter,path_string)) {
      gboolean changed=FALSE;

      gtk_tree_model_filter_convert_iter_to_child_iter(filtered_store,&iter,&filter_iter);

      gtk_tree_model_get(store,&iter,BT_SEQUENCE_GRID_MODEL_POS,&pos,BT_SEQUENCE_GRID_MODEL_LABEL,&old_text,-1);
      GST_INFO("old_text '%s'",old_text);

      if(old_text || new_text) {
        changed=TRUE;
        if(old_text && !*old_text) old_text=NULL;
        if(new_text && !*new_text) new_text=NULL;
      }
      else if(old_text && new_text && !strcmp(old_text,new_text)) changed=TRUE;
      if(changed) {
      	gchar *undo_str,*redo_str;
        gulong old_length,new_length=0;

        GST_INFO("label changed");
        g_object_get(self->priv->sequence,"length",&old_length,NULL);

#ifndef USE_SEQUENCE_GRID_MODEL
        // need to change it in the model
        gtk_list_store_set(GTK_LIST_STORE(store),&iter,BT_SEQUENCE_GRID_MODEL_LABEL,new_text,-1);
#endif
        // update the sequence
        if(pos>=old_length) {
        	new_length=pos+self->priv->bars;
          g_object_set(self->priv->sequence,"length",new_length,NULL);
          sequence_calculate_visible_lines(self);
        }
        bt_sequence_set_label(self->priv->sequence,pos,new_text);
        
        bt_change_log_start_group(self->priv->change_log);

        if(pos>=old_length) {
					undo_str = g_strdup_printf("set_sequence_property \"length\",\"%ld\"",old_length);
					redo_str = g_strdup_printf("set_sequence_property \"length\",\"%ld\"",new_length);
					bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);        	
        }
				undo_str = g_strdup_printf("set_labels %lu,%lu, ,%s",pos,pos,(old_text?old_text:" "));
				redo_str = g_strdup_printf("set_labels %lu,%lu, ,%s",pos,pos,(new_text?new_text:" "));
				bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);        
				bt_change_log_end_group(self->priv->change_log);
      }
      g_free(old_text);
    }
  }
}

static void on_pos_menu_changed(GtkComboBox *combo_box,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);

  self->priv->pos_format=gtk_combo_box_get_active(combo_box);
#ifdef USE_SEQUENCE_GRID_MODEL
  BtSequenceGridModel *store=BT_SEQUENCE_GRID_MODEL(sequence_model_get_store(self));
  g_object_set(store,"pos-format",self->priv->pos_format,NULL);
#else
  BtSong *song;
  // reformat pos-column and label-menu (this is inefficient)
  g_object_get(self->priv->app,"song",&song,NULL);
  // @todo: inefficient (we ideally just want to poke new stings into the model)
  sequence_table_refresh(self,song);
  sequence_model_recolorize(self);
  g_object_unref(song);
#endif
  g_hash_table_insert(self->priv->properties,g_strdup("pos-format"),g_strdup(bt_persistence_strfmt_ulong(self->priv->pos_format)));
  bt_edit_application_set_song_unsaved(self->priv->app);
}

//-- event handler helper

/*
 * sequence_pos_table_init:
 * @self: the sequence page
 *
 * inserts the 'Pos.' column into the first (left) treeview
 */
static void sequence_pos_table_init(const BtMainPageSequence *self) {
  GtkCellRenderer *renderer;
  GtkWidget *label;
  GtkTreeViewColumn *tree_col;
  gint col_index=0;

  // empty header widget
  gtk_container_forall(GTK_CONTAINER(self->priv->sequence_pos_table_header),(GtkCallback)gtk_widget_destroy,NULL);

  // create header widget
  self->priv->pos_header=gtk_vbox_new(FALSE,HEADER_SPACING);
  // time line position
  label=gtk_label_new(_("Pos."));
  gtk_misc_set_alignment(GTK_MISC(label),0.0,0.0);
  gtk_box_pack_start(GTK_BOX(self->priv->pos_header),label,TRUE,FALSE,0);

  self->priv->pos_menu=GTK_COMBO_BOX(gtk_combo_box_new_text());
  gtk_combo_box_set_focus_on_click(self->priv->pos_menu,FALSE);
  gtk_combo_box_append_text(self->priv->pos_menu,_("Ticks"));
  gtk_combo_box_append_text(self->priv->pos_menu,_("Time"));
  gtk_combo_box_set_active(self->priv->pos_menu,self->priv->pos_format);
  gtk_box_pack_start(GTK_BOX(self->priv->pos_header),GTK_WIDGET(self->priv->pos_menu),TRUE,TRUE,0);
  //gtk_widget_set_size_request(self->priv->pos_header,POSITION_CELL_WIDTH,-1);
  g_signal_connect(self->priv->pos_menu,"changed",G_CALLBACK(on_pos_menu_changed), (gpointer)self);
  gtk_widget_show_all(self->priv->pos_header);

  gtk_box_pack_start(GTK_BOX(self->priv->sequence_pos_table_header),self->priv->pos_header,TRUE,TRUE,0);
  gtk_widget_set_size_request(GTK_WIDGET(self->priv->sequence_pos_table_header),POSITION_CELL_WIDTH,-1);

  // add static column
  renderer=gtk_cell_renderer_text_new();
  g_object_set(renderer,
    "mode",GTK_CELL_RENDERER_MODE_INERT,
    "xalign",1.0,
    "yalign",0.5,
    NULL);
  gtk_cell_renderer_set_fixed_size(renderer, 1, -1);
  gtk_cell_renderer_text_set_fixed_height_from_font(GTK_CELL_RENDERER_TEXT(renderer),1);
  if((tree_col=gtk_tree_view_column_new_with_attributes(NULL,renderer,
    "text",BT_SEQUENCE_GRID_MODEL_POSSTR,
    NULL))
  ) {
    g_object_set(tree_col,
      "sizing",GTK_TREE_VIEW_COLUMN_FIXED,
      "fixed-width",POSITION_CELL_WIDTH,
      NULL);
    col_index=gtk_tree_view_append_column(self->priv->sequence_pos_table,tree_col);
  }
  else GST_WARNING("can't create treeview column");

  GST_DEBUG("    number of columns : %d",col_index);
}
  
static void sequence_table_refresh_model(const BtMainPageSequence *self,const BtSong *song) {
#ifndef USE_SEQUENCE_GRID_MODEL
  BtPattern *pattern;
  GtkListStore *store;
  GType *store_types;
  GtkTreeIter tree_iter;
  gboolean free_str;
  gchar *pos_str, *str;
  gulong i,col_ct;
  gulong j,timeline_ct,track_ct;
#else
  BtSequenceGridModel *store;
#endif
  GtkTreeModel *filtered_store;

  GST_INFO("refresh sequence table");

#ifdef USE_SEQUENCE_GRID_MODEL
   // @todo: in the future only do this when loading a new song
   store=bt_sequence_grid_model_new(self->priv->sequence,self->priv->bars);
   g_object_set(store,"length",self->priv->sequence_length,"pos-format",self->priv->pos_format,NULL);
#else
  g_object_get(self->priv->sequence,"length",&timeline_ct,"tracks",&track_ct,NULL);
  GST_DEBUG("  size is lines=%2lu,tracks=%2lu",timeline_ct,track_ct);

  // build model
  GST_DEBUG("  build model");
  col_ct=(__BT_SEQUENCE_GRID_MODEL_N_COLUMNS+track_ct);
  store_types=(GType *)g_new(GType,col_ct);
  store_types[BT_SEQUENCE_GRID_MODEL_SHADE]=G_TYPE_BOOLEAN;
  store_types[BT_SEQUENCE_GRID_MODEL_POS  ]=G_TYPE_ULONG;
  for(i=BT_SEQUENCE_GRID_MODEL_POSSTR;i<col_ct;i++) {
    store_types[i]=G_TYPE_STRING;
  }
  store=gtk_list_store_newv(col_ct,store_types);
  g_free(store_types);

  // add patterns
  for(i=0;i<self->priv->sequence_length;i++) {
    gtk_list_store_append(store, &tree_iter);

    pos_str=sequence_format_positions(self,i);
    // set position, highlight-color
    gtk_list_store_set(store,&tree_iter,
      BT_SEQUENCE_GRID_MODEL_POS,i,
      BT_SEQUENCE_GRID_MODEL_POSSTR,pos_str,
      -1);
    if(i<timeline_ct) {
      // set label
      str=bt_sequence_get_label(self->priv->sequence,i);
      if(str) {
        gtk_list_store_set(store,&tree_iter,BT_SEQUENCE_GRID_MODEL_LABEL,str,-1);
        g_free(str);
      }

      // set patterns
      for(j=0;j<track_ct;j++) {
        free_str=FALSE;
        if((pattern=bt_sequence_get_pattern(self->priv->sequence,i,j))) {
          g_object_get(pattern,"name",&str,NULL);
          free_str=TRUE;
          g_object_unref(pattern);
        }
        else {
          str=" ";
        }
        //GST_DEBUG("  %2d,%2d : adding \"%s\"",i,j,str);
        gtk_list_store_set(store,&tree_iter,__BT_SEQUENCE_GRID_MODEL_N_COLUMNS+j,str,-1);
        if(free_str)
          g_free(str);
      }
    }
  }
#endif

  // create a filtered model to realize step filtering
  filtered_store=gtk_tree_model_filter_new(GTK_TREE_MODEL(store),NULL);
  gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filtered_store),step_visible_filter,(gpointer)self,NULL);
  // active models
  gtk_tree_view_set_model(self->priv->sequence_table,filtered_store);
  gtk_tree_view_set_model(self->priv->sequence_pos_table,filtered_store);
  g_object_unref(filtered_store); // drop with widget

  // create a filtered store for the labels menu
  filtered_store=gtk_tree_model_filter_new(GTK_TREE_MODEL(store),NULL);
  gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filtered_store),label_visible_filter,(gpointer)self,NULL);
  // active models
  gtk_combo_box_set_model(self->priv->label_menu,filtered_store);
  gtk_combo_box_set_active(self->priv->label_menu,0);
  g_object_unref(filtered_store); // drop with widget
}


/*
 * sequence_table_clear:
 * @self: the sequence page
 *
 * removes old columns
 */
static void sequence_table_clear(const BtMainPageSequence *self) {
  GList *columns,*node;
  gulong number_of_tracks;

  // remove columns
  if((columns=gtk_tree_view_get_columns(self->priv->sequence_table))) {
    for(node=g_list_first(columns);node;node=g_list_next(node)) {
      gtk_tree_view_remove_column(self->priv->sequence_table,GTK_TREE_VIEW_COLUMN(node->data));
    }
    g_list_free(columns);
  }

  // change number of tracks
  g_object_get(self->priv->sequence,"tracks",&number_of_tracks,NULL);
  if(number_of_tracks>0) {
    BtMachine *machine;
    guint i;

    // disconnect signal handlers
    for(i=0;i<number_of_tracks;i++) {
      if((machine=bt_sequence_get_machine(self->priv->sequence,i))) {
      	// even though we can have multiple tracks per machine, we can disconnect them all, as we rebuild the treeview anyway
        g_signal_handlers_disconnect_matched(machine,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_machine_state_changed_mute,NULL);
        g_signal_handlers_disconnect_matched(machine,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_machine_state_changed_solo,NULL);
        g_signal_handlers_disconnect_matched(machine,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_machine_state_changed_bypass,NULL);
        // need to disconnect the label updates for the seq headers, unfortunately we don#t know the label
        // so we use a weak_ref and on_sequence_header_label_destroy()
        GST_INFO("machine %p,ref_count=%d cleaning sequence table",machine,G_OBJECT_REF_COUNT(machine));
        g_object_unref(machine);
      }
    }
  }
}

static void remove_container_widget(GtkWidget *widget,gpointer user_data) {
  GST_LOG("removing: %d, %s",G_OBJECT_REF_COUNT(widget),gtk_widget_get_name(widget));
  gtk_container_remove(GTK_CONTAINER(user_data),widget);
}

static void reset_level_meter(gpointer key, gpointer value, gpointer user_data) {
  GtkVUMeter *vumeter=GTK_VUMETER(value);
  gtk_vumeter_set_levels(vumeter, LOW_VUMETER_VAL, LOW_VUMETER_VAL);
  g_object_set_qdata((GObject *)vumeter,vu_meter_skip_update,GINT_TO_POINTER((gint)FALSE));
}

/*
 * sequence_table_init:
 * @self: the sequence page
 *
 * inserts the Label columns.
 */
static void sequence_table_init(const BtMainPageSequence *self) {
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *tree_col;
  GtkWidget *label;
  GtkWidget *header,*vbox;
  gint col_index=0;

  GST_INFO("preparing sequence table");

  // do not destroy when flushing the header
  if((vbox=gtk_widget_get_parent(GTK_WIDGET(self->priv->label_menu)))) {
    GST_INFO("holding label widget: %d",G_OBJECT_REF_COUNT(self->priv->label_menu));
    gtk_container_remove(GTK_CONTAINER(vbox),GTK_WIDGET(g_object_ref(self->priv->label_menu)));
    //gtk_widget_unparent(GTK_WIDGET(g_object_ref(self->priv->label_menu)));
    GST_INFO("                    : %d",G_OBJECT_REF_COUNT(self->priv->label_menu));
  }
  // empty header widget
  gtk_container_forall(GTK_CONTAINER(self->priv->sequence_table_header),(GtkCallback)remove_container_widget,GTK_CONTAINER(self->priv->sequence_table_header));

  // create header widget
  header=gtk_hbox_new(FALSE,HEADER_SPACING);
  vbox=gtk_vbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(header),vbox,TRUE,TRUE,0);
  gtk_box_pack_start(GTK_BOX(header),gtk_vseparator_new(),FALSE,FALSE,0);

  label=gtk_label_new(_("Labels"));
  gtk_misc_set_alignment(GTK_MISC(label),0.0,0.0);
  gtk_box_pack_start(GTK_BOX(vbox),label,TRUE,TRUE,0);

  gtk_box_pack_start(GTK_BOX(vbox),GTK_WIDGET(self->priv->label_menu),TRUE,TRUE,0);

  /* FIXME: specifying 0, instead of -1, should yield 'as small as possible'
   * in reality it result in distorted overlapping widgets :(
   */
  gtk_widget_set_size_request(header,SEQUENCE_CELL_WIDTH,-1);
  gtk_widget_show_all(header);
  gtk_box_pack_start(GTK_BOX(self->priv->sequence_table_header),header,FALSE,FALSE,0);
  g_signal_connect(header,"size-allocate",G_CALLBACK(on_header_size_allocate),(gpointer)self);

  // re-add static columns
  renderer=gtk_cell_renderer_text_new();
  g_object_set(renderer,
    "mode",GTK_CELL_RENDERER_MODE_EDITABLE,
    "xalign",1.0,
    "yalign",0.5,
    "editable",TRUE,
    /*
    "width",SEQUENCE_CELL_WIDTH-4,
    "height",SEQUENCE_CELL_HEIGHT-4,
    "xpad",SEQUENCE_CELL_XPAD,
    "ypad",SEQUENCE_CELL_YPAD,
    */
    NULL);
  gtk_cell_renderer_set_fixed_size(renderer, 1, -1);
  gtk_cell_renderer_text_set_fixed_height_from_font(GTK_CELL_RENDERER_TEXT(renderer),1);
  g_signal_connect(renderer,"edited",G_CALLBACK(on_sequence_label_edited),(gpointer)self);
  if((tree_col=gtk_tree_view_column_new_with_attributes(_("Labels"),renderer,
    "text",BT_SEQUENCE_GRID_MODEL_LABEL,
    NULL))
  ) {
    g_object_set(tree_col,
      "sizing",GTK_TREE_VIEW_COLUMN_FIXED,
      "fixed-width",SEQUENCE_CELL_WIDTH,
      NULL);
    col_index=gtk_tree_view_append_column(self->priv->sequence_table,tree_col);
    gtk_tree_view_column_set_cell_data_func(tree_col, renderer, label_cell_data_function, (gpointer)self, NULL);
  }
  else GST_WARNING("can't create treeview column");

  if(self->priv->level_to_vumeter) g_hash_table_destroy(self->priv->level_to_vumeter);
  self->priv->level_to_vumeter=g_hash_table_new_full(NULL,NULL,(GDestroyNotify)gst_object_unref,NULL);

  GST_DEBUG("    number of columns : %d",col_index);
}

static void sequence_table_refresh_columns(const BtMainPageSequence *self,const BtSong *song) {
  gulong j,track_ct;
  BtMachine *machine;
  GtkWidget *header;
  gchar *str;
  gint col_index;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *tree_col;
  GHashTable *machine_usage;

  // build dynamic sequence view
  GST_INFO("refresh sequence view");
  
  g_object_get(self->priv->sequence,"tracks",&track_ct,NULL);

  // @todo: we'd like to update tjis instead of re-creating things
  // reset columns
  sequence_table_clear(self);
  // add initial columns
  sequence_table_init(self);

  // add column for each machine
  machine_usage=g_hash_table_new(NULL,NULL);
  for(j=0;j<track_ct;j++) {
    machine=bt_sequence_get_machine(self->priv->sequence,j);
    GST_INFO("machine %p,ref_count=%d refresh sequence table track %lu",machine,G_OBJECT_REF_COUNT(machine),j);
    renderer=gtk_cell_renderer_text_new();
    g_object_set(renderer,
      "mode",GTK_CELL_RENDERER_MODE_ACTIVATABLE,
      "xalign",0.0,
      "yalign",0.5,
      /*
      "editable",TRUE,
      "width",SEQUENCE_CELL_WIDTH-4,
      "height",SEQUENCE_CELL_HEIGHT-4,
      "xpad",SEQUENCE_CELL_XPAD,
      "ypad",SEQUENCE_CELL_YPAD,
      */
      NULL);
    gtk_cell_renderer_set_fixed_size(renderer, 1, -1);
    gtk_cell_renderer_text_set_fixed_height_from_font(GTK_CELL_RENDERER_TEXT(renderer),1);

    // setup column header
    if(machine) {
      GtkWidget *label,*button,*vbox,*box;
      GtkVUMeter *vumeter;
      GstElement *level;
      gchar *level_name="output-post-level";

      GST_DEBUG("  %3lu build column header",j);

      // enable level meters
      if(!BT_IS_SINK_MACHINE(machine)) {
        if(!bt_machine_enable_output_post_level(machine)) {
          GST_INFO("enabling output level for machine failed");
        }
      }
      else {
        // its the sink, which already has it enabled
        level_name="input-post-level";
      }
      g_object_get(machine,"id",&str,level_name,&level,NULL);

      // @todo: add context menu like that in the machine_view to the header

      // create header widget
      header=gtk_hbox_new(FALSE,HEADER_SPACING);
      vbox=gtk_vbox_new(FALSE,0);
      gtk_box_pack_start(GTK_BOX(header),vbox,TRUE,TRUE,0);
      gtk_box_pack_start(GTK_BOX(header),gtk_vseparator_new(),FALSE,FALSE,0);

      label=gtk_label_new(str);
      gtk_misc_set_alignment(GTK_MISC(label),0.0,0.0);
      g_free(str);
      gtk_box_pack_start(GTK_BOX(vbox),label,TRUE,TRUE,0);

      // disconnecting old handler here would be better, but then we need to differentiate (see below)
      g_signal_handlers_disconnect_matched(machine,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_machine_id_changed_seq,NULL);
      g_signal_connect(machine,"notify::id",G_CALLBACK(on_machine_id_changed_seq),(gpointer)label);
      // we need to remove the signal handler when updating the labels
      //g_object_weak_ref(G_OBJECT(label),on_sequence_header_label_destroy,machine);
      /* we have the label column already
      if(j==0) {
        // connect to the size-allocate signal to adjust the height of the other treeview header
        g_signal_connect(header,"size-allocate",G_CALLBACK(on_header_size_allocate),(gpointer)self);
      }
      */

      box=gtk_hbox_new(FALSE,0);
      gtk_box_pack_start(GTK_BOX(vbox),GTK_WIDGET(box),TRUE,TRUE,0);

      /* only do this for first track of a machine
       * - multiple level-meter views for same machine don't work
       * - MSB buttons would need to be synced
       */
      if (!g_hash_table_lookup(machine_usage,machine)) {
        BtMachineState state;

        g_object_get(machine,"state",&state,NULL);

        g_hash_table_insert(machine_usage,machine,machine);
        // add M/S/B butons and connect signal handlers
        // @todo: use colors from ui-resources
        button=make_mini_button("M",1.2, 1.0/1.25, 1.0/1.25,(state==BT_MACHINE_STATE_MUTE)); // red
        gtk_box_pack_start(GTK_BOX(box),button,FALSE,FALSE,0);
        g_signal_connect(button,"toggled",G_CALLBACK(on_mute_toggled),(gpointer)machine);
        g_signal_connect(machine,"notify::state", G_CALLBACK(on_machine_state_changed_mute), (gpointer)button);

        if(BT_IS_SOURCE_MACHINE(machine)) {
          button=make_mini_button("S",1.0/1.2,1.0/1.2,1.1,(state==BT_MACHINE_STATE_SOLO)); // blue
          gtk_box_pack_start(GTK_BOX(box),button,FALSE,FALSE,0);
          g_signal_connect(button,"toggled",G_CALLBACK(on_solo_toggled),(gpointer)machine);
          g_signal_connect(machine,"notify::state", G_CALLBACK(on_machine_state_changed_solo), (gpointer)button);
        }

        if(BT_IS_PROCESSOR_MACHINE(machine)) {
          button=make_mini_button("B",1.2,1.0/1.1,1.0/1.4,(state==BT_MACHINE_STATE_BYPASS)); // orange
          gtk_box_pack_start(GTK_BOX(box),button,FALSE,FALSE,0);
          g_signal_connect(button,"toggled",G_CALLBACK(on_bypass_toggled),(gpointer)machine);
          g_signal_connect(machine,"notify::state", G_CALLBACK(on_machine_state_changed_bypass), (gpointer)button);
        }
        vumeter=GTK_VUMETER(gtk_vumeter_new(FALSE));
        gtk_vumeter_set_min_max(vumeter, LOW_VUMETER_VAL, 0);
        // no falloff in widget, we have falloff in GstLevel
        //gtk_vumeter_set_peaks_falloff(vumeter, GTK_VUMETER_PEAKS_FALLOFF_MEDIUM);
        gtk_vumeter_set_scale(vumeter, GTK_VUMETER_SCALE_LINEAR);
        reset_level_meter(level, vumeter, NULL);
        gtk_box_pack_start(GTK_BOX(box),GTK_WIDGET(vumeter),TRUE,TRUE,0);

        // add level meters to hashtable
        if(level) {
          g_hash_table_insert(self->priv->level_to_vumeter,level,vumeter);
        }
      }
    }
    else {
      // a missing machine
      header=gtk_label_new("???");
      GST_WARNING("can't get machine for column %lu",j);
    }
    gtk_widget_set_size_request(header,SEQUENCE_CELL_WIDTH,-1);
    gtk_widget_show_all(header);
    gtk_box_pack_start(GTK_BOX(self->priv->sequence_table_header),header,FALSE,FALSE,0);

    if((tree_col=gtk_tree_view_column_new_with_attributes(NULL,renderer, NULL))) {
      g_object_set(tree_col,
        "sizing",GTK_TREE_VIEW_COLUMN_FIXED,
        "fixed-width",SEQUENCE_CELL_WIDTH,
        NULL);
      g_object_set_qdata(G_OBJECT(tree_col),column_index_quark,GUINT_TO_POINTER(j));
      gtk_tree_view_append_column(self->priv->sequence_table,tree_col);

      // color code columns
      if(BT_IS_SOURCE_MACHINE(machine)) {
        gtk_tree_view_column_set_cell_data_func(tree_col, renderer, source_machine_cell_data_function, (gpointer)self, NULL);
      }
      else if(BT_IS_PROCESSOR_MACHINE(machine)) {
        gtk_tree_view_column_set_cell_data_func(tree_col, renderer, processor_machine_cell_data_function, (gpointer)self, NULL);
      }
      else if(BT_IS_SINK_MACHINE(machine)) {
        gtk_tree_view_column_set_cell_data_func(tree_col, renderer, sink_machine_cell_data_function, (gpointer)self, NULL);
      }
    }
    else GST_WARNING("can't create treeview column");
    g_object_try_unref(machine);
  }
  g_hash_table_destroy(machine_usage);

  GST_INFO("finish sequence table");

  // add a final column that eats remaining space
  renderer=gtk_cell_renderer_text_new();
  g_object_set(renderer,
    "mode",GTK_CELL_RENDERER_MODE_INERT,
    NULL);
  gtk_cell_renderer_set_fixed_size(renderer, 1, -1);
  gtk_cell_renderer_text_set_fixed_height_from_font(GTK_CELL_RENDERER_TEXT(renderer), 1);

  header=gtk_label_new(" ");
  // sad, but true, this matters, otherwise we get leftover artifacts when scrolling <->
  gtk_label_set_width_chars(GTK_LABEL(header),5);
  gtk_label_set_single_line_mode(GTK_LABEL(header),TRUE);
  gtk_label_set_line_wrap(GTK_LABEL(header),FALSE);

  gtk_widget_show(header);
  gtk_box_pack_start(GTK_BOX(self->priv->sequence_table_header),header,TRUE,TRUE,0);
  if((tree_col=gtk_tree_view_column_new_with_attributes(/*title=*/NULL,renderer,NULL))) {
    g_object_set(tree_col,
      "sizing",GTK_TREE_VIEW_COLUMN_FIXED,
      NULL);
    col_index=gtk_tree_view_append_column(self->priv->sequence_table,tree_col);
    GST_DEBUG("    number of columns : %d",col_index);
  }
  else GST_WARNING("can't create treeview column");
}

/*
 * sequence_table_refresh:
 * @self:  the sequence page
 * @song: the newly created song
 *
 * rebuild the sequence table after a structural change
 */
static void sequence_table_refresh(const BtMainPageSequence *self,const BtSong *song) {
  sequence_table_refresh_model(self,song);
  sequence_table_refresh_columns(self,song);
}


static void pattern_list_refresh(const BtMainPageSequence *self) {
  BtPatternListModel *store;

  // refresh the pattern list
  if(self->priv->machine) {

    GST_INFO("refresh pattern list for machine : %p,ref_count=%d",self->priv->machine,G_OBJECT_REF_COUNT(self->priv->machine));

    store=bt_pattern_list_model_new(self->priv->machine,self->priv->sequence,FALSE);

    // sync machine in pattern page
    if(self->priv->main_window) {
      BtMainPagePatterns *patterns_page;

      bt_child_proxy_get(self->priv->main_window,"pages::patterns-page",&patterns_page,NULL);
      bt_main_page_patterns_show_machine(patterns_page,self->priv->machine);
      g_object_unref(patterns_page);
    }

    GST_INFO("refreshed pattern list for machine : %p,ref_count=%d",self->priv->machine,G_OBJECT_REF_COUNT(self->priv->machine));
  }
  else {
    // FIXME, do we need a dummy store? - yes for the column headers
    //store=gtk_list_store_new(3,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_STRING);
    store=NULL;
    GST_INFO("no machine for cursor_column: %ld",self->priv->cursor_column);
  }
  gtk_tree_view_set_model(self->priv->pattern_list,GTK_TREE_MODEL(store));

  g_object_try_unref(store); // drop with treeview
}


/*
 * update_after_track_changed:
 * @self: the sequence page
 *
 * When the user moves the cursor in the sequence, update the list of patterns
 * so that it shows the patterns that belong to the machine in the current
 * sequence row.
 * Also update the current selected machine in pattern view.
 */
static void update_after_track_changed(const BtMainPageSequence *self) {
  BtMachine *machine;

  GST_INFO("change active track");

  machine=bt_sequence_get_machine(self->priv->sequence,self->priv->cursor_column-1);
  if(machine==self->priv->machine) {
    // nothing changed
    g_object_try_unref(machine);
    return;
  }

  GST_INFO("changing machine %p,ref_count=%d to %p,ref_count=%d",
    self->priv->machine,G_OBJECT_REF_COUNT(self->priv->machine),
    machine,G_OBJECT_REF_COUNT(machine)
  );

  if(self->priv->machine) {
    GST_INFO("unref old cur-machine %p,refs=%d",self->priv->machine,G_OBJECT_REF_COUNT(self->priv->machine));
    g_signal_handler_disconnect(self->priv->machine,self->priv->pattern_removed_handler);
    // unref the old machine
    g_object_unref(self->priv->machine);
    self->priv->machine=NULL;
    self->priv->pattern_removed_handler=0;
  }
  if(machine) {
    GST_INFO("ref new cur-machine: refs: %d",G_OBJECT_REF_COUNT(machine));
    self->priv->pattern_removed_handler=g_signal_connect(machine,"pattern-removed",G_CALLBACK(on_pattern_removed),(gpointer)self);
    // remember the new machine
    self->priv->machine=machine;
  }
  pattern_list_refresh(self);
}

/*
 * machine_menu_refresh:
 * add all machines from setup to self->priv->context_menu_add
 */
static void machine_menu_refresh(const BtMainPageSequence *self,const BtSetup *setup) {
  BtMachine *machine;
  GList *node,*list,*widgets;
  GtkWidget *menu_item,*submenu,*label;
  gchar *str;

  GST_INFO("refreshing track menu");

  // (re)create a new menu
  submenu=gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(self->priv->context_menu_add),submenu);

  // fill machine menu
  g_object_get((gpointer)setup,"machines",&list,NULL);
  for(node=list;node;node=g_list_next(node)) {
    machine=BT_MACHINE(node->data);
    g_object_get(machine,"id",&str,NULL);

    menu_item=gtk_image_menu_item_new_with_label(str);
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menu_item),bt_ui_resources_get_icon_image_by_machine(machine));
    gtk_menu_shell_append(GTK_MENU_SHELL(submenu),menu_item);
    gtk_widget_show(menu_item);
    widgets=gtk_container_get_children(GTK_CONTAINER(menu_item));
    label=g_list_nth_data(widgets,0);
    if(GTK_IS_LABEL(label)) {
      GST_DEBUG("menu item for machine %p,ref_count=%d",machine,G_OBJECT_REF_COUNT(machine));
      g_signal_handlers_disconnect_matched(machine,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_machine_id_changed_menu,NULL);
      g_signal_connect(machine,"notify::id",G_CALLBACK(on_machine_id_changed_menu),(gpointer)label);
      // we need to remove the signal handler when updating the labels
      //g_object_weak_ref(G_OBJECT(label),on_sequence_header_label_destroy,machine);
    }
    g_signal_connect(menu_item,"activate",G_CALLBACK(on_track_add_activated),(gpointer)self);
    g_list_free(widgets);
    g_free(str);
  }
  g_list_free(list);
}

/*
 * sequence_view_set_pos:
 *
 * set play, loop-start/end or length bars
 */
static void sequence_view_set_pos(const BtMainPageSequence *self,gulong type,glong row) {
  BtSong *song;
  gulong sequence_length;
  gdouble pos;
  glong play_pos,loop_start,loop_end;
  glong old_loop_start,old_loop_end;
  gchar *undo_str,*redo_str;

  g_object_get(self->priv->app,"song",&song,NULL);
  g_object_get(song,"play-pos",&play_pos,NULL);
  g_object_get(self->priv->sequence,"length",&sequence_length,"loop-start",&loop_start,"loop-end",&loop_end,NULL);
  if(row==-1) row=sequence_length;
  // use a keyboard qualifier to set loop_start and end
  /* @todo should the sequence-view listen to notify::xxx ? */
  switch(type) {
    case SEQUENCE_VIEW_POS_PLAY:
      if(play_pos!=row) {
        g_object_set(song,"play-pos",row,NULL);
      }
      break;
    case SEQUENCE_VIEW_POS_LOOP_START:
    	g_object_get(self->priv->sequence,"loop-start",&old_loop_start,NULL);
      // set and read back, as sequence might clamp the value
      g_object_set(self->priv->sequence,"loop-start",row,NULL);
      g_object_get(self->priv->sequence,"loop-start",&loop_start,NULL);
      
      if(loop_start!=old_loop_start) {
      	bt_change_log_start_group(self->priv->change_log);

				undo_str = g_strdup_printf("set_sequence_property \"loop-start\",\"%ld\"",old_loop_start);
				redo_str = g_strdup_printf("set_sequence_property \"loop-start\",\"%ld\"",loop_start);
				bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);
      	
				pos=(gdouble)loop_start/(gdouble)sequence_length;
				g_object_set(self->priv->sequence_table,"loop-start",pos,NULL);
				g_object_set(self->priv->sequence_pos_table,"loop-start",pos,NULL);
	
				GST_INFO("adjusted loop-end = %ld -> %ld",old_loop_start,loop_start);
	
				g_object_get(self->priv->sequence,"loop-end",&old_loop_end,NULL);
				if((old_loop_end!=-1) && (old_loop_end<=old_loop_start)) {
					loop_end=loop_start+self->priv->bars;
					g_object_set(self->priv->sequence,"loop-end",loop_end,NULL);
	
					undo_str = g_strdup_printf("set_sequence_property \"loop-end\",\"%ld\"",old_loop_end);
					redo_str = g_strdup_printf("set_sequence_property \"loop-end\",\"%ld\"",loop_end);
					bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);
	
					GST_INFO("adjusted loop-end = %ld -> %ld",old_loop_end,loop_end);
	
					pos=(gdouble)loop_end/(gdouble)sequence_length;
					g_object_set(self->priv->sequence_table,"loop-end",pos,NULL);
					g_object_set(self->priv->sequence_pos_table,"loop-end",pos,NULL);
				}
				
				bt_change_log_end_group(self->priv->change_log);
			}
      break;
    case SEQUENCE_VIEW_POS_LOOP_END:
      // pos is beyond length or is on loop-end already -> adjust length
      if((row>sequence_length) || (row==loop_end)) {
        GST_INFO("adjusted length = %ld -> %ld",sequence_length,row);
        
        bt_change_log_start_group(self->priv->change_log);

				undo_str = g_strdup_printf("set_sequence_property \"length\",\"%ld\"",sequence_length);
				redo_str = g_strdup_printf("set_sequence_property \"length\",\"%ld\"",row);
				bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);

        // we shorten the song, backup data
        if(row<sequence_length) {
					GString *old_data=g_string_new(NULL);
					gulong number_of_tracks;

					g_object_get(self->priv->sequence,"tracks",&number_of_tracks,NULL);
					sequence_range_copy(self,0,number_of_tracks,row,sequence_length-1,old_data);
					sequence_range_log_undo_redo(self,0,number_of_tracks,row,sequence_length-1,old_data->str,g_strdup(old_data->str));
					g_string_free(old_data,TRUE);
        }				
				bt_change_log_end_group(self->priv->change_log);
        
        sequence_length=row;
        g_object_set(self->priv->sequence,"length",sequence_length,NULL);
#ifndef USE_SEQUENCE_GRID_MODEL
        // this triggers redraw
        sequence_table_refresh(self,song);
        sequence_view_set_cursor_pos(self);
#endif
        sequence_calculate_visible_lines(self);
        sequence_model_recolorize(self);        
      }
      else {
      	old_loop_end=loop_end;
        // set and read back, as sequence might clamp the value
        g_object_set(self->priv->sequence,"loop-end",row,NULL);
        g_object_get(self->priv->sequence,"loop-end",&loop_end,NULL);

				if(loop_end!=old_loop_end) {
					bt_change_log_start_group(self->priv->change_log);

					GST_INFO("adjusted loop-end = %ld -> %ld",old_loop_end,loop_end);

					undo_str = g_strdup_printf("set_sequence_property \"loop-end\",\"%ld\"",old_loop_end);
					redo_str = g_strdup_printf("set_sequence_property \"loop-end\",\"%ld\"",loop_end);
					bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);
	
					g_object_get(self->priv->sequence,"loop-start",&old_loop_start,NULL);
					if((old_loop_start!=-1) && (old_loop_start>=loop_end)) {
						loop_start=loop_end-self->priv->bars;
						if(loop_start<0) loop_start=0;
						g_object_set(self->priv->sequence,"loop-start",loop_start,NULL);
	
						undo_str = g_strdup_printf("set_sequence_property \"loop-start\",\"%ld\"",old_loop_start);
						redo_str = g_strdup_printf("set_sequence_property \"loop-start\",\"%ld\"",loop_start);
						bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);

						GST_INFO("and adjusted loop-start = %ld",loop_start);
					}
					
					bt_change_log_end_group(self->priv->change_log);
				}
      }
      pos=(loop_end>-1)?(gdouble)loop_end/(gdouble)sequence_length:1.0;
      g_object_set(self->priv->sequence_table,"loop-end",pos,NULL);
      g_object_set(self->priv->sequence_pos_table,"loop-end",pos,NULL);

      pos=(loop_start>-1)?(gdouble)loop_start/(gdouble)sequence_length:0.0;
      g_object_set(self->priv->sequence_table,"loop-start",pos,NULL);
      g_object_set(self->priv->sequence_pos_table,"loop-start",pos,NULL);
      break;
  }
  g_object_unref(song);
}

/*
 * sequence_add_track:
 * @pos: the track position (-1 at the end)
 *
 * add a new track for the machine at the given position
 */
static void sequence_add_track(const BtMainPageSequence *self,BtMachine *machine,glong pos) {
  BtSong *song;
  GList *columns;

  // get song from app and then setup from song
  g_object_get(self->priv->app,"song",&song,NULL);
	
  bt_sequence_add_track(self->priv->sequence,machine,pos);

  GST_INFO("machine %p,ref_count=%d track added",machine,G_OBJECT_REF_COUNT(machine));

  // reset selection
  self->priv->selection_start_column=self->priv->selection_start_row=self->priv->selection_end_column=self->priv->selection_end_row=-1;

  // reinit the view
#ifndef USE_SEQUENCE_GRID_MODEL
  sequence_table_refresh_model(self,song);
#endif
  sequence_table_refresh_columns(self,song);
  sequence_model_recolorize(self);

  GST_INFO("machine %p,ref_count=%d sequence table update",machine,G_OBJECT_REF_COUNT(machine));

  // update cursor_column and focus cell
  // (-2 because last column is empty and first is label)
  columns=gtk_tree_view_get_columns(self->priv->sequence_table);
  self->priv->cursor_column=g_list_length(columns)-2;
  GST_INFO("new cursor column: %ld",self->priv->cursor_column);
  g_list_free(columns);
  sequence_view_set_cursor_pos(self);

  GST_INFO("machine %p,ref_count=%d cursor moved",machine,G_OBJECT_REF_COUNT(machine));

  update_after_track_changed(self);

  GST_INFO("machine %p,ref_count=%d adding track and updates done",machine,G_OBJECT_REF_COUNT(machine));

  g_object_unref(song);
}

static void sequence_remove_track(const BtMainPageSequence *self,gulong ix) {
	BtMachine *machine;

	if((machine=bt_sequence_get_machine(self->priv->sequence,ix))) {
		// even though we can have multiple tracks per machine, we can disconnect them all, as we rebuild the treeview anyway
		// FIXME: be careful when using the new sequence model
		g_signal_handlers_disconnect_matched(machine,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_machine_state_changed_mute,NULL);
		g_signal_handlers_disconnect_matched(machine,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_machine_state_changed_solo,NULL);
		g_signal_handlers_disconnect_matched(machine,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_machine_state_changed_bypass,NULL);
		GST_INFO("machine %p,ref_count=%d removing track",machine,G_OBJECT_REF_COUNT(machine));
	
		// remove the track where the cursor is
		bt_sequence_remove_track_by_ix(self->priv->sequence,ix);
	
		g_object_unref(machine);
	}
}

static gboolean update_bars_menu(const BtMainPageSequence *self,gulong bars) {
  GtkListStore *store;
  GtkTreeIter iter;
  gchar str[5];
  gulong i,j;
  gint active=2;
  gint selected=-1;
  gint added=0;
  /* the useful stepping depends on the rythm
     beats=bars/tpb
     bars=16, beats=4, tpb=4 : 4/4 -> 1,8, 16,32,64
     bars=12, beats=3, tpb=4 : 3/4 -> 1,6, 12,24,48
     bars=18, beats=3, tpb=6 : 3/6 -> 1,9, 18,36,72
  */
  store=gtk_list_store_new(1,G_TYPE_STRING);

  if(bars!=1) {
    // single steps
    gtk_list_store_append(store,&iter);
    gtk_list_store_set(store,&iter,0,"1",-1);
    if(self->priv->bars==1) selected=added;
    added++;
  }
  else {
    active--;
  }
  if(bars/2>1) {
    // half bars
    sprintf(str,"%lu",bars/2);
    gtk_list_store_append(store,&iter);
    gtk_list_store_set(store,&iter,0,str,-1);
    if(self->priv->bars==(bars/2)) selected=added;
    added++;
  }
  else {
    active--;
  }
  // add bars and 3 times the double of bars
  for(j=0,i=bars;j<4;i*=2,j++) {
    sprintf(str,"%lu",i);
    gtk_list_store_append(store,&iter);
    gtk_list_store_set(store,&iter,0,str,-1);
    if(self->priv->bars==i) selected=added;
    added++;
  }
  if(selected>-1) active=selected;
  gtk_combo_box_set_model(self->priv->bars_menu,GTK_TREE_MODEL(store));
  gtk_combo_box_set_active(self->priv->bars_menu,active);
  g_object_unref(store); // drop with combobox

  return(TRUE);
}

//-- event handler

static void on_track_add_activated(GtkMenuItem *menu_item, gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  GList *widgets;
  GtkWidget *label;

  // get the machine by the menuitems name
  //id=(gchar *)gtk_widget_get_name(GTK_WIDGET(menu_item));
  widgets=gtk_container_get_children(GTK_CONTAINER(menu_item));
  label=g_list_nth_data(widgets,0);
  if(GTK_IS_LABEL(label)) {
  	const gchar *id;
    BtSong *song;
    BtSetup *setup;
    BtMachine *machine;

    // get song from app and then setup from song
    g_object_get(self->priv->app,"song",&song,NULL);
    g_object_get(song,"setup",&setup,NULL);

    id=gtk_label_get_text(GTK_LABEL(label));
    GST_INFO("adding track for machine \"%s\"",id);
    if((machine=bt_setup_get_machine_by_id(setup,id))) {
			gchar *undo_str,*redo_str;
			gchar *mid;
			gulong ix;

      g_object_get(self->priv->sequence,"tracks",&ix,NULL);

      sequence_add_track(self,machine,-1);
      
			g_object_get(machine,"id",&mid,NULL);

      /* handle undo/redo */
			undo_str = g_strdup_printf("rem_track %lu",ix);
			redo_str = g_strdup_printf("add_track \"%s\",%lu",mid,ix);
			bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);
			g_free(mid);
      
      g_object_unref(machine);
    }
    g_object_unref(setup);
    g_object_unref(song);
  }
  g_list_free(widgets);
}

static void on_track_remove_activated(GtkMenuItem *menuitem, gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gulong number_of_tracks,sequence_length;

  // change number of tracks
  g_object_get(self->priv->sequence,"tracks",&number_of_tracks,"length",&sequence_length,NULL);
  if(number_of_tracks>0) {
		BtMachine *machine;
		gulong ix=self->priv->cursor_column-1;

   	if((machine=bt_sequence_get_machine(self->priv->sequence,ix))) {
   		BtSong *song;
   		GString *old_data=g_string_new(NULL);
			gchar *undo_str,*redo_str;
			gchar *mid;

			g_object_get(machine,"id",&mid,NULL);
		
			/* handle undo/redo */                 
			bt_change_log_start_group(self->priv->change_log);
			undo_str = g_strdup_printf("add_track \"%s\",%lu",mid,ix);
			redo_str = g_strdup_printf("rem_track %lu",ix);
			bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);
			
			sequence_range_copy(self,ix+1,ix+1,0,sequence_length-1,old_data);
			sequence_range_log_undo_redo(self,ix+1,ix+1,0,sequence_length-1,old_data->str,g_strdup(old_data->str));
			g_string_free(old_data,TRUE);

			bt_change_log_end_group(self->priv->change_log);

			GST_DEBUG("old cursor column: %ld, tracks: %lu",self->priv->cursor_column,number_of_tracks);
			sequence_remove_track(self,self->priv->cursor_column-1);
			
			// reset selection
			self->priv->selection_start_column=self->priv->selection_start_row=self->priv->selection_end_column=self->priv->selection_end_row=-1;
	
			// get song from app
			g_object_get(self->priv->app,"song",&song,NULL);
	
			// reinit the view
#ifndef USE_SEQUENCE_GRID_MODEL
      sequence_table_refresh_model(self,song);
#endif
      sequence_table_refresh_columns(self,song);
			sequence_model_recolorize(self);
	
			if(self->priv->cursor_column>=number_of_tracks) {
				// update cursor_column and focus cell
				self->priv->cursor_column--;
				sequence_view_set_cursor_pos(self);
				GST_DEBUG("new cursor column: %ld",self->priv->cursor_column);
			}
	
			update_after_track_changed(self);
	
			g_free(mid);
			g_object_unref(song);
			g_object_unref(machine);
		}
  }
}

static void on_track_move_left_activated(GtkMenuItem *menuitem, gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gulong track=self->priv->cursor_column-1;

  GST_INFO("move track %ld to left",self->priv->cursor_column);

  if(track>0) {
    if(bt_sequence_move_track_left(self->priv->sequence,track)) {
      BtSong *song;
      gchar *undo_str,*redo_str;
      
      // get song from app
      g_object_get(self->priv->app,"song",&song,NULL);
  
      self->priv->cursor_column--;
      // reinit the view
#ifndef USE_SEQUENCE_GRID_MODEL
      sequence_table_refresh_model(self,song);
#endif
      sequence_table_refresh_columns(self,song);
      sequence_model_recolorize(self);
      sequence_view_set_cursor_pos(self);

      /* handle undo/redo */
			undo_str = g_strdup_printf("move_track %lu,%lu",track-1,track);
			redo_str = g_strdup_printf("move_track %lu,%lu",track,track-1);
			bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);

      g_object_unref(song);
    }
  }
}

static void on_track_move_right_activated(GtkMenuItem *menuitem, gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gulong track=self->priv->cursor_column-1,number_of_tracks;

  GST_INFO("move track %ld to right",self->priv->cursor_column);

  g_object_get(self->priv->sequence,"tracks",&number_of_tracks,NULL);

  if(track<number_of_tracks) {
    if(bt_sequence_move_track_right(self->priv->sequence,track)) {
      BtSong *song;
      gchar *undo_str,*redo_str;

      // get song from app
      g_object_get(self->priv->app,"song",&song,NULL);

      self->priv->cursor_column++;
      // reinit the view
#ifndef USE_SEQUENCE_GRID_MODEL
      sequence_table_refresh_model(self,song);
#endif
      sequence_table_refresh_columns(self,song);
      sequence_model_recolorize(self);
      sequence_view_set_cursor_pos(self);

      /* handle undo/redo */
			undo_str = g_strdup_printf("move_track %lu,%lu",track+1,track);
			redo_str = g_strdup_printf("move_track %lu,%lu",track,track+1);
			bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);

      g_object_unref(song);
    }
  }
}

static void on_context_menu_machine_properties_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);

  bt_machine_show_properties_dialog(self->priv->machine);
}

static void on_context_menu_machine_preferences_activate(GtkMenuItem *menuitem,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);

  bt_machine_show_preferences_dialog(self->priv->machine);
}


static void on_song_play_pos_notify(const BtSong *song,GParamSpec *arg,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gdouble play_pos;
  gulong sequence_length,pos;
  GtkTreePath *path;

  // calculate fractional pos and set into sequence-viewer
  g_object_get((gpointer)song,"play-pos",&pos,NULL);
  g_object_get(self->priv->sequence,"length",&sequence_length,NULL);
  play_pos=(gdouble)pos/(gdouble)sequence_length;
  if(play_pos<=1.0) {
    g_object_set(self->priv->sequence_table,"play-position",play_pos,NULL);
    g_object_set(self->priv->sequence_pos_table,"play-position",play_pos,NULL);
  }

  //GST_DEBUG("sequence tick received : %d",pos);

  // do nothing for invisible rows
  if(IS_SEQUENCE_POS_VISIBLE(pos,self->priv->bars)) {
    // scroll  to make play pos visible
    if((path=gtk_tree_path_new_from_indices((pos/self->priv->bars),-1))) {
      // that would try to keep the cursor in the middle (means it will scroll more)
      if(gtk_widget_get_realized(GTK_WIDGET(self->priv->sequence_table))) {
        gtk_tree_view_scroll_to_cell(self->priv->sequence_table,path,NULL,TRUE,0.5,0.5);
        //gtk_tree_view_scroll_to_cell(self->priv->sequence_table,path,NULL,FALSE,0.0,0.0);
      }
      if(gtk_widget_get_realized(GTK_WIDGET(self->priv->sequence_pos_table))) {
        gtk_tree_view_scroll_to_cell(self->priv->sequence_pos_table,path,NULL,TRUE,0.5,0.5);
      }
      gtk_tree_path_free(path);
    }
  }
}

static void on_song_is_playing_notify(const BtSong *song,GParamSpec *arg,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);

  g_object_get((gpointer)song,"is-playing",&self->priv->is_playing,NULL);
  // stop all level meters
  if(!self->priv->is_playing) {
    g_hash_table_foreach(self->priv->level_to_vumeter,reset_level_meter,NULL);
  }
}

static void on_bars_menu_changed(GtkComboBox *combo_box,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  GtkTreeModel *store;
  GtkTreeIter iter;

  GST_INFO("bars_menu has changed : page=%p",user_data);

  if((store=gtk_combo_box_get_model(self->priv->bars_menu))
    && gtk_combo_box_get_active_iter(self->priv->bars_menu,&iter))
  {
    gchar *str;
    gulong old_bars=self->priv->bars;

    gtk_tree_model_get(store,&iter,0,&str,-1);
    self->priv->bars=atoi(str);
    g_free(str);
    
    if(self->priv->bars!=old_bars) {
      GtkTreeModelFilter *filtered_store;

      sequence_calculate_visible_lines(self);
      sequence_model_recolorize(self);
      //GST_INFO("  bars = %d",self->priv->bars);
      if((filtered_store=GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(self->priv->sequence_table)))) {
#ifdef USE_SEQUENCE_GRID_MODEL
        BtSequenceGridModel *store=BT_SEQUENCE_GRID_MODEL(gtk_tree_model_filter_get_model(filtered_store));
        g_object_set(store,"bars",self->priv->bars,NULL);
#endif
        gtk_tree_model_filter_refilter(filtered_store);
      }
      g_hash_table_insert(self->priv->properties,g_strdup("bars"),g_strdup(bt_persistence_strfmt_ulong(self->priv->bars)));
      bt_edit_application_set_song_unsaved(self->priv->app);
    }
    gtk_widget_grab_focus_savely(GTK_WIDGET(self->priv->sequence_table));
  }
}

static void on_toolbar_menu_clicked(GtkButton *button, gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);

  gtk_menu_popup(self->priv->context_menu,NULL,NULL,NULL,NULL,1,gtk_get_current_event_time());
}

static void on_label_menu_changed(GtkComboBox *combo_box,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  GtkTreeModel *store;
  GtkTreeIter iter;

  GST_INFO("label_menu has changed : page=%p",user_data);

  if((store=gtk_combo_box_get_model(self->priv->label_menu))
    && gtk_combo_box_get_active_iter(self->priv->label_menu,&iter))
  {
    GtkTreePath *path;
    glong pos;

    gtk_tree_model_get(store,&iter,BT_SEQUENCE_GRID_MODEL_POS,&pos,-1);
    GST_INFO("  move to = %ld",pos);
    if((path=gtk_tree_path_new_from_indices((pos/self->priv->bars),-1))) {
      // that would try to keep the cursor in the middle (means it will scroll more)
      if(gtk_widget_get_realized(GTK_WIDGET(self->priv->sequence_table))) {
        gtk_tree_view_scroll_to_cell(self->priv->sequence_table,path,NULL,TRUE,0.5,0.5);
      }
      gtk_tree_path_free(path);
    }
  }
}

static gboolean on_sequence_table_cursor_changed_idle(gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  GtkTreePath *path;
  GtkTreeViewColumn *column;
  gulong cursor_column,cursor_row;
  GList *columns;

  g_return_val_if_fail(user_data,FALSE);

  //GST_INFO("sequence_table cursor has changed : self=%p",user_data);

  gtk_tree_view_get_cursor(self->priv->sequence_table,&path,&column);
  if(column && path) {
    if(sequence_view_get_cursor_pos(self->priv->sequence_table,path,column,&cursor_column,&cursor_row)) {
      gulong last_line,last_bar,last_column;

      columns=gtk_tree_view_get_columns(self->priv->sequence_table);
      last_column=g_list_length(columns)-2;
      g_list_free(columns);

      GST_INFO("new row = %3lu <-> old row = %3ld",cursor_row,self->priv->cursor_row);
      self->priv->cursor_row=cursor_row;

      if(cursor_column>last_column) {
        cursor_column=last_column;
        sequence_view_set_cursor_pos(self);
      }

      GST_INFO("new col = %3lu <-> old col = %3ld",cursor_column,self->priv->cursor_column);
      if(cursor_column!=self->priv->cursor_column) {
        self->priv->cursor_column=cursor_column;
        update_after_track_changed(self);
      }
      GST_INFO("cursor has changed: %3ld,%3ld",self->priv->cursor_column,self->priv->cursor_row);

      // calculate the last visible row from step-filter and scroll-filter
      last_line=self->priv->sequence_length-1;
      last_bar=last_line-(last_line%self->priv->bars);

      // do we need to extend sequence?
      if(cursor_row>=last_bar) {
        GtkTreeModelFilter *filtered_store;
#ifndef USE_SEQUENCE_GRID_MODEL
        GtkTreeIter tree_iter;
        GtkListStore *store;
        gulong pos;
        gchar *pos_str;

        pos=self->priv->sequence_length;
#else
        BtSequenceGridModel *store;
#endif
        self->priv->sequence_length+=self->priv->bars;

#ifndef USE_SEQUENCE_GRID_MODEL
        store=GTK_LIST_STORE(sequence_model_get_store(self));
        for(;pos<self->priv->sequence_length;pos++) {
          gtk_list_store_append(store, &tree_iter);
					pos_str=sequence_format_positions(self,pos);
					// set position, highlight-color
					gtk_list_store_set(store,&tree_iter,
						BT_SEQUENCE_GRID_MODEL_POS,pos,
						BT_SEQUENCE_GRID_MODEL_POSSTR,pos_str,
						-1);
        }
#else
        store=BT_SEQUENCE_GRID_MODEL(sequence_model_get_store(self));
        g_object_set(store,"length",self->priv->sequence_length,NULL);
#endif
        // this is not optimal
        sequence_model_recolorize(self);

        if((filtered_store=GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(self->priv->sequence_table)))) {
          gtk_tree_model_filter_refilter(filtered_store);
        }
        gtk_tree_view_set_cursor(self->priv->sequence_table,path,column,FALSE);
        gtk_widget_grab_focus_savely(GTK_WIDGET(self->priv->sequence_table));
      }
      gtk_tree_view_scroll_to_cell(self->priv->sequence_table,path,column,FALSE,1.0,0.0);
      gtk_widget_queue_draw(GTK_WIDGET(self->priv->sequence_table));
    }
  }
  else {
    GST_INFO("No cursor pos, column=%p, path=%p",column,path);
  }
  if(path) gtk_tree_path_free(path);

  return(FALSE);
}

static void on_sequence_table_cursor_changed(GtkTreeView *treeview, gpointer user_data) {
  /* delay the action */
  g_idle_add_full(G_PRIORITY_HIGH_IDLE,on_sequence_table_cursor_changed_idle,user_data,NULL);
}

static gboolean change_pattern(BtMainPageSequence *self, BtPattern *new_pattern,gulong row,gulong track) {
	gboolean res=FALSE;
	BtMachine *machine;

	if((machine=bt_sequence_get_machine(self->priv->sequence,track))) {
		BtPattern *old_pattern=bt_sequence_get_pattern(self->priv->sequence,row,track);
		gchar *undo_str,*redo_str;
		gchar *mid,*old_pid=NULL,*new_pid=NULL;

		g_object_get(machine,"id",&mid,NULL);
		bt_sequence_set_pattern(self->priv->sequence,row,track,new_pattern);
		if(old_pattern) {
			g_object_get(old_pattern,"id",&old_pid,NULL);
			g_object_unref(old_pattern);
		}
		if(new_pattern) {
			g_object_get(new_pattern,"id",&new_pid,NULL);
		}
		undo_str = g_strdup_printf("set_patterns %lu,%lu,%lu,%s,%s",track,row,row,mid,(old_pid?old_pid:" "));
		redo_str = g_strdup_printf("set_patterns %lu,%lu,%lu,%s,%s",track,row,row,mid,(new_pid?new_pid:" "));
		bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);        
		g_free(mid);g_free(new_pid);g_free(old_pid);
		g_object_unref(machine);
		res=TRUE;
	}
  return(res);
}

// use key-press-event, as then we get key repeats
static gboolean on_sequence_table_key_press_event(GtkWidget *widget,GdkEventKey *event,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gboolean res=FALSE;
  gulong row,track;

  if(!gtk_widget_get_realized(GTK_WIDGET(self->priv->sequence_table))) return(FALSE);

  GST_INFO("sequence_table key key : state 0x%x, keyval 0x%x, hw-code 0x%x, name %s",
    event->state,event->keyval,event->hardware_keycode,gdk_keyval_name(event->keyval));

  // determine timeline and timelinetrack from cursor pos
  if(sequence_view_get_current_pos(self,&row,&track)) {
    BtSong *song;
    gulong length,tracks;
    gchar *str=NULL;
    gboolean free_str=FALSE;
    gboolean change=FALSE;
    gulong modifier=(gulong)event->state&gtk_accelerator_get_default_mod_mask();

    g_object_get(self->priv->sequence,"length",&length,"tracks",&tracks,NULL);

    GST_DEBUG("cursor pos : %lu/%lu, %lu/%lu",row,length,track,tracks);
    if(track>tracks) return(FALSE);

    g_object_get(self->priv->app,"song",&song,NULL);

    // look up pattern for key
    if(event->keyval==GDK_space || event->keyval == GDK_period) {
      // first column is label
      if((track>0) && (row<length)) {
      	if((res=change_pattern(self,NULL,row,track-1))) {
					str=" ";
					change=TRUE;
					res=TRUE;
				}
      }
    }
    else if(event->keyval==GDK_Return) {  /* GDK_KP_Enter */
      // first column is label
      if(track>0) {
        BtMainPagePatterns *patterns_page;
        BtPattern *pattern;

        bt_child_proxy_get(self->priv->main_window,"pages::patterns-page",&patterns_page,NULL);
        bt_child_proxy_set(self->priv->main_window,"pages::page",BT_MAIN_PAGES_PATTERNS_PAGE,NULL);
        if((row<length) && (pattern=bt_sequence_get_pattern(self->priv->sequence,row,track-1))) {
          GST_INFO("show pattern");
          bt_main_page_patterns_show_pattern(patterns_page,pattern);
          g_object_unref(pattern);
        }
        else {
          GST_INFO("show machine");
          bt_main_page_patterns_show_machine(patterns_page,self->priv->machine);
        }
        g_object_unref(patterns_page);

        res=TRUE;
      }
    }
    else if(event->keyval==GDK_Menu) {
      gtk_menu_popup(self->priv->context_menu,NULL,NULL,NULL,NULL,3,gtk_get_current_event_time());
    }
    else if(event->keyval==GDK_Up || event->keyval==GDK_Down || event->keyval==GDK_Left || event->keyval==GDK_Right) {
      if(modifier==GDK_SHIFT_MASK) {
        gboolean select=FALSE;

        GST_INFO("handling selection");

        // handle selection
        switch(event->keyval) {
          case GDK_Up:
            if((self->priv->cursor_row>=0)) {
              self->priv->cursor_row-=self->priv->bars;
              sequence_view_set_cursor_pos(self);
              GST_INFO("up   : %3ld,%3ld -> %3ld,%3ld @ %3ld,%3ld",self->priv->selection_start_column,self->priv->selection_start_row,self->priv->selection_end_column,self->priv->selection_end_row,self->priv->cursor_column,self->priv->cursor_row);
              if(self->priv->selection_start_row==-1) {
                GST_INFO("up   : new selection");
                self->priv->selection_start_column=self->priv->cursor_column;
                self->priv->selection_end_column=self->priv->cursor_column;
                self->priv->selection_start_row=self->priv->cursor_row;
                self->priv->selection_end_row=self->priv->cursor_row+self->priv->bars;
              }
              else {
                if(self->priv->selection_start_row==(self->priv->cursor_row+self->priv->bars)) {
                  GST_INFO("up   : expand selection");
                  self->priv->selection_start_row-=self->priv->bars;
                }
                else {
                  GST_INFO("up   : shrink selection");
                  self->priv->selection_end_row-=self->priv->bars;
                }
              }
              GST_INFO("up   : %3ld,%3ld -> %3ld,%3ld",self->priv->selection_start_column,self->priv->selection_start_row,self->priv->selection_end_column,self->priv->selection_end_row);
              select=TRUE;
            }
            break;
          case GDK_Down:
            /* we expand length */
            self->priv->cursor_row+=self->priv->bars;
            sequence_view_set_cursor_pos(self);
            GST_INFO("down : %3ld,%3ld -> %3ld,%3ld @ %3ld,%3ld",self->priv->selection_start_column,self->priv->selection_start_row,self->priv->selection_end_column,self->priv->selection_end_row,self->priv->cursor_column,self->priv->cursor_row);
            if(self->priv->selection_end_row==-1) {
              GST_INFO("down : new selection");
              self->priv->selection_start_column=self->priv->cursor_column;
              self->priv->selection_end_column=self->priv->cursor_column;
              self->priv->selection_start_row=self->priv->cursor_row-self->priv->bars;
              self->priv->selection_end_row=self->priv->cursor_row;
            }
            else {
              if(self->priv->selection_end_row==(self->priv->cursor_row-self->priv->bars)) {
                GST_INFO("down : expand selection");
                self->priv->selection_end_row+=self->priv->bars;
              }
              else {
                GST_INFO("down : shrink selection");
                self->priv->selection_start_row+=self->priv->bars;
              }
            }
            GST_INFO("down : %3ld,%3ld -> %3ld,%3ld",self->priv->selection_start_column,self->priv->selection_start_row,self->priv->selection_end_column,self->priv->selection_end_row);
            select=TRUE;
            break;
          case GDK_Left:
            if(self->priv->cursor_column>=0) {
              // move cursor
              self->priv->cursor_column--;
              sequence_view_set_cursor_pos(self);
              GST_INFO("left : %3ld,%3ld -> %3ld,%3ld @ %3ld,%3ld",self->priv->selection_start_column,self->priv->selection_start_row,self->priv->selection_end_column,self->priv->selection_end_row,self->priv->cursor_column,self->priv->cursor_row);
              if(self->priv->selection_start_column==-1) {
                GST_INFO("left : new selection");
                self->priv->selection_start_column=self->priv->cursor_column;
                self->priv->selection_end_column=self->priv->cursor_column+1;
                self->priv->selection_start_row=self->priv->cursor_row;
                self->priv->selection_end_row=self->priv->cursor_row;
              }
              else {
                if(self->priv->selection_start_column==(self->priv->cursor_column+1)) {
                  GST_INFO("left : expand selection");
                  self->priv->selection_start_column--;
                }
                else {
                  GST_INFO("left : shrink selection");
                  self->priv->selection_end_column--;
                }
              }
              GST_INFO("left : %3ld,%3ld -> %3ld,%3ld",self->priv->selection_start_column,self->priv->selection_start_row,self->priv->selection_end_column,self->priv->selection_end_row);
              select=TRUE;
            }
            break;
          case GDK_Right:
            if(self->priv->cursor_column<tracks) {
              // move cursor
              self->priv->cursor_column++;
              sequence_view_set_cursor_pos(self);
              GST_INFO("right: %3ld,%3ld -> %3ld,%3ld @ %3ld,%3ld",self->priv->selection_start_column,self->priv->selection_start_row,self->priv->selection_end_column,self->priv->selection_end_row,self->priv->cursor_column,self->priv->cursor_row);
              if(self->priv->selection_end_column==-1) {
                GST_INFO("right: new selection");
                self->priv->selection_start_column=self->priv->cursor_column-1;
                self->priv->selection_end_column=self->priv->cursor_column;
                self->priv->selection_start_row=self->priv->cursor_row;
                self->priv->selection_end_row=self->priv->cursor_row;
              }
              else {
                if(self->priv->selection_end_column==(self->priv->cursor_column-1)) {
                  GST_INFO("right: expand selection");
                  self->priv->selection_end_column++;
                }
                else {
                  GST_INFO("right: shrink selection");
                  self->priv->selection_start_column++;
                }
              }
              GST_INFO("right: %3ld,%3ld -> %3ld,%3ld",self->priv->selection_start_column,self->priv->selection_start_row,self->priv->selection_end_column,self->priv->selection_end_row);
              select=TRUE;
            }
            break;
        }
        if(select) {
          gtk_widget_queue_draw(GTK_WIDGET(self->priv->sequence_table));
          res=TRUE;
        }
      }
      else {
        // remove selection
        if(self->priv->selection_start_column!=-1) {
          self->priv->selection_start_column=self->priv->selection_start_row=self->priv->selection_end_column=self->priv->selection_end_row=-1;
          gtk_widget_queue_draw(GTK_WIDGET(self->priv->sequence_table));
        }
      }
    }
    else if(event->keyval == GDK_b) {
      if(modifier==GDK_CONTROL_MASK) {
        GST_INFO("ctrl-b pressed, row %lu",row);
        sequence_view_set_pos(self,SEQUENCE_VIEW_POS_LOOP_START,(glong)row);
        res=TRUE;
      }
    }
    else if(event->keyval == GDK_e) {
      if(modifier==GDK_CONTROL_MASK) {
        GST_INFO("ctrl-e pressed, row %lu",row);
        sequence_view_set_pos(self,SEQUENCE_VIEW_POS_LOOP_END,(glong)row);
        res=TRUE;
      }
    }
    else if(event->keyval == GDK_Insert) {
      if(modifier==0) {
      	GString *old_data=g_string_new(NULL),*new_data=g_string_new(NULL);
      	glong col=(glong)track-1;
      	gulong sequence_length;

        GST_INFO("insert pressed, row %lu, track %ld",row,col);
        g_object_get(self->priv->sequence,"length",&sequence_length,NULL);
				sequence_range_copy(self,track,track,row,sequence_length-1,old_data);
        bt_sequence_insert_rows(self->priv->sequence,row,col,self->priv->bars);
        sequence_range_copy(self,track,track,row,sequence_length-1,new_data);
        sequence_range_log_undo_redo(self,track,track,row,sequence_length-1,old_data->str,new_data->str);
				g_string_free(old_data,TRUE);g_string_free(new_data,TRUE);
        // reinit the view
        sequence_table_refresh(self,song);
        //sequence_calculate_visible_lines(self);
        sequence_model_recolorize(self);
        sequence_view_set_cursor_pos(self);
        res=TRUE;
      }
      else if(modifier==GDK_SHIFT_MASK) {
      	GString *old_data=g_string_new(NULL),*new_data=g_string_new(NULL);
      	gulong sequence_length,number_of_tracks;
      	gchar *undo_str,*redo_str;

        GST_INFO("shift-insert pressed, row %lu",row);
        g_object_get(self->priv->sequence,"length",&sequence_length,"tracks",&number_of_tracks,NULL);
        sequence_length+=self->priv->bars;

				sequence_range_copy(self,0,number_of_tracks,row,sequence_length-1,old_data);
        bt_sequence_insert_full_rows(self->priv->sequence,row,self->priv->bars);
        sequence_range_copy(self,0,number_of_tracks,row,sequence_length-1,new_data);
        
        bt_change_log_start_group(self->priv->change_log);
        sequence_range_log_undo_redo(self,0,number_of_tracks,row,sequence_length-1,old_data->str,new_data->str);
				undo_str = g_strdup_printf("set_sequence_property \"length\",\"%ld\"",sequence_length-self->priv->bars);
				redo_str = g_strdup_printf("set_sequence_property \"length\",\"%ld\"",sequence_length);
				bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);        	
        bt_change_log_end_group(self->priv->change_log);
        
				g_string_free(old_data,TRUE);g_string_free(new_data,TRUE);
        self->priv->sequence_length+=self->priv->bars;
        // reinit the view
        sequence_table_refresh(self,song);
        sequence_calculate_visible_lines(self);
        sequence_model_recolorize(self);
        sequence_view_set_cursor_pos(self);
        res=TRUE;
      }
    }
    else if(event->keyval == GDK_Delete) {
      if(modifier==0) {
      	GString *old_data=g_string_new(NULL),*new_data=g_string_new(NULL);
      	glong col=(glong)track-1;
      	gulong sequence_length;

        GST_INFO("delete pressed, row %lu, track %ld",row,col);
        g_object_get(self->priv->sequence,"length",&sequence_length,NULL);
				sequence_range_copy(self,track,track,row,sequence_length-1,old_data);
        bt_sequence_delete_rows(self->priv->sequence,row,col,self->priv->bars);
        sequence_range_copy(self,track,track,row,sequence_length-1,new_data);
        sequence_range_log_undo_redo(self,track,track,row,sequence_length-1,old_data->str,new_data->str);
				g_string_free(old_data,TRUE);g_string_free(new_data,TRUE);
        // reinit the view
        sequence_table_refresh(self,song);
        //sequence_calculate_visible_lines(self);
        sequence_model_recolorize(self);
        sequence_view_set_cursor_pos(self);
        res=TRUE;
      }
      else if(modifier==GDK_SHIFT_MASK) {
      	GString *old_data=g_string_new(NULL),*new_data=g_string_new(NULL);
      	gulong sequence_length,number_of_tracks;
      	gchar *undo_str,*redo_str;

        GST_INFO("shift-delete pressed, row %lu",row);
        g_object_get(self->priv->sequence,"length",&sequence_length,"tracks",&number_of_tracks,NULL);

				sequence_range_copy(self,0,number_of_tracks,row,sequence_length-1,old_data);
        bt_sequence_delete_full_rows(self->priv->sequence,row,self->priv->bars);
        sequence_range_copy(self,0,number_of_tracks,row,sequence_length-1,new_data);
        
        bt_change_log_start_group(self->priv->change_log);
				undo_str = g_strdup_printf("set_sequence_property \"length\",\"%ld\"",sequence_length);
				redo_str = g_strdup_printf("set_sequence_property \"length\",\"%ld\"",sequence_length-self->priv->bars);
				bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);        	
        sequence_range_log_undo_redo(self,0,number_of_tracks,row,sequence_length-1,old_data->str,new_data->str);
        bt_change_log_end_group(self->priv->change_log);
        
				g_string_free(old_data,TRUE);g_string_free(new_data,TRUE);
        self->priv->sequence_length-=self->priv->bars;
        // reinit the view
        sequence_table_refresh(self,song);
        sequence_calculate_visible_lines(self);
        sequence_model_recolorize(self);
        sequence_view_set_cursor_pos(self);
        res=TRUE;
      }
    }

    if((!res) && (event->keyval<='z') && ((modifier==0) || (modifier==GDK_SHIFT_MASK))) {
      // first column is label
      if((track>0) && (row<length)) {
        gchar key=(gchar)(event->keyval&0xff);
        BtPattern *new_pattern;
        GtkTreeModel *store;

        store=gtk_tree_view_get_model(self->priv->pattern_list);
        if((new_pattern=pattern_list_model_get_pattern_by_key(store,key))) {
        	if((res=change_pattern(self,new_pattern,row,track-1))) {
						g_object_get(new_pattern,"name",&str,NULL);
						g_object_unref(new_pattern);

						free_str=TRUE;
						change=TRUE;
						res=TRUE;
					}
        }
        else {
          GST_WARNING_OBJECT(self->priv->machine,"keyval '%c' not used by machine",key);
        }
      }
    }

    // update tree-view model
    if(change) {
      GtkTreeModelFilter *filtered_store;
      GtkTreeModel *store;

      if((filtered_store=GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(self->priv->sequence_table))) &&
        (store=gtk_tree_model_filter_get_model(filtered_store))
      ) {
        GtkTreePath *path;
        GtkTreeViewColumn *column;

        gtk_tree_view_get_cursor(self->priv->sequence_table,&path,&column);
        if(path && column) {
          GtkTreeIter iter,filter_iter;

          GST_INFO("  update model");

          if(gtk_tree_model_get_iter(GTK_TREE_MODEL(filtered_store),&filter_iter,path)) {
            GList *columns=gtk_tree_view_get_columns(self->priv->sequence_table);
#ifndef USE_SEQUENCE_GRID_MODEL
            glong col=g_list_index(columns,(gpointer)column)-1;
#endif
            GtkTreePath *cpath;

            g_list_free(columns);
            gtk_tree_model_filter_convert_iter_to_child_iter(filtered_store,&iter,&filter_iter);
            //glong row;
            //gtk_tree_model_get(store,&iter,BT_SEQUENCE_GRID_MODEL_POS,&row,-1);
            //GST_INFO("  position is %d,%d -> ",row,col,__BT_SEQUENCE_GRID_MODEL_N_COLUMNS+col);

#ifndef USE_SEQUENCE_GRID_MODEL
            gtk_list_store_set(GTK_LIST_STORE(store),&iter,__BT_SEQUENCE_GRID_MODEL_N_COLUMNS+col,str,-1);
#endif

            // move cursor down & set cell focus
            self->priv->cursor_row+=self->priv->bars;
            if((cpath=gtk_tree_path_new_from_indices((self->priv->cursor_row/self->priv->bars),-1))) {
              gtk_tree_view_set_cursor(self->priv->sequence_table,cpath,column,FALSE);
              gtk_tree_path_free(cpath);
            }
          }
          else {
            GST_WARNING("  can't get tree-iter");
          }
        }
        else {
          GST_WARNING("  can't evaluate cursor pos");
        }

        if(path) gtk_tree_path_free(path);
      }
      else {
        GST_WARNING("  can't get tree-model");
      }
    }
    //else if(!select) GST_INFO("  nothing assgned to this key");

    // release the references
    g_object_unref(song);
    if(free_str) g_free(str);
  }
  return(res);
}

static gboolean on_sequence_header_button_press_event(GtkWidget *widget,GdkEventButton *event,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gboolean res=FALSE;

  GST_INFO("sequence_header button_press : button 0x%x, type 0x%d",event->button,event->type);
  if(event->button==3) {
    gtk_menu_popup(self->priv->context_menu,NULL,NULL,NULL,NULL,3,gtk_get_current_event_time());
    res=TRUE;
  }
  return(res);
}

static gboolean on_sequence_table_button_press_event(GtkWidget *widget,GdkEventButton *event,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gboolean res=FALSE;

  GST_INFO("sequence_table button_press : button 0x%x, type 0x%d",event->button,event->type);
  if(event->button==1) {
    if(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(widget))==(event->window)) {
      GtkTreePath *path;
      GtkTreeViewColumn *column;
      gulong modifier=(gulong)event->state&(GDK_CONTROL_MASK|GDK_MOD4_MASK);
      // determine sequence position from mouse coordinates
      if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),event->x,event->y,&path,&column,NULL,NULL)) {
        gulong track,row;

        if(sequence_view_get_cursor_pos(GTK_TREE_VIEW(widget),path,column,&track,&row)) {
          GST_INFO("  left click to column %lu, row %lu",track,row);
          if(widget==GTK_WIDGET(self->priv->sequence_pos_table)) {
            switch(modifier) {
              case 0:
                sequence_view_set_pos(self,SEQUENCE_VIEW_POS_PLAY,(glong)row);
                break;
              case GDK_CONTROL_MASK:
                sequence_view_set_pos(self,SEQUENCE_VIEW_POS_LOOP_START,(glong)row);
                break;
              case GDK_MOD4_MASK:
                sequence_view_set_pos(self,SEQUENCE_VIEW_POS_LOOP_END,(glong)row);
                break;
            }
          }
          else {
            // set cell focus
            gtk_tree_view_set_cursor(self->priv->sequence_table,path,column,FALSE);
            gtk_widget_grab_focus_savely(GTK_WIDGET(self->priv->sequence_table));
            // reset selection
            self->priv->selection_start_column=self->priv->selection_start_row=self->priv->selection_end_column=self->priv->selection_end_row=-1;
          }
          res=TRUE;
        }
      }
      else {
        GST_INFO("clicked outside data area - #1");
        switch(modifier) {
          case 0:
            sequence_view_set_pos(self,SEQUENCE_VIEW_POS_PLAY,-1);
            break;
          case GDK_CONTROL_MASK:
            sequence_view_set_pos(self,SEQUENCE_VIEW_POS_LOOP_START,-1);
            break;
          case GDK_MOD4_MASK:
            sequence_view_set_pos(self,SEQUENCE_VIEW_POS_LOOP_END,-1);
            break;
        }
        res=TRUE;
      }
      if(path) gtk_tree_path_free(path);
    }
  }
  else if(event->button==3) {
    gtk_menu_popup(self->priv->context_menu,NULL,NULL,NULL,NULL,3,gtk_get_current_event_time());
    res=TRUE;
  }
  return(res);
}

static gboolean on_sequence_table_motion_notify_event(GtkWidget *widget,GdkEventMotion *event,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gboolean res=FALSE;

  // only activate in button_press ?
  if(event->state&GDK_BUTTON1_MASK) {
    if(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(widget))==(event->window)) {
      GtkTreePath *path;
      GtkTreeViewColumn *column;
      // determine sequence position from mouse coordinates
      if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),event->x,event->y,&path,&column,NULL,NULL)) {
        if(widget==GTK_WIDGET(self->priv->sequence_pos_table)) {
          gulong track,row;
          if(sequence_view_get_cursor_pos(GTK_TREE_VIEW(widget),path,column,&track,&row)) {
            sequence_view_set_pos(self,SEQUENCE_VIEW_POS_PLAY,(glong)row);
          }
        }
        else {
          // handle selection
          glong cursor_column=self->priv->cursor_column;
          glong cursor_row=self->priv->cursor_row;

          if(self->priv->selection_start_column==-1) {
            self->priv->selection_column=self->priv->cursor_column;
            self->priv->selection_row=self->priv->cursor_row;
          }
          gtk_tree_view_set_cursor(self->priv->sequence_table,path,column,FALSE);
          gtk_widget_grab_focus_savely(GTK_WIDGET(self->priv->sequence_table));
          // cursor updates are not yet processed
          on_sequence_table_cursor_changed_idle(self);
          GST_DEBUG("cursor new/old: %3ld,%3ld -> %3ld,%3ld",cursor_column,cursor_row,self->priv->cursor_column,self->priv->cursor_row);
          if((cursor_column!=self->priv->cursor_column) || (cursor_row!=self->priv->cursor_row)) {
            if(self->priv->selection_start_column==-1) {
              self->priv->selection_start_column=MIN(cursor_column,self->priv->selection_column);
              self->priv->selection_start_row=MIN(cursor_row,self->priv->selection_row);
              self->priv->selection_end_column=MAX(cursor_column,self->priv->selection_column);
              self->priv->selection_end_row=MAX(cursor_row,self->priv->selection_row);
            }
            else {
              if(self->priv->cursor_column<self->priv->selection_column) {
                self->priv->selection_start_column=self->priv->cursor_column;
                self->priv->selection_end_column=self->priv->selection_column;
              }
              else {
                self->priv->selection_start_column=self->priv->selection_column;
                self->priv->selection_end_column=self->priv->cursor_column;
              }
              if(self->priv->cursor_row<self->priv->selection_row) {
                self->priv->selection_start_row=self->priv->cursor_row;
                self->priv->selection_end_row=self->priv->selection_row;
              }
              else {
                self->priv->selection_start_row=self->priv->selection_row;
                self->priv->selection_end_row=self->priv->cursor_row;
              }
            }
            gtk_widget_queue_draw(GTK_WIDGET(self->priv->sequence_table));
          }
        }
        res=TRUE;
      }
      if(path) gtk_tree_path_free(path);
    }
  }
  return(res);
}

static gboolean on_sequence_table_scroll_event( GtkWidget *widget, GdkEventScroll *event, gpointer user_data ) {
  //BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);

  if(event) {
    static GdkEventKey keyevent={0,};

    if( event->direction == GDK_SCROLL_UP ) {
      keyevent.keyval = GDK_Up;
      keyevent.hardware_keycode = 98;
    }
    else if( event->direction == GDK_SCROLL_DOWN ) {
      keyevent.keyval = GDK_Down;
      keyevent.hardware_keycode = 104;
    }
    else
      return FALSE;

    keyevent.window = event->window;
    keyevent.state = event->state;
    keyevent.time = GDK_CURRENT_TIME;

    keyevent.type = GDK_KEY_PRESS;
    gtk_main_do_event((GdkEvent *)&keyevent);

    keyevent.type = GDK_KEY_RELEASE;
    gtk_main_do_event((GdkEvent *)&keyevent);

    return TRUE;
  }

  return FALSE;
}

// setup colors for sequence view
static void bt_sequence_table_update_colors(const BtMainPageSequence *self) {
  GtkStyle *s=GTK_WIDGET(self->priv->sequence_table)->style;
  guint fg,bg;

  fg=(s->text->red>>8) + (s->text->green>>8) + (s->text->blue>>8);
  bg=(s->base->red>>8) + (s->base->green>>8) + (s->base->blue>>8);
/*
  GST_DEBUG("text %u : base %u",fg,bg);
#define PRINT_COLOR(c) (c->red)>>8,(c->green)>>8,(c->blue)>>8
  GST_DEBUG("sequence view colors: fg    : #%02x%02x%02x",PRINT_COLOR(s->fg));
  GST_DEBUG("sequence view colors: bg    : #%02x%02x%02x",PRINT_COLOR(s->bg));
  GST_DEBUG("sequence view colors: light : #%02x%02x%02x",PRINT_COLOR(s->light));
  GST_DEBUG("sequence view colors: mid   : #%02x%02x%02x",PRINT_COLOR(s->mid));
  GST_DEBUG("sequence view colors: dark  : #%02x%02x%02x",PRINT_COLOR(s->dark));
  GST_DEBUG("sequence view colors: base  : #%02x%02x%02x",PRINT_COLOR(s->base));
  GST_DEBUG("sequence view colors: text  : #%02x%02x%02x",PRINT_COLOR(s->text));
  GST_DEBUG("sequence view colors: texta : #%02x%02x%02x",PRINT_COLOR(s->text_aa));
*/

  // get colors
  self->priv->cursor_bg=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_CURSOR);
  self->priv->selection_bg1=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_SELECTION1);
  self->priv->selection_bg2=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_SELECTION2);
  if (bg>fg) {
    self->priv->source_bg1=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_SOURCE_MACHINE_BRIGHT1);
    self->priv->source_bg2=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_SOURCE_MACHINE_BRIGHT2);
    self->priv->processor_bg1=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_PROCESSOR_MACHINE_BRIGHT1);
    self->priv->processor_bg2=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_PROCESSOR_MACHINE_BRIGHT2);
    self->priv->sink_bg1=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_SINK_MACHINE_BRIGHT1);
    self->priv->sink_bg2=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_SINK_MACHINE_BRIGHT2);
  } else {
    self->priv->source_bg1=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_SOURCE_MACHINE_DARK1);
    self->priv->source_bg2=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_SOURCE_MACHINE_DARK2);
    self->priv->processor_bg1=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_PROCESSOR_MACHINE_DARK1);
    self->priv->processor_bg2=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_PROCESSOR_MACHINE_DARK2);
    self->priv->sink_bg1=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_SINK_MACHINE_DARK1);
    self->priv->sink_bg2=bt_ui_resources_get_gdk_color(BT_UI_RES_COLOR_SINK_MACHINE_DARK2);
  }
  sequence_model_recolorize(self);
}

static void on_sequence_table_realize(GtkWidget *widget,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);

  bt_sequence_table_update_colors(self);
}

static void on_sequence_table_style_set(GtkWidget *widget,GtkStyle *old_style,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);

  bt_sequence_table_update_colors(self);
}

static void on_machine_added(BtSetup *setup,BtMachine *machine,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);

  GST_INFO("machine %p,ref_count=%d has been added",machine,G_OBJECT_REF_COUNT(machine));
  machine_menu_refresh(self,setup);
  GST_INFO("machine %p,ref_count=%d chk1",machine,G_OBJECT_REF_COUNT(machine));
  // don't create the track, if we already do so from an undo
  if(bt_change_log_is_active(self->priv->change_log)) {
		if(BT_IS_SOURCE_MACHINE(machine)) {
			sequence_add_track(self,machine,-1);
		}
	}
  GST_INFO("... machine %p,ref_count=%d has been added",machine,G_OBJECT_REF_COUNT(machine));
}

static void on_machine_removed(BtSetup *setup,BtMachine *machine,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  BtSong *song;
  gulong number_of_tracks;
	GString *old_data;
	gchar *undo_str,*redo_str;
	gchar *mid;
	glong ix=0;
	gulong sequence_length;

  g_return_if_fail(BT_IS_MACHINE(machine));

  GST_INFO("machine %p,ref_count=%d has been removed",machine,G_OBJECT_REF_COUNT(machine));

  // reinit the menu
  GST_DEBUG("menu item for machine %p,ref_count=%d",machine,G_OBJECT_REF_COUNT(machine));
  //g_signal_handlers_disconnect_matched(machine,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_machine_id_changed_menu,NULL);
  machine_menu_refresh(self,setup);

  // get song from app and then setup from song
  g_object_get(self->priv->app,"song",&song,NULL);

	/* handle undo/redo */
	g_object_get(machine,"id",&mid,NULL);
	g_object_get(self->priv->sequence,"length",&sequence_length,NULL);
	bt_change_log_start_group(self->priv->change_log);
	/* which order ? */
  while(((ix=bt_sequence_get_track_by_machine(self->priv->sequence,machine,ix))>-1)) {
  	old_data=g_string_new(NULL);
  	undo_str = g_strdup_printf("add_track \"%s\",%lu",mid,(gulong)ix);
	  redo_str = g_strdup_printf("rem_track %lu",(gulong)ix);
	  bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);
	
	  sequence_range_copy(self,ix+1,ix+1,0,sequence_length-1,old_data);
	  sequence_range_log_undo_redo(self,ix+1,ix+1,0,sequence_length-1,old_data->str,g_strdup(old_data->str));
	  g_string_free(old_data,TRUE);

    bt_sequence_remove_track_by_ix(self->priv->sequence,ix);
  }
	bt_change_log_end_group(self->priv->change_log);
	g_free(mid);

  // reset selection
  self->priv->selection_start_column=self->priv->selection_start_row=self->priv->selection_end_column=self->priv->selection_end_row=-1;

  // reinit the view
  sequence_table_refresh(self,song);
  sequence_model_recolorize(self);

  g_object_get(self->priv->sequence,"tracks",&number_of_tracks,NULL);
  if(self->priv->cursor_column>=number_of_tracks) {
    // update cursor_column and focus cell
    self->priv->cursor_column=number_of_tracks-1;
    sequence_view_set_cursor_pos(self);
    GST_DEBUG("new cursor column: %ld",self->priv->cursor_column);
  }
  update_after_track_changed(self);

  g_object_unref(song);
  GST_INFO("... machine %p,ref_count=%d has been removed",machine,G_OBJECT_REF_COUNT(machine));
}

static void on_pattern_removed(BtMachine *machine,BtPattern *pattern,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  BtSequence *sequence=self->priv->sequence;
  BtSong *song;

  GST_INFO("pattern has been removed: %p,ref_count=%d",pattern,G_OBJECT_REF_COUNT(pattern));

  /* this is racy if the sequence also listens for pattern_removed
   * and clears the tracks - right now we don't do this automatic updates in
   * the song anymore
   *
   * we also want to ensure, that we update the sequence_view *after* the changes
   * which is hard to ensure if the song changes itself
   */
  /* FIXME: we don't want to do this when the machine gets removed, as then we
   * save the content already when handling the track removal, in the current
   * situation the track would then be saved empty
   *
   * solutions:
   * - don't do main-page-patterns::on_machine_removed(), but emit signal in machine::dispose()
   *   - we get bad undo/redo serialization (due to dispose_has_run)
   * - use connect_after() for main-page-patterns::on_machine_removed()
   *   - this isn't better, we want to completely avoid it, or serialize it
   * - how could we handle the on_machine_removed first, handle undo/redo remove
   *   the track(s), disconnect the on_pattern_removed handler and don't worry
   *   - right now main-page-patterns.c::on_machine_removed() removes the patterns
   *
   * - the add/remove signal handlers could have an additional argument (explicit=TRUE/FALSE)
   *   - when we dispose the song, all removed-handlers with have explicit=FALSE
   *   - when e.g. removing a machine, machine-removed has explicit=TRUE and
   *     pattern-removed has explicit=FALSE
   *   - same could be done for adding - when adding a source machine
   *     machine-added has explicit=TRUE, pattern-added will have explicit=FALSE
   *   - we only do undo/redo for explicit actions 
   */
  if(bt_sequence_is_pattern_used(sequence,pattern)) {
  	glong tick;
  	glong track=0;
		gchar *undo_str,*redo_str;
		gchar *mid,*pid;
  	
    g_object_get(machine,"id",&mid,NULL);
    g_object_get(pattern,"id",&pid,NULL);
    
    GST_WARNING("pattern %s is used in sequence, doing undo/redo",pid);
		/* save the cells that use the pattern */
		bt_change_log_start_group(self->priv->change_log);
  	while((track=bt_sequence_get_track_by_machine(sequence,machine,track))>-1) {
      tick=0;
      while((tick=bt_sequence_get_tick_by_pattern(sequence,track,pattern,tick))>-1) {
        undo_str = g_strdup_printf("set_patterns %lu,%lu,%lu,%s,%s",track,tick,tick,mid,pid);
        redo_str = g_strdup_printf("set_patterns %lu,%lu,%lu,%s,%s",track,tick,tick,mid," ");
        bt_change_log_add(self->priv->change_log,BT_CHANGE_LOGGER(self),undo_str,redo_str);
        bt_sequence_set_pattern_quick(sequence,tick,track,NULL);        
        tick++;
      }
      track++;
		}
		bt_change_log_end_group(self->priv->change_log);
		bt_sequence_repair_damage(sequence);

		g_free(mid);g_free(pid);
	}

  // get song from app
  g_object_get(self->priv->app,"song",&song,NULL);
  // reinit the sequence view, FIXME: only when pattern was used
  sequence_table_refresh(self,song);
  sequence_model_recolorize(self);
  g_object_unref(song);
}

static void on_song_info_bars_changed(const BtSongInfo *song_info,GParamSpec *arg,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  gulong bars;

  g_object_get((gpointer)song_info,"bars",&bars,NULL);
  // this also recolors the sequence
  self->priv->bars=0;
  update_bars_menu(self,bars);
}

static void on_song_changed(const BtEditApplication *app,GParamSpec *arg,gpointer user_data) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(user_data);
  BtSong *song;
  BtSongInfo *song_info;
  BtSetup *setup;
  GstBin *bin;
  GstBus *bus;
  glong bars;
  gulong sequence_length;
  gchar *prop;

  GST_INFO("song has changed : app=%p, self=%p",app,self);
  // get song from app and then setup from song
  g_object_get(self->priv->app,"song",&song,NULL);
  if(!song) {
    self->priv->properties=NULL;
    return;
  }
  GST_INFO("song->ref_ct=%d",G_OBJECT_REF_COUNT(song));

  g_object_try_unref(self->priv->sequence);
  g_object_get(song,"song-info",&song_info,"setup",&setup,"sequence",&self->priv->sequence,"bin", &bin,NULL);
  g_object_get(self->priv->sequence,"length",&sequence_length,"properties",&self->priv->properties,NULL);
  // make sequence_length and step_filter_pos accord to song length
  self->priv->sequence_length=sequence_length;

  // reset vu-meter hash (rebuilt below)
  if(self->priv->level_to_vumeter) g_hash_table_destroy(self->priv->level_to_vumeter);
  self->priv->level_to_vumeter=g_hash_table_new_full(NULL,NULL,(GDestroyNotify)gst_object_unref,NULL);

  // reset cursor pos                                                               
  self->priv->cursor_column=1;
  self->priv->cursor_row=0;
  
  // get stored settings
  if((prop=(gchar *)g_hash_table_lookup(self->priv->properties,"bars"))) {
    self->priv->bars=atol(prop);
  }
  if((prop=(gchar *)g_hash_table_lookup(self->priv->properties,"pos-format"))) {
    self->priv->pos_format=atol(prop);
  }

  // update page
  // update sequence and pattern list
  sequence_table_refresh(self,song);
  update_after_track_changed(self);
  machine_menu_refresh(self,setup);
  g_signal_connect(setup,"machine-added",G_CALLBACK(on_machine_added),(gpointer)self);
  g_signal_connect(setup,"machine-removed",G_CALLBACK(on_machine_removed),(gpointer)self);
  gtk_combo_box_set_active(self->priv->pos_menu,self->priv->pos_format);
  // update toolbar          
  g_object_get(song_info,"bars",&bars,NULL);
  update_bars_menu(self,bars);

  // update sequence view
  sequence_calculate_visible_lines(self);
  sequence_model_recolorize(self);
  g_object_set(self->priv->sequence_table,"play-position",0.0,NULL);
  g_object_set(self->priv->sequence_pos_table,"play-position",0.0,NULL);
  // vumeters
  bus=gst_element_get_bus(GST_ELEMENT(bin));
  g_signal_connect(bus, "message::element", G_CALLBACK(on_track_level_change), (gpointer)self);
  gst_object_unref(bus);
  if(self->priv->clock) gst_object_unref(self->priv->clock);
  self->priv->clock=gst_pipeline_get_clock (GST_PIPELINE(bin));

  // subscribe to play-pos changes of song->sequence
  g_signal_connect(song, "notify::play-pos", G_CALLBACK(on_song_play_pos_notify), (gpointer)self);
  g_signal_connect(song,"notify::is-playing",G_CALLBACK(on_song_is_playing_notify),(gpointer)self);
  // subscribe to changes in the rythm
  g_signal_connect(song_info, "notify::bars", G_CALLBACK(on_song_info_bars_changed), (gpointer)self);
  //-- release the references
  gst_object_unref(bin);
  g_object_unref(song_info);
  g_object_unref(setup);
  g_object_unref(song);
  GST_INFO("song has changed done");
}

static void on_toolbar_style_changed(const BtSettings *settings,GParamSpec *arg,gpointer user_data) {
  GtkToolbar *toolbar=GTK_TOOLBAR(user_data);
  gchar *toolbar_style;

  g_object_get((gpointer)settings,"toolbar-style",&toolbar_style,NULL);
  if(!BT_IS_STRING(toolbar_style)) return;

  GST_INFO("!!!  toolbar style has changed '%s'", toolbar_style);
  gtk_toolbar_set_style(toolbar,gtk_toolbar_get_style_from_string(toolbar_style));
  g_free(toolbar_style);
}

//-- helper methods

static void bt_main_page_sequence_init_ui(const BtMainPageSequence *self,const BtMainPages *pages) {
  GtkWidget *toolbar;
  GtkWidget *split_box,*box,*vbox,*tool_item;
  GtkWidget *scrolled_window,*scrolled_vsync_window,*scrolled_hsync_window;
  GtkWidget *hsync_viewport;
  GtkWidget *menu_item,*image;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *tree_col;
  GtkTreeSelection *tree_sel;
  GtkAdjustment *vadjust, *hadjust;
  BtSettings *settings;

  GST_DEBUG("!!!! self=%p",self);

  gtk_widget_set_name(GTK_WIDGET(self),"sequence view");

  // add toolbar
  toolbar=gtk_toolbar_new();
  gtk_widget_set_name(toolbar,"sequence view toolbar");
  gtk_box_pack_start(GTK_BOX(self),toolbar,FALSE,FALSE,0);
  gtk_toolbar_set_style(GTK_TOOLBAR(toolbar),GTK_TOOLBAR_BOTH);
  // add toolbar widgets
  // steps
  box=gtk_hbox_new(FALSE,2);
  gtk_container_set_border_width(GTK_CONTAINER(box),4);
  // build the menu
  self->priv->bars_menu=GTK_COMBO_BOX(gtk_combo_box_new());
  gtk_widget_set_tooltip_text(GTK_WIDGET(self->priv->bars_menu),_("Show every n-th line"));
  renderer=gtk_cell_renderer_text_new();
  //gtk_cell_renderer_set_fixed_size(renderer, 1, -1);
  gtk_cell_renderer_text_set_fixed_height_from_font(GTK_CELL_RENDERER_TEXT(renderer), 1);
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(self->priv->bars_menu),renderer,TRUE);
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(self->priv->bars_menu),renderer,"text", 0,NULL);
  g_signal_connect(self->priv->bars_menu,"changed",G_CALLBACK(on_bars_menu_changed), (gpointer)self);
  gtk_box_pack_start(GTK_BOX(box),gtk_label_new(_("Steps")),FALSE,FALSE,2);
  gtk_box_pack_start(GTK_BOX(box),GTK_WIDGET(self->priv->bars_menu),TRUE,TRUE,2);

  tool_item=GTK_WIDGET(gtk_tool_item_new());
  gtk_widget_set_name(tool_item,"Steps");
  gtk_container_add(GTK_CONTAINER(tool_item),box);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar),GTK_TOOL_ITEM(tool_item),-1);

#ifndef USE_HILDON
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar),gtk_separator_tool_item_new(),-1);
#endif

  // popup menu button
  image=gtk_image_new_from_filename("popup-menu.png");
  tool_item=GTK_WIDGET(gtk_tool_button_new(image,_("Sequence view menu")));
  gtk_tool_item_set_tooltip_text (GTK_TOOL_ITEM(tool_item),_("Menu actions for sequence view below"));
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar),GTK_TOOL_ITEM(tool_item),-1);
  g_signal_connect(tool_item,"clicked",G_CALLBACK(on_toolbar_menu_clicked),(gpointer)self);

  // generate the context menu
  self->priv->accel_group=gtk_accel_group_new();
  self->priv->context_menu=GTK_MENU(g_object_ref_sink(gtk_menu_new()));
  gtk_menu_set_accel_group(GTK_MENU(self->priv->context_menu), self->priv->accel_group);
  gtk_menu_set_accel_path(GTK_MENU(self->priv->context_menu),"<Buzztard-Main>/SequenceView/SequenceContext");

  self->priv->context_menu_add=GTK_MENU_ITEM(gtk_image_menu_item_new_with_label(_("Add track")));
  image=gtk_image_new_from_stock(GTK_STOCK_ADD,GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(self->priv->context_menu_add),image);
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),GTK_WIDGET(self->priv->context_menu_add));
  gtk_widget_show(GTK_WIDGET(self->priv->context_menu_add));

  menu_item=gtk_image_menu_item_new_with_label(_("Remove track"));
  image=gtk_image_new_from_stock(GTK_STOCK_REMOVE,GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menu_item),image);
  gtk_menu_item_set_accel_path (GTK_MENU_ITEM (menu_item), "<Buzztard-Main>/SequenceView/SequenceContext/RemoveTrack");
  gtk_accel_map_add_entry ("<Buzztard-Main>/SequenceView/SequenceContext/RemoveTrack", GDK_Delete, GDK_CONTROL_MASK);
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);
  g_signal_connect(menu_item,"activate",G_CALLBACK(on_track_remove_activated),(gpointer)self);

  menu_item=gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);

  menu_item=gtk_image_menu_item_new_with_label(_("Move track left"));
  image=gtk_image_new_from_stock(GTK_STOCK_GO_BACK,GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menu_item),image);
  gtk_menu_item_set_accel_path (GTK_MENU_ITEM (menu_item), "<Buzztard-Main>/SequenceView/SequenceContext/MoveTrackLeft");
  gtk_accel_map_add_entry ("<Buzztard-Main>/SequenceView/SequenceContext/MoveTrackLeft", GDK_Left, GDK_CONTROL_MASK);
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);
  g_signal_connect(menu_item,"activate",G_CALLBACK(on_track_move_left_activated),(gpointer)self);

  menu_item=gtk_image_menu_item_new_with_label(_("Move track right"));
  image=gtk_image_new_from_stock(GTK_STOCK_GO_FORWARD,GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menu_item),image);
  gtk_menu_item_set_accel_path (GTK_MENU_ITEM (menu_item), "<Buzztard-Main>/SequenceView/SequenceContext/MoveTrackRight");
  gtk_accel_map_add_entry ("<Buzztard-Main>/SequenceView/SequenceContext/MoveTrackRight", GDK_Right, GDK_CONTROL_MASK);
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);
  g_signal_connect(menu_item,"activate",G_CALLBACK(on_track_move_right_activated),(gpointer)self);

  menu_item=gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);

  menu_item=gtk_image_menu_item_new_with_label(_("Machine properties"));  // dynamic part
  image=gtk_image_new_from_stock(GTK_STOCK_PROPERTIES,GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menu_item),image);
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);
  g_signal_connect(menu_item,"activate",G_CALLBACK(on_context_menu_machine_properties_activate),(gpointer)self);

  menu_item=gtk_image_menu_item_new_with_label(_("Machine preferences"));  // static part
  image=gtk_image_new_from_stock(GTK_STOCK_PREFERENCES,GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menu_item),image);
  gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->context_menu),menu_item);
  gtk_widget_show(menu_item);
  g_signal_connect(menu_item,"activate",G_CALLBACK(on_context_menu_machine_preferences_activate),(gpointer)self);

  // --
  // @todo cut, copy, paste


  // add a hpaned
  split_box=gtk_hpaned_new();
  gtk_container_add(GTK_CONTAINER(self),split_box);

  // add hbox for sequence view
  box=gtk_hbox_new(FALSE,0);
  gtk_paned_pack1(GTK_PANED(split_box),box,TRUE,TRUE);

  // add sequence-pos list-view
  vbox=gtk_vbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(box), vbox, FALSE, FALSE, 0);
  self->priv->sequence_pos_table_header=GTK_HBOX(gtk_hbox_new(FALSE,0));
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(self->priv->sequence_pos_table_header), FALSE, FALSE, 0);

  scrolled_vsync_window=gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_vsync_window),GTK_POLICY_NEVER,GTK_POLICY_NEVER);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_vsync_window),GTK_SHADOW_NONE);
  self->priv->sequence_pos_table=GTK_TREE_VIEW(bt_sequence_view_new());
  g_object_set(self->priv->sequence_pos_table,
    "enable-search",FALSE,
    "rules-hint",TRUE,
    "fixed-height-mode",TRUE,
    "headers-visible", FALSE,
    NULL);
  gtk_widget_set_events(GTK_WIDGET(self->priv->sequence_pos_table),gtk_widget_get_events(GTK_WIDGET(self->priv->sequence_pos_table))|GDK_POINTER_MOTION_HINT_MASK);
  // set a minimum size, otherwise the window can't be shrinked (we need this because of GTK_POLICY_NEVER)
  gtk_widget_set_size_request(GTK_WIDGET(self->priv->sequence_pos_table),POSITION_CELL_WIDTH,40);
  tree_sel=gtk_tree_view_get_selection(self->priv->sequence_pos_table);
  gtk_tree_selection_set_mode(tree_sel,GTK_SELECTION_NONE);
  sequence_pos_table_init(self);
  gtk_container_add(GTK_CONTAINER(scrolled_vsync_window),GTK_WIDGET(self->priv->sequence_pos_table));
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(scrolled_vsync_window), TRUE, TRUE, 0);
  g_signal_connect(self->priv->sequence_pos_table, "button-press-event", G_CALLBACK(on_sequence_table_button_press_event), (gpointer)self);
  g_signal_connect(self->priv->sequence_pos_table, "motion-notify-event", G_CALLBACK(on_sequence_table_motion_notify_event), (gpointer)self);

  // add vertical separator
  gtk_box_pack_start(GTK_BOX(box), gtk_vseparator_new(), FALSE, FALSE, 0);

  // build label menu
  self->priv->label_menu=GTK_COMBO_BOX(gtk_combo_box_new());
  gtk_widget_set_tooltip_text(GTK_WIDGET(self->priv->label_menu),_("Browse to labels in the sequence"));
  renderer=gtk_cell_renderer_text_new();
  gtk_cell_renderer_set_fixed_size(renderer, -1, -1);
  gtk_cell_renderer_text_set_fixed_height_from_font(GTK_CELL_RENDERER_TEXT(renderer), 1);
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(self->priv->label_menu),renderer,FALSE);
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(self->priv->label_menu),renderer,"text",BT_SEQUENCE_GRID_MODEL_POSSTR,NULL);
  renderer=gtk_cell_renderer_text_new();
  gtk_cell_renderer_set_fixed_size(renderer, -1, -1);
  gtk_cell_renderer_text_set_fixed_height_from_font(GTK_CELL_RENDERER_TEXT(renderer), 1);
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(self->priv->label_menu),renderer,TRUE);
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(self->priv->label_menu),renderer,"text",BT_SEQUENCE_GRID_MODEL_LABEL,NULL);
  g_signal_connect(self->priv->label_menu,"changed",G_CALLBACK(on_label_menu_changed), (gpointer)self);

  vbox=gtk_vbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(box), vbox, TRUE, TRUE, 0);

  // add sequence header list-view
  scrolled_hsync_window=gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_hsync_window),GTK_POLICY_NEVER,GTK_POLICY_NEVER);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_hsync_window),GTK_SHADOW_NONE);
  self->priv->sequence_table_header=GTK_HBOX(gtk_hbox_new(FALSE,0));
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_hsync_window),GTK_WIDGET(self->priv->sequence_table_header));
  hsync_viewport=gtk_bin_get_child(GTK_BIN(scrolled_hsync_window));
  gtk_viewport_set_shadow_type(GTK_VIEWPORT(hsync_viewport),GTK_SHADOW_NONE);
  // set a minimum size, otherwise the window can't be shrinked (we need this because of GTK_POLICY_NEVER)
  gtk_widget_set_size_request(GTK_WIDGET(hsync_viewport),SEQUENCE_CELL_WIDTH,-1);
  /* DEBUG
  g_signal_connect(self->priv->sequence_table_header,"size-allocate",G_CALLBACK(on_sequence_header_size_allocate),(gpointer)"box");
  g_signal_connect(self->priv->sequence_table_header,"size-allocate",G_CALLBACK(on_sequence_header_size_allocate),(gpointer)"vport");
  // DEBUG */

  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(scrolled_hsync_window), FALSE, FALSE, 0);
  g_signal_connect(scrolled_hsync_window, "button-press-event", G_CALLBACK(on_sequence_header_button_press_event), (gpointer)self);

  // add sequence list-view
  scrolled_window=gtk_scrolled_window_new(NULL,NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window),GTK_SHADOW_NONE);
  self->priv->sequence_table=GTK_TREE_VIEW(bt_sequence_view_new());
  g_object_set(self->priv->sequence_table,
    "enable-search",FALSE,
    "rules-hint",TRUE,
    "fixed-height-mode",TRUE,
    "headers-visible", FALSE,
    NULL);
  //GST_DEBUG("sequence-view events = 0x%x",gtk_widget_get_events(GTK_WIDGET(self->priv->sequence_table)));
  gtk_widget_set_events(GTK_WIDGET(self->priv->sequence_table),gtk_widget_get_events(GTK_WIDGET(self->priv->sequence_table))|GDK_POINTER_MOTION_HINT_MASK);
  tree_sel=gtk_tree_view_get_selection(self->priv->sequence_table);
  gtk_tree_selection_set_mode(tree_sel,GTK_SELECTION_NONE);
  sequence_table_init(self);
  gtk_container_add(GTK_CONTAINER(scrolled_window),GTK_WIDGET(self->priv->sequence_table));
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(scrolled_window), TRUE, TRUE, 0);
  g_signal_connect_after(self->priv->sequence_table, "cursor-changed", G_CALLBACK(on_sequence_table_cursor_changed), (gpointer)self);
  g_signal_connect(self->priv->sequence_table, "key-press-event", G_CALLBACK(on_sequence_table_key_press_event), (gpointer)self);
  g_signal_connect(self->priv->sequence_table, "button-press-event", G_CALLBACK(on_sequence_table_button_press_event), (gpointer)self);
  g_signal_connect(self->priv->sequence_table, "motion-notify-event", G_CALLBACK(on_sequence_table_motion_notify_event), (gpointer)self);
  g_signal_connect(self->priv->sequence_table, "scroll-event", G_CALLBACK(on_sequence_table_scroll_event), (gpointer)self);
  g_signal_connect(self->priv->sequence_table, "realize", G_CALLBACK(on_sequence_table_realize), (gpointer)self);
  g_signal_connect(self->priv->sequence_table, "style-set", G_CALLBACK(on_sequence_table_style_set), (gpointer)self);
  gtk_widget_set_name(GTK_WIDGET(self->priv->sequence_table),"sequence editor");

  // make pos scrolled-window also use the vertical-scrollbar of the sequence scrolled-window
  vadjust=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled_window));
  gtk_scrolled_window_set_vadjustment(GTK_SCROLLED_WINDOW(scrolled_vsync_window),vadjust);
  // make header scrolled-window also use the horizontal-scrollbar of the sequence scrolled-window
  hadjust=gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolled_window));
  gtk_scrolled_window_set_hadjustment(GTK_SCROLLED_WINDOW(scrolled_hsync_window),hadjust);
  //GST_DEBUG("pos_view=%p, data_view=%p", self->priv->sequence_pos_table,self->priv->sequence_table);

  // add vertical separator
  gtk_box_pack_start(GTK_BOX(box), gtk_vseparator_new(), FALSE, FALSE, 0);

  // add hbox for pattern list
  box=gtk_hbox_new(FALSE,0);
  gtk_paned_pack2(GTK_PANED(split_box),box,FALSE,FALSE);

  // add vertical separator
  gtk_box_pack_start(GTK_BOX(box), gtk_vseparator_new(), FALSE, FALSE, 0);

  // add pattern list-view
  scrolled_window=gtk_scrolled_window_new(NULL,NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window),GTK_SHADOW_NONE);
  self->priv->pattern_list=GTK_TREE_VIEW(gtk_tree_view_new());
  g_object_set(self->priv->pattern_list,
    "enable-search",FALSE,
    "rules-hint",TRUE,
    "fixed-height-mode",TRUE,
    NULL);
  gtk_widget_set_name(GTK_WIDGET(self->priv->pattern_list),"sequence-pattern-list");

  renderer=gtk_cell_renderer_text_new();
  g_object_set(renderer,
    "xalign",1.0,
    NULL);
  gtk_cell_renderer_set_fixed_size(renderer, 1, -1);
  gtk_cell_renderer_text_set_fixed_height_from_font(GTK_CELL_RENDERER_TEXT(renderer), 1);
  if((tree_col=gtk_tree_view_column_new_with_attributes(_("Key"),renderer,
    "text",BT_PATTERN_MODEL_SHORTCUT,
    "sensitive",BT_PATTERN_MODEL_IS_USED,
    NULL))
  ) {
    g_object_set(tree_col,"sizing",GTK_TREE_VIEW_COLUMN_FIXED,"fixed-width",30,NULL);
    gtk_tree_view_insert_column(self->priv->pattern_list,tree_col,-1);
  }
  else GST_WARNING("can't create treeview column");

  renderer=gtk_cell_renderer_text_new();
  gtk_cell_renderer_set_fixed_size(renderer, 1, -1);
  gtk_cell_renderer_text_set_fixed_height_from_font(GTK_CELL_RENDERER_TEXT(renderer), 1);
  if((tree_col=gtk_tree_view_column_new_with_attributes(_("Patterns"),renderer,
    "text",BT_PATTERN_MODEL_LABEL,
    "sensitive",BT_PATTERN_MODEL_IS_USED,
    NULL))
  ) {
    g_object_set(tree_col,"sizing",GTK_TREE_VIEW_COLUMN_FIXED,"fixed-width",70,NULL);
    gtk_tree_view_insert_column(self->priv->pattern_list,tree_col,-1);
  }
  else GST_WARNING("can't create treeview column");

  gtk_container_add(GTK_CONTAINER(scrolled_window),GTK_WIDGET(self->priv->pattern_list));
  gtk_box_pack_start(GTK_BOX(box),GTK_WIDGET(scrolled_window),TRUE,TRUE,0);
  //gtk_paned_pack2(GTK_PANED(split_box),GTK_WIDGET(scrolled_window),FALSE,FALSE);
  pattern_list_refresh(self); // this is needed for the initial model creation

  // register event handlers
  g_signal_connect(self->priv->app, "notify::song", G_CALLBACK(on_song_changed), (gpointer)self);
  // listen to page changes
  g_signal_connect((gpointer)pages, "notify::page", G_CALLBACK(on_page_switched), (gpointer)self);

  // let settings control toolbar style
  g_object_get(self->priv->app,"settings",&settings,NULL);
  on_toolbar_style_changed(settings,NULL,(gpointer)toolbar);
  g_signal_connect(settings, "notify::toolbar-style", G_CALLBACK(on_toolbar_style_changed), (gpointer)toolbar);
  g_object_unref(settings);

  GST_DEBUG("  done");
}

//-- constructor methods

/**
 * bt_main_page_sequence_new:
 * @pages: the page collection
 *
 * Create a new instance
 *
 * Returns: the new instance
 */
BtMainPageSequence *bt_main_page_sequence_new(const BtMainPages *pages) {
  BtMainPageSequence *self;

  self=BT_MAIN_PAGE_SEQUENCE(g_object_new(BT_TYPE_MAIN_PAGE_SEQUENCE,NULL));
  bt_main_page_sequence_init_ui(self,pages);
  return(self);
}

//-- methods

//-- cut/copy/paste

static void sequence_clipboard_get_func(GtkClipboard *clipboard,GtkSelectionData *selection_data,guint info,gpointer data) {
  GST_INFO("get clipboard data, info=%d, data=%p",info,data);
  GST_INFO("sending : [%s]",data);
  // FIXME: do we need to format differently depending on info?
  if(selection_data->target==sequence_atom) {
    gtk_selection_data_set(selection_data,sequence_atom,8,(guchar *)data,strlen(data));
  }
  else {
    // allow pasting into a test editor for debugging
    // its only active if we register the formats in _copy_selection() below
    gtk_selection_data_set_text(selection_data,data,-1);
  }
}

static void sequence_clipboard_clear_func(GtkClipboard *clipboard,gpointer data) {
  GST_INFO("freeing clipboard data, data=%p",data);
  g_free(data);
}

/**
 * bt_main_page_sequence_delete_selection:
 * @self: the sequence subpage
 *
 * Delete (clear) the selected area.
 */
void bt_main_page_sequence_delete_selection(const BtMainPageSequence *self) {
  GtkTreeModel *store;
  glong selection_start_column,selection_start_row;
  glong selection_end_column,selection_end_row;

  if(self->priv->selection_start_column==-1) {
    selection_start_column=selection_end_column=self->priv->cursor_column;
    selection_start_row=selection_end_row=self->priv->cursor_row;
  }
  else {
    selection_start_column=self->priv->selection_start_column;
    selection_start_row=self->priv->selection_start_row;
    selection_end_column=self->priv->selection_end_column;
    selection_end_row=self->priv->selection_end_row;
  }

  GST_INFO("delete sequence region: %3ld,%3ld -> %3ld,%3ld",selection_start_column,selection_start_row,selection_end_column,selection_end_row);

  if((store=sequence_model_get_store(self))) {
    GtkTreePath *path;

    if((path=gtk_tree_path_new_from_indices(selection_start_row,-1))) {
      GtkTreeIter iter;

      if(gtk_tree_model_get_iter(store,&iter,path)) {
        gboolean sequence_changed=FALSE;
        glong i,j;

        for(i=selection_start_row;i<=selection_end_row;i++) {
          for(j=selection_start_column-1;j<selection_end_column;j++) {
            GST_DEBUG("  delete sequence cell: %3ld,%3ld",j,i);
            sequence_changed|=bt_sequence_set_pattern_quick(self->priv->sequence,i,j,NULL);
#ifndef USE_SEQUENCE_GRID_MODEL
            gtk_list_store_set(GTK_LIST_STORE(store),&iter,__BT_SEQUENCE_GRID_MODEL_N_COLUMNS+j," ",-1);
#endif
          }
          if(!gtk_tree_model_iter_next(store,&iter)) {
            if(j<self->priv->selection_end_column) {
              GST_WARNING("  can't get next tree-iter");
            }
            break;
          }
        }
        if(sequence_changed) {
          // repair damage
          bt_sequence_repair_damage(self->priv->sequence);
        }
      }
      else {
        GST_WARNING("  can't get tree-iter for row %ld",selection_start_row);
      }
      gtk_tree_path_free(path);
    }
    else {
      GST_WARNING("  can't get tree-path for row %ld",selection_start_row);
    }
  }
  else {
    GST_WARNING("  can't get tree-model");
  }
  // reset selection
  self->priv->selection_start_column=self->priv->selection_start_row=self->priv->selection_end_column=self->priv->selection_end_row=-1;
}

/**
 * bt_main_page_sequence_cut_selection:
 * @self: the sequence subpage
 *
 * Cut selected area.
 * <note>not yet working</note>
 */
void bt_main_page_sequence_cut_selection(const BtMainPageSequence *self) {
  bt_main_page_sequence_copy_selection(self);
  bt_main_page_sequence_delete_selection(self);
}

/**
 * bt_main_page_sequence_copy_selection:
 * @self: the sequence subpage
 *
 * Copy selected area.
 * <note>not yet working</note>
 */
void bt_main_page_sequence_copy_selection(const BtMainPageSequence *self) {
  if(self->priv->selection_start_row!=-1 && self->priv->selection_start_column!=-1) {
    //GtkClipboard *cb=gtk_clipboard_get_for_display(gdk_display_get_default(),GDK_SELECTION_CLIPBOARD);
    //GtkClipboard *cb=gtk_widget_get_clipboard(GTK_WIDGET(self->priv->sequence_table),GDK_SELECTION_SECONDARY);
    GtkClipboard *cb=gtk_widget_get_clipboard(GTK_WIDGET(self->priv->sequence_table),GDK_SELECTION_CLIPBOARD);
    GtkTargetEntry *targets;
    gint n_targets;
    GString *data=g_string_new(NULL);

    GST_INFO("copying : %ld,%ld - %d,%ld", self->priv->selection_start_column, self->priv->selection_start_row, self->priv->selection_end_column, self->priv->selection_end_row);

    targets = gtk_target_table_make(sequence_atom, &n_targets);

    /* the number of ticks */
    g_string_append_printf(data,"%ld\n",(self->priv->selection_end_row+1)-self->priv->selection_start_row);

    sequence_range_copy(self,
      self->priv->selection_start_column,self->priv->selection_end_column,
      self->priv->selection_start_row,self->priv->selection_end_row,
      data);

    GST_INFO("copying : [%s]",data->str);

    /* put to clipboard */
    if(gtk_clipboard_set_with_data (cb, targets, n_targets,
                     sequence_clipboard_get_func, sequence_clipboard_clear_func,
                     g_string_free (data, FALSE))
    ) {
      gtk_clipboard_set_can_store (cb, NULL, 0);
    }
    else {
      GST_INFO("copy failed");
    }

    gtk_target_table_free (targets, n_targets);
    GST_INFO("copy done");
  }
}

static gboolean sequence_deserialize_pattern_track(BtMainPageSequence *self,GtkTreeModel *store,GtkTreePath *path,gchar **fields,gulong track,gulong row,gboolean *sequence_changed) {
	gboolean res=TRUE;
	GtkTreeIter iter;
	BtMachine *machine;

	GST_INFO("get machine for col %d",track);
	machine=bt_sequence_get_machine(self->priv->sequence,track);
	if(machine) {
    gchar *id, *str;

		g_object_get(machine,"id",&id,NULL);
		if(!strcmp(id,fields[0])) {
			if(gtk_tree_model_get_iter(store,&iter,path)) {
				BtSequence *sequence=self->priv->sequence;
				BtPattern *pattern;
				gint j=1;
				gulong sequence_length;
				
				g_object_get(sequence,"length",&sequence_length,NULL);
			
				while((row<sequence_length) && fields[j] && *fields[j] && res) {
					if(*fields[j]!=' ') {
						pattern=bt_machine_get_pattern_by_id(machine,fields[j]);
						if(!pattern) {
							GST_WARNING("machine %p on track %d, has no pattern with id %s",machine,track,fields[j]);
							str=NULL;
						} else {
							g_object_get(pattern,"name",&str,NULL);
						}
					}
					else {
						pattern=NULL;
						str=NULL;
					}
					(*sequence_changed)|=bt_sequence_set_pattern_quick(sequence,row,track,pattern);
#ifndef USE_SEQUENCE_GRID_MODEL
					gtk_list_store_set(GTK_LIST_STORE(store),&iter,__BT_SEQUENCE_GRID_MODEL_N_COLUMNS+track,str,-1);
#endif
					GST_DEBUG("inserted %s @ %d,%d - changed=%d",str,row,track,sequence_changed);
					g_object_try_unref(pattern);
					g_free(str);
					if(!gtk_tree_model_iter_next(store,&iter)) {
						GST_WARNING("  can't get next tree-iter");
						res=FALSE;
					}
					j++;row++;
				}
			}
			else {
				GST_WARNING("  can't get tree-iter for row %ld",self->priv->cursor_row);
				res=FALSE;
			}
		}
		else {
			GST_INFO("machines don't match in %s <-> %s",fields[0],id);
			res=FALSE;
		}

		g_free(id);
		g_object_unref(machine);
	}
	else {
		GST_INFO("no machine for track");
		res=FALSE;
	}
  return(res);
}

static gboolean sequence_deserialize_label_track(BtMainPageSequence *self,GtkTreeModel *store,GtkTreePath *path,gchar **fields,gulong row) {
	gboolean res=TRUE;
	GtkTreeIter iter;
	gchar *str;

	GST_INFO("paste labels");
	if(gtk_tree_model_get_iter(store,&iter,path)) {
		BtSequence *sequence=self->priv->sequence;
		gint j=1;
		gulong sequence_length;

		g_object_get(sequence,"length",&sequence_length,NULL);
		
		while((row<sequence_length) && fields[j] && *fields[j] && res) {
			if(*fields[j]!=' ') {
				str=fields[j];
			}
			else {
				str=NULL;
			}
			bt_sequence_set_label(sequence,row,str);
#ifndef USE_SEQUENCE_GRID_MODEL
			gtk_list_store_set(GTK_LIST_STORE(store),&iter,BT_SEQUENCE_GRID_MODEL_LABEL,str,-1);
#endif
			GST_DEBUG("inserted %s @ %d",str,row);
			if(!gtk_tree_model_iter_next(store,&iter)) {
				GST_WARNING("  can't get next tree-iter");
				res=FALSE;
			}
			j++;row++;
		}
	}
	else {
		GST_WARNING("  can't get tree-iter for row %ld",self->priv->cursor_row);
		res=FALSE;
	}          		

  return(res);
}


static void sequence_clipboard_received_func(GtkClipboard *clipboard,GtkSelectionData *selection_data,gpointer user_data) {
  BtMainPageSequence *self = BT_MAIN_PAGE_SEQUENCE(user_data);
  gchar **lines;
  guint ticks;
  gchar *data;

  GST_INFO("receiving clipboard data");

  data=(gchar *)gtk_selection_data_get_data(selection_data);
  GST_INFO("pasting : [%s]",data);

  if(!data)
    return;

  lines=g_strsplit_set(data,"\n",0);
  if(lines[0]) {
    GtkTreeModel *store;
    gint i=1;
    gint beg,end;
    gulong sequence_length;
    gchar **fields;
    gboolean res=TRUE;

    g_object_get(self->priv->sequence,"length",&sequence_length,NULL);

    ticks=atol(lines[0]);
    sequence_length--;
    // paste from self->priv->cursor_row to MIN(self->priv->cursor_row+ticks,sequence_length)
    beg=self->priv->cursor_row;
    end=beg+ticks;
    end=MIN(end,sequence_length);
    GST_INFO("pasting from row %d to %d",beg,end);

    if((store=sequence_model_get_store(self))) {
      GtkTreePath *path;

      if((path=gtk_tree_path_new_from_indices(self->priv->cursor_row,-1))) {
      	gboolean sequence_changed=FALSE;
        // process each line (= pattern column)
        while(lines[i] && *lines[i] && (self->priv->cursor_row+(i-1)<=end) && res) {
          fields=g_strsplit_set(lines[i],",",0);
          
          if((self->priv->cursor_column+(i-2))>=0) {
          	res=sequence_deserialize_pattern_track(self,store,path,fields,(self->priv->cursor_column+i-2),self->priv->cursor_row,&sequence_changed);
					}
          else if(*fields[0]==' ') {
          	res=sequence_deserialize_label_track(self,store,path,fields,self->priv->cursor_row);
          }
          g_strfreev(fields);
          i++;
        }
        if(sequence_changed) {
          // repair damage
          bt_sequence_repair_damage(self->priv->sequence);
        }
        gtk_tree_path_free(path);
      }
      else {
        GST_WARNING("  can't get tree-path");
      }
    }
    else {
      GST_WARNING("  can't get tree-model");
    }
  }
  g_strfreev(lines);
}


/**
 * bt_main_page_sequence_paste_selection:
 * @self: the sequence subpage
 *
 * Paste at the top of the selected area.
 * <note>not yet working</note>
 */
void bt_main_page_sequence_paste_selection(const BtMainPageSequence *self) {
  //GtkClipboard *cb=gtk_clipboard_get_for_display(gdk_display_get_default(),GDK_SELECTION_CLIPBOARD);
  //GtkClipboard *cb=gtk_widget_get_clipboard(GTK_WIDGET(self->priv->sequence_table),GDK_SELECTION_SECONDARY);
  GtkClipboard *cb=gtk_widget_get_clipboard(GTK_WIDGET(self->priv->sequence_table),GDK_SELECTION_CLIPBOARD);

  gtk_clipboard_request_contents(cb,sequence_atom,sequence_clipboard_received_func,(gpointer)self);
}

//-- change logger interface

static gboolean bt_main_page_sequence_change_logger_change(const BtChangeLogger *owner,const gchar *data) {
  BtMainPageSequence *self = BT_MAIN_PAGE_SEQUENCE(owner);
  GMatchInfo *match_info;
  gboolean res=FALSE;
  gchar *s;

  GST_INFO("undo/redo: [%s]",data);
  // parse data and apply action
  switch (bt_change_logger_match_method(change_logger_methods, data, &match_info)) {
    case METHOD_SET_PATTERNS: {
			GtkTreeModel *store;
      gchar *str;
      gulong track,s_row,e_row;

      // track, beg, end, patterns
      s=g_match_info_fetch(match_info,1);track=atol(s);g_free(s);
      s=g_match_info_fetch(match_info,2);s_row=atol(s);g_free(s);
      s=g_match_info_fetch(match_info,3);e_row=atol(s);g_free(s);
      str=g_match_info_fetch(match_info,4);
      g_match_info_free(match_info);
      
      GST_DEBUG("-> [%lu|%lu|%lu|%s]",track,s_row,e_row,str);

			if((store=sequence_model_get_store(self))) {
				GtkTreePath *path;
				if((path=gtk_tree_path_new_from_indices(s_row,-1))) {
					gboolean sequence_changed=FALSE;
					gchar **fields=g_strsplit_set(str,",",0);

					res=sequence_deserialize_pattern_track(self,store,path,fields,track,s_row,&sequence_changed);
					if(sequence_changed) {
						// repair damage
						bt_sequence_repair_damage(self->priv->sequence);
					}
					g_strfreev(fields);
					gtk_tree_path_free(path);
				}
			}
			if(res) {
				// move cursor to s_row (+self->priv->bars?)
				self->priv->cursor_row=s_row;
				sequence_view_set_cursor_pos(self);
			}
      g_free(str);
      break;
    }
    case METHOD_SET_LABELS: {
			GtkTreeModel *store;
      gchar *str;
      gulong s_row,e_row;

      // track, beg, end, patterns
      s=g_match_info_fetch(match_info,1);s_row=atol(s);g_free(s);
      s=g_match_info_fetch(match_info,2);e_row=atol(s);g_free(s);
      str=g_match_info_fetch(match_info,3);
      g_match_info_free(match_info);
      
      GST_DEBUG("-> [%lu|%lu|%s]",s_row,e_row,str);

			if((store=sequence_model_get_store(self))) {
				GtkTreePath *path;
				if((path=gtk_tree_path_new_from_indices(s_row,-1))) {
					gchar **fields=g_strsplit_set(str,",",0);

					res=sequence_deserialize_label_track(self,store,path,fields,s_row);
					g_strfreev(fields);
					gtk_tree_path_free(path);
				}
			}
			if(res) {
				// move cursor to s_row (+self->priv->bars?)
				self->priv->cursor_row=s_row;
				sequence_view_set_cursor_pos(self);
			}
      g_free(str);
      break;
    }
		case METHOD_SET_SEQUENCE_PROPERTY: {
		  gchar *key,*val;

      key=g_match_info_fetch(match_info,1);
      val=g_match_info_fetch(match_info,2);
      g_match_info_free(match_info);

		  GST_DEBUG("-> [%s|%s]",key,val);
		  // length
		  // loop-start/end
		  res=TRUE;
      if(!strcmp(key,"loop-start")) {
        g_object_set(self->priv->sequence,"loop-start",atol(val),NULL);
      	sequence_calculate_visible_lines(self);
      } else if(!strcmp(key,"loop-end")) {
      	g_object_set(self->priv->sequence,"loop-end",atol(val),NULL);
      	sequence_calculate_visible_lines(self);
      } else if(!strcmp(key,"length")) {
      	g_object_set(self->priv->sequence,"length",atol(val),NULL);
      	sequence_calculate_visible_lines(self);
      }
			else {
				GST_WARNING("unhandled property '%s'",key);
				res=FALSE;
			}
      g_free(key);
      g_free(val);
      break;
		}
    case METHOD_ADD_TRACK: {
      gchar *mid;
      gulong ix;
      BtSong *song;
      BtSetup *setup;
      BtMachine *machine;

      mid=g_match_info_fetch(match_info,1);
      s=g_match_info_fetch(match_info,2);ix=atol(s);g_free(s);
      g_match_info_free(match_info);

      GST_DEBUG("-> [%s|%lu]",mid,ix);

      // get song from app and then setup from song
      g_object_get(self->priv->app,"song",&song,NULL);
      g_object_get(song,"setup",&setup,NULL);

      if((machine=bt_setup_get_machine_by_id(setup,mid))) {
        sequence_add_track(self,machine,ix);
        g_object_unref(machine);
        
        update_after_track_changed(self);
        res=TRUE;
      }
      g_object_unref(setup);
      g_object_unref(song);
      g_free(mid);
      break;
    }
    case METHOD_REM_TRACK: {
      gulong ix;

      s=g_match_info_fetch(match_info,1);ix=atol(s);g_free(s);
      g_match_info_free(match_info);

      GST_DEBUG("-> [%lu]",ix);
    	sequence_remove_track(self,ix);
    	
    	update_after_track_changed(self);
      res=TRUE;
      break;
    }
    case METHOD_MOVE_TRACK: {
      gulong ix_cur,ix_new;

      s=g_match_info_fetch(match_info,1);ix_cur=atol(s);g_free(s);
      s=g_match_info_fetch(match_info,2);ix_new=atol(s);g_free(s);
      g_match_info_free(match_info);

      GST_DEBUG("-> [%lu|%lu]",ix_cur,ix_new);
      // we only move right/left by one right now
      // @todo: but maybe better change that sequence API, then one function
      // would be all we need
      if(ix_cur>ix_new) {
        if(bt_sequence_move_track_left(self->priv->sequence,ix_cur)) {
          self->priv->cursor_column--;
          res=TRUE;
        }        
      } else {
        if(bt_sequence_move_track_right(self->priv->sequence,ix_cur)) {
          self->priv->cursor_column++;
          res=TRUE;
        }        
      }
      if(res) {
        BtSong *song;
        
        g_object_get(self->priv->app,"song",&song,NULL);
        // reinit the view
#ifndef USE_SEQUENCE_GRID_MODEL
        sequence_table_refresh_model(self,song);
#endif
        sequence_table_refresh_columns(self,song);
        sequence_model_recolorize(self);
        sequence_view_set_cursor_pos(self);
        g_object_unref(song);
      }
      break;
    }
    default:
      GST_WARNING("unhandled undo/redo method: [%s]",data);
  }

  return res;
}

static void bt_main_page_sequence_change_logger_interface_init(gpointer const g_iface, gconstpointer const iface_data) {
  BtChangeLoggerInterface * const iface = g_iface;

  iface->change = bt_main_page_sequence_change_logger_change;
}

//-- wrapper

//-- class internals

static gboolean bt_main_page_sequence_focus(GtkWidget *widget, GtkDirectionType direction) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(widget);

  if(!self->priv->main_window)
    grab_main_window(self);

  GST_DEBUG("focusing default widget");
  gtk_widget_grab_focus_savely(GTK_WIDGET(self->priv->sequence_table));

  // do we need to set the cursor here?
  sequence_view_set_cursor_pos(self);
  /* use status bar */
  bt_child_proxy_set(self->priv->main_window,"statusbar::status",_("Add new tracks from right click context menu."),NULL);
  return FALSE;
}

static void bt_main_page_sequence_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
  BtMainPageSequence *self=BT_MAIN_PAGE_SEQUENCE(object);
  return_if_disposed();
  switch (property_id) {
    case MAIN_PAGE_SEQUENCE_CURSOR_ROW: {
      g_value_set_long(value, self->priv->cursor_row);
    } break;
    default: {
       G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    } break;
  }
}

static void bt_main_page_sequence_dispose(GObject *object) {
  BtMainPageSequence *self = BT_MAIN_PAGE_SEQUENCE(object);
  BtSong *song;

  return_if_disposed();
  self->priv->dispose_has_run = TRUE;

  GST_DEBUG("!!!! self=%p",self);

  g_object_get(self->priv->app,"song",&song,NULL);
  if(song) {
    BtSetup *setup;
    BtSongInfo *song_info;
    GstBin *bin;
    GstBus *bus;

    GST_DEBUG("disconnect handlers from song=%p, song->ref_ct=%d",song,G_OBJECT_REF_COUNT(song));
    g_object_get(song,"setup",&setup,"song-info",&song_info,"bin", &bin,NULL);

    g_signal_handlers_disconnect_matched(song,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_song_play_pos_notify,NULL);
    g_signal_handlers_disconnect_matched(song,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_song_is_playing_notify,NULL);
    g_signal_handlers_disconnect_matched(setup,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_machine_added,NULL);
    g_signal_handlers_disconnect_matched(setup,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_machine_removed,NULL);
    g_signal_handlers_disconnect_matched(song_info,G_SIGNAL_MATCH_FUNC,0,0,NULL,on_song_info_bars_changed,NULL);

    bus=gst_element_get_bus(GST_ELEMENT(bin));
    g_signal_handlers_disconnect_matched(bus, G_SIGNAL_MATCH_FUNC,0,0,NULL,on_track_level_change,NULL);
    gst_object_unref(bus);

    gst_object_unref(bin);
    g_object_unref(setup);
    g_object_unref(song_info);
    g_object_unref(song);
  }

  g_object_try_unref(self->priv->sequence);
  self->priv->main_window=NULL;

  g_object_unref(self->priv->change_log);
  g_object_unref(self->priv->app);

  if(self->priv->machine) {
    GST_INFO("unref old cur-machine: %p,refs=%d",self->priv->machine,G_OBJECT_REF_COUNT(self->priv->machine));
    if(self->priv->pattern_removed_handler)
      g_signal_handler_disconnect(self->priv->machine,self->priv->pattern_removed_handler);
    g_object_unref(self->priv->machine);
  }

  gtk_widget_destroy(GTK_WIDGET(self->priv->context_menu));
  g_object_unref(self->priv->context_menu);

  g_object_try_unref(self->priv->accel_group);

  if(self->priv->clock) gst_object_unref(self->priv->clock);

  GST_DEBUG("  chaining up");
  G_OBJECT_CLASS(bt_main_page_sequence_parent_class)->dispose(object);
}

static void bt_main_page_sequence_finalize(GObject *object) {
  BtMainPageSequence *self = BT_MAIN_PAGE_SEQUENCE(object);

  GST_DEBUG("!!!! self=%p",self);
  g_mutex_free(self->priv->lock);
  g_hash_table_destroy(self->priv->level_to_vumeter);

  G_OBJECT_CLASS(bt_main_page_sequence_parent_class)->finalize(object);
}

static void bt_main_page_sequence_init(BtMainPageSequence *self) {
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, BT_TYPE_MAIN_PAGE_SEQUENCE, BtMainPageSequencePrivate);
  GST_DEBUG("!!!! self=%p",self);
  self->priv->app = bt_edit_application_new();

  self->priv->bars=16;
  //self->priv->cursor_column=0;
  //self->priv->cursor_row=0;
  self->priv->selection_start_column=-1;
  self->priv->selection_start_row=-1;
  self->priv->selection_end_column=-1;
  self->priv->selection_end_row=-1;
  self->priv->sequence_length=self->priv->bars;

  self->priv->lock=g_mutex_new();

  // the undo/redo changelogger
  self->priv->change_log=bt_change_log_new();
  bt_change_log_register(self->priv->change_log,BT_CHANGE_LOGGER(self));
}

static void bt_main_page_sequence_class_init(BtMainPageSequenceClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GtkWidgetClass *gtkwidget_class = GTK_WIDGET_CLASS(klass);

  sequence_atom=gdk_atom_intern_static_string("application/buzztard::sequence");

  column_index_quark=g_quark_from_static_string("BtMainPageSequence::column-index");
  bus_msg_level_quark=g_quark_from_static_string("level");
  vu_meter_skip_update=g_quark_from_static_string("BtMainPageSequence::skip-update");

  g_type_class_add_private(klass,sizeof(BtMainPageSequencePrivate));

  gobject_class->get_property = bt_main_page_sequence_get_property;
  gobject_class->dispose      = bt_main_page_sequence_dispose;
  gobject_class->finalize     = bt_main_page_sequence_finalize;

  gtkwidget_class->focus      = bt_main_page_sequence_focus;

  g_object_class_install_property(gobject_class,MAIN_PAGE_SEQUENCE_CURSOR_ROW,
                                  g_param_spec_long("cursor-row",
                                     "cursor-row prop",
                                     "position of the cursor in the sequence view in bars",
                                     0,
                                     G_MAXLONG,  // loop-positions are LONG as well
                                     0,
                                     G_PARAM_READABLE|G_PARAM_STATIC_STRINGS));
}

