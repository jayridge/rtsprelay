/*
 *
 * gst-launch-1.0 videotestsrc is-live=1 ! vtenc_h264 max-keyframe-interval=30 realtime=1 allow-frame-reordering=0 !     video/x-h264,profile=baseline,width=640,height=480,framerate=10/1 ! queue ! rtspclientsink debug=0 latency=0 location=rtsp://127.0.0.1:8554/test/record protocols=udp
 * GST_DEBUG=*:3 gst-launch-1.0 rtspsrc location="rtsp://localhost:8554/test" latency=50 debug=1 ! decodebin ! autovideosink
 * GST_DEBUG=*:4 gst-launch-1.0 rtspsrc location="rtsp://localhost:8554/test" latency=50 debug=1 protocols=tcp ! rtph264depay ! h264parse ! avdec_h264 ! autovideosink
 */

#include <stdlib.h>
#include <stdio.h>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#define DEFAULT_RTSP_PORT "8554"

#define RECORD_BIN "( rtph264depay name=depay0 ! h264parse ! appsink name=recordsink )"
#define PLAY_BIN "( appsrc name=appsrc is-live=1 do-timestamp=0 caps=\"video/x-h264, stream-format=(string)avc, alignment=(string)au, level=(string)3.1, profile=(string)constrained-baseline\" ! progressreport !" \
                 "  rtph264pay pt=96 config-interval=5 name=pay0 )"

static char *port = (char *) DEFAULT_RTSP_PORT;

static GOptionEntry entries[] = {
    {"port", 'p', 0, G_OPTION_ARG_STRING, &port,
     "Port to listen on (default: " DEFAULT_RTSP_PORT ")", "PORT"},
    {NULL}
};

typedef struct
{
    GstClockTime timestamp;
    GstElement   *appsrc;
    int          buffers_relayed;
} RelayCtx;


static GstPadProbeReturn
cb_have_data (GstPad          *pad,
              GstPadProbeInfo *info,
              gpointer         user_data)
{
    RelayCtx *ctx = (RelayCtx *) user_data;
    GstMapInfo map;
    guint16 *ptr;
    gsize size;
    GstBuffer *buffer, *buffer2;
    GstFlowReturn ret;
    GstCaps *caps;

    if (ctx->appsrc && ctx->buffers_relayed == 0) {
        caps = gst_pad_get_current_caps (pad);
        fprintf(stderr, "****CAPS: %s\n", gst_caps_to_string (caps));
        g_object_set (G_OBJECT (ctx->appsrc), "caps",
                      gst_caps_copy(caps), NULL);
        gst_caps_unref(caps);
    }

    buffer = GST_PAD_PROBE_INFO_BUFFER (info);
    if (ctx->appsrc && gst_buffer_map (buffer, &map, GST_MAP_WRITE)) {
        ptr = (guint16 *) map.data;
        size = gst_buffer_get_size(buffer);
        buffer2 = gst_buffer_new_allocate(NULL, size, NULL);
        gst_buffer_fill(buffer2, 0, ptr, size);

        g_signal_emit_by_name (ctx->appsrc, "push-buffer", buffer2, &ret);
        if (ret != 0) {
            fprintf(stderr, "push-buffer %d\n", ret);
            gst_object_unref (ctx->appsrc);
            ctx->appsrc = NULL;
        }
        gst_buffer_unmap (buffer, &map);
        ctx->buffers_relayed++;
    }
    return GST_PAD_PROBE_OK;
}

static void
record_media_configure (GstRTSPMediaFactory * factory, GstRTSPMedia * media,
                        gpointer user_data)
{
    RelayCtx *ctx = (RelayCtx *) user_data;
    GstElement *element, *bin;
    GstPad *pad;

    /* get the element used for providing the streams of the media */
    bin = gst_rtsp_media_get_element (media);

    /* get our appsink, we named it 'recordsink' with the name property */
    element = gst_bin_get_by_name_recurse_up (GST_BIN (bin), "recordsink");

    /* get the sink pad */
    pad = gst_element_get_static_pad (element, "sink");

    /* set a buffer probe to drain the record bin */
    gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
                       (GstPadProbeCallback) cb_have_data, ctx, NULL);

    /* no longer need these refs */
    gst_object_unref (element);
    gst_object_unref (bin);
}


