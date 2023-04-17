/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* gnome-shell-perf-helper: a program to create windows for performance tests
 *
 * Running performance tests with whatever windows a user has open results
 * in unreliable results, so instead we hide all other windows and talk
 * to this program over D-Bus to create just the windows we want.
 */

#include "config.h"

#include <math.h>

#include <gtk/gtk.h>

#define BUS_NAME "org.gnome.Shell.PerfHelper"

static const gchar introspection_xml[] =
	  "<node>"
	  "  <interface name='org.gnome.Shell.PerfHelper'>"
	  "    <method name='Exit'/>"
	  "    <method name='CreateWindow'>"
	  "      <arg type='i' name='width' direction='in'/>"
	  "      <arg type='i' name='height' direction='in'/>"
	  "      <arg type='b' name='alpha' direction='in'/>"
	  "      <arg type='b' name='maximized' direction='in'/>"
	  "      <arg type='b' name='redraws' direction='in'/>"
	  "      <arg type='b' name='text_input' direction='in'/>"
	  "    </method>"
	  "    <method name='WaitWindows'/>"
	  "    <method name='DestroyWindows'/>"
	  "  </interface>"
	"</node>";

static const char application_css[] =
  ".solid { background: rgb(255,255,255); }"
  ".alpha { background: rgba(255,255,255,0.5); }"
  "";

static int opt_idle_timeout = 30;

static GOptionEntry opt_entries[] =
  {
    { "idle-timeout", 'r', 0, G_OPTION_ARG_INT, &opt_idle_timeout, "Exit after N seconds", "N" },
    { NULL }
  };

#define PERF_HELPER_TYPE_APP (perf_helper_app_get_type ())
G_DECLARE_FINAL_TYPE (PerfHelperApp, perf_helper_app, PERF_HELPER, APP, GtkApplication)

struct _PerfHelperApp {
  GtkApplication parent;

  guint timeout_id;
  GList *wait_windows_invocations;
};

G_DEFINE_TYPE (PerfHelperApp, perf_helper_app, GTK_TYPE_APPLICATION);

#define PERF_HELPER_TYPE_WINDOW (perf_helper_window_get_type ())
G_DECLARE_FINAL_TYPE (PerfHelperWindow, perf_helper_window, PERF_HELPER, WINDOW, GtkApplicationWindow)

struct _PerfHelperWindow {
  GtkApplicationWindow parent;

  guint mapped : 1;
  guint exposed : 1;
  guint pending : 1;
};

G_DEFINE_TYPE (PerfHelperWindow, perf_helper_window, GTK_TYPE_APPLICATION_WINDOW);

#define PERF_HELPER_TYPE_WINDOW_CONTENT (perf_helper_window_content_get_type ())
G_DECLARE_FINAL_TYPE (PerfHelperWindowContent, perf_helper_window_content, PERF_HELPER, WINDOW_CONTENT, GtkBox)

struct _PerfHelperWindowContent {
  GtkBox parent;

  guint redraws : 1;

  gint64 start_time;
  gint64 time;
};

G_DEFINE_TYPE (PerfHelperWindowContent, perf_helper_window_content, GTK_TYPE_BOX);

static void destroy_windows           (PerfHelperApp *app);
static void finish_wait_windows       (PerfHelperApp *app);
static void check_finish_wait_windows (PerfHelperApp *app);

static gboolean
on_timeout (gpointer data)
{
  PerfHelperApp *app = data;
  app->timeout_id = 0;

  destroy_windows (app);
  g_application_quit (G_APPLICATION (app));

  return FALSE;
}

static void
establish_timeout (PerfHelperApp *app)
{
  g_clear_handle_id (&app->timeout_id, g_source_remove);

  app->timeout_id = g_timeout_add (opt_idle_timeout * 1000, on_timeout, app);
  g_source_set_name_by_id (app->timeout_id, "[gnome-shell] on_timeout");
}

static void
destroy_windows (PerfHelperApp *app)
{
  GtkApplication *gtk_app = GTK_APPLICATION (app);
  GtkWindow *window;

  while ((window = gtk_application_get_active_window (gtk_app)))
    gtk_widget_destroy (GTK_WIDGET (window));

  check_finish_wait_windows (app);
}

static gboolean
on_window_map_event (GtkWidget        *toplevel,
                     GdkEventAny      *event,
                     PerfHelperWindow *window)
{
  window->mapped = TRUE;

  return FALSE;
}

static void
perf_helper_window_realize (GtkWidget *widget)
{
  GtkWidget *toplevel;

  GTK_WIDGET_CLASS (perf_helper_window_parent_class)->realize (widget);

  toplevel = gtk_widget_get_toplevel (widget);
  g_signal_connect_object (toplevel,
                           "map-event", G_CALLBACK (on_window_map_event),
                           widget, G_CONNECT_DEFAULT);
}

