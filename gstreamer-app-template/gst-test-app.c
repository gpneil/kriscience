#include <gst/gst.h>
#include <glib-unix.h>

#include <stdlib.h>
#include <string.h>

static GstElement *
get_element_by_name (GstElement * pipeline, gchar * name)
{
  GstElement *element = gst_bin_get_by_name (GST_BIN (pipeline), name);
  if (!element) {
    g_warning ("Failed to get '%s' element by name", name);
  }

  return element;
}

static GstPadProbeReturn
event_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  g_print ("Got event: %s\n", GST_EVENT_TYPE_NAME (event));

  g_warning ("TODO: Implement me!!!");

  /* TODO:  Decide what to return */
  return GST_PAD_PROBE_OK;
}

static gulong
setup_probe_on_elem (GstElement * element, const gchar * pad_name,
    GstPadProbeType mask)
{
  GstPad *pad;
  gulong probe_id;

  pad = gst_element_get_static_pad (element, pad_name);
  if (!pad) {
    g_warning ("Cannot get pad '%s' on element '%s'", pad_name,
        GST_ELEMENT_NAME (element));
    return 0;
  }

  probe_id = gst_pad_add_probe (pad, mask, event_probe, NULL, NULL);
  gst_object_unref (pad);

  return probe_id;
}

static void
setup_probe (GstElement * pipeline, const gchar * probed_pad)
{
  gchar **names;
  GstElement *elem;

  /* TODO:  Choose what to probe */
  GstPadProbeType probe_mask = GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM;

  if (NULL == strchr (probed_pad, ':')) {
    g_warning ("Invalid pad name: '%s'.  Should be 'elem:pad'", probed_pad);
    return;
  }

  names = g_strsplit (probed_pad, ":", 2);

  elem = get_element_by_name (pipeline, names[0]);
  if (elem) {
    if (0 != setup_probe_on_elem (elem, names[1], probe_mask)) {
      g_message ("Successfully installed probe on '%s'", probed_pad);
    } else {
      g_warning ("Failed to install probe on pad '%s'", probed_pad);
    }

    gst_object_unref (elem);
  }

  g_strfreev (names);
}

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_message ("End of stream");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);

      g_printerr ("Error: %s\n\t%s\n",
          GST_STR_NULL (error->message), GST_STR_NULL (debug));
      g_error_free (error);
      g_free (debug);

      g_main_loop_quit (loop);
      break;
    }

    default:
      break;
  }

  return TRUE;
}

static gboolean
intr_handler (gpointer user_data)
{
  GstElement *pipeline = (GstElement *) user_data;

  g_message ("Handling interrupt:  forcing EOS on the pipeline\n");
  gst_element_send_event (pipeline, gst_event_new_eos ());

  /* remove the signal handler */
  return FALSE;
}

static gboolean
options_parse (int *argc, char ***argv, GOptionEntry * entries)
{
  GOptionContext *ctx;
  GError *error;
  gboolean ret;

  ctx = g_option_context_new ("<pipeline description>");
  g_option_context_add_main_entries (ctx, entries, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  error = NULL;
  ret = g_option_context_parse (ctx, argc, argv, &error);
  if (!ret) {
    g_printerr ("Failed to initialise: %s\n", GST_STR_NULL (error->message));
    g_error_free (error);
  }

  g_option_context_free (ctx);
  return ret;
}

static GstElement *
create_pipeline (int argc, char *argv[])
{
  GstElement *pipeline;
  GError *error = NULL;
  gchar **argvn = NULL;

  /* make a null-terminated version of argv */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
  {
    pipeline =
        (GstElement *) gst_parse_launchv ((const gchar **) argvn, &error);
  }
  g_free (argvn);

  if (!pipeline) {
    g_printerr ("ERROR: pipeline could not be constructed\n");
  }

  if (error) {
    g_printerr ("Erroneous pipeline: %s\n", GST_STR_NULL (error->message));
    g_error_free (error);
  }

  return pipeline;
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline;
  GMainLoop *loop;

  gboolean eos_on_shutdown = FALSE;
  gchar *probed_pad = NULL;
  gboolean verbose = FALSE;

  GOptionEntry options[] = {
    {"eos-on-shutdown", 'e', 0, G_OPTION_ARG_NONE, &eos_on_shutdown,
        "Force EOS on sources before shutting the pipeline down", NULL},
    {"probe-pad", 'p', 0, G_OPTION_ARG_STRING, &probed_pad,
        "name of the pad to install probe on", "elem:pad"},
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
        "output status information and property notifications", NULL},
    {NULL}
  };

  GstBus *bus = NULL;
  guint bus_watch_id = 0;
  guint signal_watch_id = 0;

  int ret = EXIT_FAILURE;

  if (!options_parse (&argc, &argv, options)) {
    return ret;
  }

  gst_init (&argc, &argv);

  pipeline = create_pipeline (argc, argv);
  if (!pipeline) {
    return ret;
  }

  if (probed_pad && '\0' != probed_pad[0]) {
    setup_probe (pipeline, probed_pad);
  }

  if (eos_on_shutdown) {
    signal_watch_id =
        g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
  }

  if (verbose) {
    g_signal_connect (pipeline, "deep-notify",
        G_CALLBACK (gst_object_default_deep_notify), NULL);
  }

  loop = g_main_loop_new (NULL, FALSE);

  // bus callback
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (GST_OBJECT (bus));

  g_message ("Running...");
  if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (pipeline,
          GST_STATE_PLAYING)) {
    g_printerr ("Failed to play the pipeline\n");
    goto untergang;
  }

  g_main_loop_run (loop);

  g_message ("Returned, stopping playback");
  if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (pipeline,
          GST_STATE_NULL)) {
    g_printerr ("Failed to stop the pipeline\n");
    goto untergang;
  }

  ret = EXIT_SUCCESS;

untergang:
  if (0 != signal_watch_id)
    g_source_remove (signal_watch_id);
  if (0 != bus_watch_id)
    g_source_remove (bus_watch_id);
  if (loop)
    g_main_loop_unref (loop);
  if (pipeline)
    gst_object_unref (GST_OBJECT (pipeline));

  return ret;
}