static void
play_media_configure (GstRTSPMediaFactory * factory, GstRTSPMedia * media,
                      gpointer user_data)
{
    RelayCtx *ctx = (RelayCtx *) user_data;
    GstElement *element, *bin;

    /* get the element used for providing the streams of the media */
    bin = gst_rtsp_media_get_element (media);

    /* get our appsrc, we named it 'appsrc' with the name property */
    element = gst_bin_get_by_name_recurse_up (GST_BIN (bin), "appsrc");

    /* set the format to time, appsrc will timestamp incoming buffers */
    gst_util_set_object_arg (G_OBJECT (element), "format", "time");

    /* hold ref to appsrc, this  bin is shared across play requests */
    ctx->appsrc = element;
    ctx->buffers_relayed = 0;

    /* no longer need these refs */
    gst_object_unref (bin);
}

int
main (int argc, char *argv[])
{
    GMainLoop *loop;
    GstRTSPServer *server;
    GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *recordfactory, *playfactory;
    GOptionContext *optctx;
    GError *error = NULL;
    RelayCtx *ctx;

    optctx = g_option_context_new ("RTSP Relay Server\n\n"
        "See README for instructions ( https://github.com/jayridge/rtsprelay )\n\n");

    g_option_context_add_main_entries (optctx, entries, NULL);
    g_option_context_add_group (optctx, gst_init_get_option_group ());
    if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
        g_printerr ("Error parsing options: %s\n", error->message);
        g_option_context_free (optctx);
        g_clear_error (&error);
        return -1;
    }
    g_option_context_free (optctx);

    loop = g_main_loop_new (NULL, FALSE);

    /* create a server instance */
    server = gst_rtsp_server_new ();
    g_object_set (server, "service", port, NULL);

    /* get the mount points for this server, every server has a default object
    * that be used to map uri mount points to media factories */
    mounts = gst_rtsp_server_get_mount_points (server);

    /* make a record media factory for a test stream.
    * any launch line works as long as it contains elements named pay%d. Each
    * element with pay%d names will be a stream */
    recordfactory = gst_rtsp_media_factory_new ();
    gst_rtsp_media_factory_set_transport_mode (recordfactory,
        GST_RTSP_TRANSPORT_MODE_RECORD);
    gst_rtsp_media_factory_set_latency (recordfactory, 50);
    gst_rtsp_media_factory_set_launch (recordfactory, RECORD_BIN);

    /* make a play media factory for a test stream. */
    playfactory = gst_rtsp_media_factory_new ();
    gst_rtsp_media_factory_set_shared(playfactory, TRUE);
    gst_rtsp_media_factory_set_transport_mode (playfactory,
        GST_RTSP_TRANSPORT_MODE_PLAY);
    gst_rtsp_media_factory_set_latency (playfactory, 0);
    gst_rtsp_media_factory_set_launch (playfactory, PLAY_BIN);

    /* notify when our media is ready, This is called whenever someone asks for
     * the media and a new pipeline is created */
    ctx = g_new0(RelayCtx, 1);
    g_signal_connect (recordfactory, "media-configure",
                      (GCallback) record_media_configure, ctx);
    g_signal_connect (playfactory, "media-configure",
                      (GCallback) play_media_configure, ctx);

    /* attach the test factory to the /test url */
    gst_rtsp_mount_points_add_factory (mounts, "/test/record", recordfactory);
    gst_rtsp_mount_points_add_factory (mounts, "/test", playfactory);

    /* don't need the ref to the mapper anymore */
    g_object_unref (mounts);

    /* attach the server to the default maincontext */
    gst_rtsp_server_attach (server, NULL);

    /* start serving */
    g_print ("stream ready at rtsp://127.0.0.1:%s\n", port);
    g_main_loop_run (loop);

    g_free(ctx);

    return 0;
}
