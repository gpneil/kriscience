#include <gst/gst.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct
{
  GMainLoop *loop;
  GstElement *pipeline;
} AppData;

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  AppData *app_data = (AppData *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_message ("End of stream");
      g_main_loop_quit (app_data->loop);
      break;

    case GST_MESSAGE_ERROR:
    {
      gchar *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);

      g_printerr ("Error: %s\n\t%s\n",
          GST_STR_NULL (error->message), GST_STR_NULL (debug));
      g_error_free (error);
      g_free (debug);

      g_main_loop_quit (app_data->loop);
      break;
    }

    default:
      break;
  }

  return TRUE;
}

static gboolean
pipeline_stuff (GstElement * pipeline, gchar cmd)
{
  g_message ("Pipeline stuff. Received command: %c", cmd);
  return TRUE;
}

static gboolean
io_callback (GIOChannel * io, GIOCondition condition, gpointer data)
{
  gchar in;

  AppData *app_data = (AppData *) data;
  GError *error = NULL;

  switch (g_io_channel_read_chars (io, &in, 1, NULL, &error)) {

    case G_IO_STATUS_NORMAL:
      if ('q' == in) {
        g_main_loop_quit (app_data->loop);
        return FALSE;
      } else if ('\n' != in && !pipeline_stuff (app_data->pipeline, in)) {
        g_warning ("Pipeline stuff failed");
      }

      return TRUE;

    case G_IO_STATUS_ERROR:
      g_printerr ("IO error: %s\n", error->message);
      g_error_free (error);

      return FALSE;

    case G_IO_STATUS_EOF:
      g_warning ("No input data available");
      return TRUE;

    case G_IO_STATUS_AGAIN:
      return TRUE;

    default:
      g_return_val_if_reached (FALSE);
      break;
  }

  return FALSE;
}

int
main (int argc, char *argv[])
{
  GError *error = NULL;

  AppData data = { NULL, NULL };

  GIOChannel *io = NULL;

  GstBus *bus = NULL;
  guint bus_watch_id = 0, io_watch_id = 0;

  int ret = EXIT_FAILURE;

  gchar **argvn = NULL;

  if (1 == argc) {
    fprintf (stderr, "Error: Missing argument\n"
        "USAGE: %s <pipeline description>\n", argv[0]);
    return EXIT_FAILURE;
  }

  /* we must initialise the threading system before using any
   * other GLib function, such as g_option_context_new() */
  if (!g_thread_supported ()) {
    g_thread_init (NULL);
  }

  gst_init (&argc, &argv);

  /* make a null-terminated version of argv */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
  {
    data.pipeline =
        (GstElement *) gst_parse_launchv ((const gchar **) argvn, &error);
  }
  g_free (argvn);

  /* handling pipeline creation failure */
  if (!data.pipeline) {
    g_printerr ("ERROR: pipeline could not be constructed: %s\n",
        error ? GST_STR_NULL (error->message) : "(unknown error)");
    goto untergang;
  } else if (error) {
    g_printerr ("Erroneous pipeline: %s\n", GST_STR_NULL (error->message));
    goto untergang;
  }

  data.loop = g_main_loop_new (NULL, FALSE);

  /* bus callback */
  bus = gst_pipeline_get_bus (GST_PIPELINE (data.pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, &data);
  gst_object_unref (GST_OBJECT (bus));

  /* standard input callback */
  io = g_io_channel_unix_new (STDIN_FILENO);
  io_watch_id = g_io_add_watch (io, G_IO_IN, io_callback, &data);
  g_io_channel_unref (io);

  g_message ("Running...");
  if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (data.pipeline,
          GST_STATE_PLAYING)) {
    g_printerr ("Failed to play the pipeline\n");
    goto untergang;
  }

  g_main_loop_run (data.loop);

  g_message ("Returned, stopping playback");
  if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (data.pipeline,
          GST_STATE_NULL)) {
    g_printerr ("Failed to stop the pipeline\n");
    goto untergang;
  }

  ret = EXIT_SUCCESS;

untergang:
  if (0 != bus_watch_id)
    g_source_remove (bus_watch_id);
  if (data.pipeline)
    gst_object_unref (GST_OBJECT (data.pipeline));
  if (error)
    g_error_free (error);
  if (data.loop)
    g_main_loop_unref (data.loop);

  return ret;
}