static gboolean
perf_helper_window_draw (GtkWidget  *widget,
                         cairo_t    *cr)
{
  PerfHelperWindow *window = PERF_HELPER_WINDOW (widget);

  GTK_WIDGET_CLASS (perf_helper_window_parent_class)->draw (widget, cr);

  window->exposed = TRUE;

  if (window->exposed && window->mapped && window->pending)
    {
      window->pending = FALSE;
      check_finish_wait_windows (PERF_HELPER_APP (g_application_get_default ()));
    }

  return FALSE;
}

static gboolean
perf_helper_window_content_draw (GtkWidget  *widget,
                                 cairo_t    *cr)
{
  PerfHelperWindowContent *content = PERF_HELPER_WINDOW_CONTENT (widget);
  GtkWindow *window = GTK_WINDOW (gtk_widget_get_toplevel (widget));
  cairo_rectangle_int_t allocation;
  double x_offset, y_offset;

  gtk_widget_get_allocation (widget, &allocation);

  /* We draw an arbitrary pattern of red lines near the border of the
   * window to make it more clear than empty windows if something
   * is drastrically wrong.
   */

  if (content->redraws)
    {
      double position = (content->time - content->start_time) / 1000000.;
      x_offset = 20 * cos (2 * M_PI * position);
      y_offset = 20 * sin (2 * M_PI * position);
    }
  else
    {
      x_offset = y_offset = 0;
    }

  cairo_set_source_rgb (cr, 1, 0, 0);
  cairo_set_line_width (cr, 10);
  cairo_move_to (cr, 0, 40 + y_offset);
  cairo_line_to (cr, allocation.width, 40 + y_offset);
  cairo_move_to (cr, 0, allocation.height - 40 + y_offset);
  cairo_line_to (cr, allocation.width, allocation.height - 40 + y_offset);
  cairo_move_to (cr, 40 + x_offset, 0);
  cairo_line_to (cr, 40 + x_offset, allocation.height);
  cairo_move_to (cr, allocation.width - 40 + x_offset, 0);
  cairo_line_to (cr, allocation.width - 40 + x_offset, allocation.height);
  cairo_stroke (cr);

  return FALSE;
}

static gboolean
tick_callback (GtkWidget     *widget,
               GdkFrameClock *frame_clock,
               gpointer       user_data)
{
  PerfHelperWindowContent *content = user_data;

  if (content->start_time < 0)
    content->start_time = content->time = gdk_frame_clock_get_frame_time (frame_clock);
  else
    content->time = gdk_frame_clock_get_frame_time (frame_clock);

  gtk_widget_queue_draw (widget);

  return TRUE;
}

static GtkWidget *
perf_helper_window_content_new (gboolean redraws) {
  PerfHelperWindowContent *content;
  GtkWidget *widget;

  content = g_object_new (PERF_HELPER_TYPE_WINDOW_CONTENT,
                          "visible", TRUE,
                          NULL);

  content->redraws = redraws;

  widget = GTK_WIDGET (content);

  if (redraws)
    gtk_widget_add_tick_callback (widget, tick_callback, content, NULL);

  return widget;
}

static void
create_window (PerfHelperApp *app,
               int            width,
               int            height,
               gboolean       alpha,
               gboolean       maximized,
               gboolean       redraws,
               gboolean       text_input)
{
  PerfHelperWindow *window;
  GtkWidget *child;

  window = g_object_new (PERF_HELPER_TYPE_WINDOW,
                         "application", app,
                         NULL);

  if (alpha)
    gtk_widget_set_visual (GTK_WIDGET (window), gdk_screen_get_rgba_visual (gdk_screen_get_default ()));
  if (maximized)
    gtk_window_maximize (GTK_WINDOW (window));

  if (text_input)
    {
      child = gtk_entry_new ();
      gtk_widget_show (child);
    }
  else
    {
      gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (window)),
                                   alpha ? "alpha" : "solid");
      child = perf_helper_window_content_new (redraws);
    }

  gtk_container_add (GTK_CONTAINER (window), child);

  gtk_widget_set_size_request (GTK_WIDGET (window), width, height);
  gtk_widget_show (GTK_WIDGET (window));
}

static void
finish_wait_windows (PerfHelperApp *app)
{
  GList *l;

  for (l = app->wait_windows_invocations; l; l = l->next)
    g_dbus_method_invocation_return_value (l->data, NULL);

  g_clear_list (&app->wait_windows_invocations, NULL);
}

static void
check_finish_wait_windows (PerfHelperApp *app)
{
  GList *l;
  gboolean have_pending = FALSE;

  for (l = gtk_application_get_windows (GTK_APPLICATION (app)); l; l = l->next)
    {
      PerfHelperWindow *window = l->data;
      if (window->pending)
        have_pending = TRUE;
    }

  if (!have_pending)
    finish_wait_windows (app);
}

static void
handle_method_call (GDBusConnection       *connection,
		    const gchar           *sender,
		    const gchar           *object_path,
		    const gchar           *interface_name,
		    const gchar           *method_name,
		    GVariant              *parameters,
		    GDBusMethodInvocation *invocation,
		    gpointer               user_data)
{
  PerfHelperApp *app = user_data;

  /* Push off the idle timeout */
  establish_timeout (app);

  if (g_strcmp0 (method_name, "Exit") == 0)
    {
      destroy_windows (app);

      g_dbus_method_invocation_return_value (invocation, NULL);
      g_dbus_connection_flush_sync (connection, NULL, NULL);

      g_application_quit (G_APPLICATION (app));
    }
  else if (g_strcmp0 (method_name, "CreateWindow") == 0)
    {
      int width, height;
      gboolean alpha, maximized, redraws, text_input;

      g_variant_get (parameters, "(iibbbb)",
                     &width, &height,
                     &alpha, &maximized, &redraws, &text_input);

      create_window (app, width, height, alpha, maximized, redraws, text_input);
      g_dbus_method_invocation_return_value (invocation, NULL);
    }
  else if (g_strcmp0 (method_name, "WaitWindows") == 0)
    {
      app->wait_windows_invocations = g_list_prepend (app->wait_windows_invocations, invocation);
      check_finish_wait_windows (app);
    }
  else if (g_strcmp0 (method_name, "DestroyWindows") == 0)
    {
      destroy_windows (app);
      g_dbus_method_invocation_return_value (invocation, NULL);
    }
}

static const GDBusInterfaceVTable interface_vtable =
{
  handle_method_call,
  NULL,
  NULL
};

static void
perf_helper_app_activate (GApplication *app)
{
}


static void
perf_helper_app_startup (GApplication *app)
{
  GtkCssProvider *css_provider;

  G_APPLICATION_CLASS (perf_helper_app_parent_class)->startup (app);

  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (css_provider, application_css, -1, NULL);

  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (css_provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static gboolean
perf_helper_app_dbus_register (GApplication     *app,
                               GDBusConnection  *connection,
                               const char       *object_path,
                               GError          **error)
{
  GDBusNodeInfo *introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);

  g_dbus_connection_register_object (connection,
				     object_path,
				     introspection_data->interfaces[0],
				     &interface_vtable,
				     app,   /* user_data */
				     NULL,  /* user_data_free_func */
				     error);
  return TRUE;
}

static void
perf_helper_app_init (PerfHelperApp *app)
{
}

static void
perf_helper_app_class_init (PerfHelperAppClass *klass)
{
  GApplicationClass *gapp_class = G_APPLICATION_CLASS (klass);

  gapp_class->activate = perf_helper_app_activate;
  gapp_class->startup = perf_helper_app_startup;
  gapp_class->dbus_register = perf_helper_app_dbus_register;
}

static void
perf_helper_window_content_init (PerfHelperWindowContent *content)
{
  content->start_time = -1;
}

static void
perf_helper_window_content_class_init (PerfHelperWindowContentClass *klass) {
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->draw = perf_helper_window_content_draw;
}

static PerfHelperApp *
perf_helper_app_new (void) {
  GApplicationFlags flags = G_APPLICATION_IS_SERVICE |
                            G_APPLICATION_ALLOW_REPLACEMENT |
                            G_APPLICATION_REPLACE;

  return g_object_new (PERF_HELPER_TYPE_APP,
                       "application-id", "org.gnome.Shell.PerfHelper",
                       "flags", flags,
                       NULL);
}

static void
perf_helper_window_init (PerfHelperWindow *window)
{
  window->pending = TRUE;
}

static void
perf_helper_window_class_init (PerfHelperWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->realize = perf_helper_window_realize;
  widget_class->draw = perf_helper_window_draw;
}

int
main (int argc, char **argv)
{
  PerfHelperApp *app = NULL;

  /* Since we depend on this, avoid the possibility of lt-gnome-shell-perf-helper */
  g_set_prgname ("gnome-shell-perf-helper");

  app = perf_helper_app_new ();

  g_application_set_option_context_summary (G_APPLICATION (app),
                                            "Server to create windows for performance testing");
  g_application_add_main_option_entries (G_APPLICATION (app), opt_entries);

  g_application_hold (G_APPLICATION (app));
  establish_timeout (app);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
