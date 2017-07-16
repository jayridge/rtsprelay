#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#define DEFAULT_RTSP_PORT "8554"

/* order matters here. first stream must be H264, second MP4A-LATM */
#define RECORD_BIN "( rtph264depay name=depay0 ! fakesink " \
                   "  rtpmp4adepay name=depay1 ! fakesink )"

#define PLAY_BIN "( appsrc name=video_src is-live=1 do-timestamp=1 ! " \
                 "  h264parse ! rtph264pay pt=96 config-interval=5 name=pay0 " \
                 "  appsrc name=audio_src is-live=1 do-timestamp=1 ! " \
                 "  aacparse ! rtpmp4apay pt=97 name=pay1 )"

static char *port = (char *) DEFAULT_RTSP_PORT;

static GOptionEntry entries[] = {
	{"port", 'p', 0, G_OPTION_ARG_STRING, &port, "Listen port", "PORT"},
	{NULL}
};

typedef struct Relay {
	GstRTSPServer *server;
	GHashTable *factories;
} Relay;

typedef struct RelayInstance {
	Relay *relay;
	time_t started;
	gchar *path;
	gchar *play_path;
	gchar *record_path;
	GstElement *video_src;
	GstElement *audio_src;
	int video_buffers;
	int audio_buffers;
	int ref_count;
} RelayInstance;


static void relay_instance_free(void *data)
{
	RelayInstance *ri = (RelayInstance *)data;
	GstRTSPMountPoints *mounts;

	if (ri) {
		printf("relay_instance_free\n");
		if (ri->video_src) {
			gst_object_unref(ri->video_src);
		}
		if (ri->audio_src) {
			gst_object_unref(ri->audio_src);
		}
		mounts = gst_rtsp_server_get_mount_points(ri->relay->server);
		gst_rtsp_mount_points_remove_factory(mounts, ri->record_path);
		gst_rtsp_mount_points_remove_factory(mounts, ri->play_path);
		g_object_unref(mounts);
		g_free(ri->record_path);
		g_free(ri->play_path);
		free(ri);
	}
}

static GstFlowReturn push_buffer(GstElement *src, GstBuffer *buffer)
{
	GstFlowReturn ret = GST_FLOW_NOT_LINKED;
	GstBuffer *buffer2;
	GstMapInfo map;
	guint16 *ptr;
	gsize size;

	if (src && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
		/* copy buffer */
		ptr =(guint16 *) map.data;
		size = gst_buffer_get_size(buffer);
		buffer2 = gst_buffer_new_allocate(NULL, size, NULL);
		gst_buffer_fill(buffer2, 0, ptr, size);

		/* push buffer to play bin */
		g_signal_emit_by_name(src, "push-buffer", buffer2, &ret);
		//printf("push_buffer %d\n", ret);
		gst_buffer_unmap(buffer, &map);
	}
	return ret;
}

static GstPadProbeReturn cb_have_video(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
	RelayInstance *ri = (RelayInstance *)data;
	GstBuffer *buffer;
	GstCaps *caps;

	if (ri->video_src) {
		/* update play sink caps based on record sink */
		if (ri->video_buffers == 0) {
			caps = gst_pad_get_current_caps(pad);
			gchar *capstr = gst_caps_to_string(caps);
			printf("video: %s\n", capstr);
			g_free(capstr);
			g_object_set(G_OBJECT(ri->video_src), "caps",
				     gst_caps_copy(caps), NULL);
			gst_caps_unref(caps);
		}
		buffer = GST_PAD_PROBE_INFO_BUFFER(info);
		push_buffer(ri->video_src, buffer);
		ri->video_buffers++;
	}
	return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn cb_have_audio(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
	RelayInstance *ri = (RelayInstance *)data;
	GstBuffer *buffer;
	GstCaps *caps;

	/* update play sink caps based on record sink */
	if (ri->audio_src) {
		if (ri->audio_buffers == 0) {
			caps = gst_pad_get_current_caps(pad);
			gchar *capstr = gst_caps_to_string(caps);
			printf("audio: %s\n", capstr);
			g_free(capstr);
			g_object_set(G_OBJECT(ri->audio_src), "caps",
				     gst_caps_copy(caps), NULL);
			gst_caps_unref(caps);
		}
		buffer = GST_PAD_PROBE_INFO_BUFFER(info);
		push_buffer(ri->audio_src, buffer);
		ri->audio_buffers++;
	}
	return GST_PAD_PROBE_OK;
}

static void record_media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer data)
{
	RelayInstance *ri = (RelayInstance *)data;
	GstElement *element, *bin;
	GstPad *pad;

	bin = gst_rtsp_media_get_element(media);

	element = gst_bin_get_by_name_recurse_up(GST_BIN(bin), "depay0");
	pad = gst_element_get_static_pad(element, "src");
	gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                          (GstPadProbeCallback)cb_have_video, ri, NULL);
	gst_object_unref(pad);
	gst_object_unref(element);

	element = gst_bin_get_by_name_recurse_up(GST_BIN(bin), "depay1");
	pad = gst_element_get_static_pad(element, "src");
	gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                          (GstPadProbeCallback)cb_have_audio, ri, NULL);
	gst_object_unref(pad);
	gst_object_unref(element);

	gst_object_unref(bin);
}


static void play_media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer data)
{
	RelayInstance *ri = (RelayInstance *)data;
	GstElement *bin;

	gst_rtsp_media_set_reusable(media, 1);
	bin = gst_rtsp_media_get_element(media);
	/* we have reusable media so only set this first time */
	if (!ri->video_src) {
		ri->video_src = gst_bin_get_by_name_recurse_up(GST_BIN(bin), "video_src");
		ri->audio_src = gst_bin_get_by_name_recurse_up(GST_BIN(bin), "audio_src");
		gst_util_set_object_arg(G_OBJECT(ri->video_src), "format", "time");
		gst_util_set_object_arg(G_OBJECT(ri->audio_src), "format", "time");
	}
	gst_object_unref(bin);
}

static RelayInstance * get_relay_instance(Relay *relay, gconstpointer path)
{
	return (RelayInstance *)g_hash_table_lookup(relay->factories, path);
}

static void on_client_closed(GstRTSPClient *client, gpointer data)
{
	RelayInstance *ri = (RelayInstance *)data;

	printf("on_client_closed\n");
	if (--ri->ref_count <= 0) {
		g_hash_table_remove(ri->relay->factories, ri->path);
	}
}

/* inspiration: https://github.com/insonifi/gst-rtsp-dynsrv/blob/master/src/main.c */
static void on_options(GstRTSPClient *client, GstRTSPContext *ctx, gpointer data)
{
	Relay *relay = (Relay *)data;
	RelayInstance *ri;
	GstRTSPMountPoints *mounts;
	GstRTSPMediaFactory *factory;
	gchar **pathv;

	if (!ctx->uri) {
		fprintf(stderr, "on_options no url\n");
		return;
	}

	pathv = gst_rtsp_url_decode_path_components(ctx->uri);
	if (!pathv || !pathv[1]) {
		g_strfreev(pathv);
		return;
	}

	ri = get_relay_instance(relay, pathv[1]);
	if (!ri) {
		printf("creating factories: %s\n", pathv[1]);
		ri = malloc(sizeof(*ri));
		memset(ri, 0, sizeof(*ri));
		ri->relay = relay;
		ri->path = strdup(pathv[1]);
		ri->started = time(NULL);
		g_hash_table_insert(relay->factories, ri->path, ri);

		mounts = gst_rtsp_server_get_mount_points(relay->server);

		ri->play_path = g_build_path("/", "/", pathv[1], NULL);
		factory = gst_rtsp_media_factory_new();
		gst_rtsp_media_factory_set_launch(factory, PLAY_BIN);
		gst_rtsp_media_factory_set_shared(factory, TRUE);
		gst_rtsp_mount_points_add_factory(mounts, ri->play_path, factory);
		g_signal_connect(factory, "media-configure",
	                         (GCallback)play_media_configure, ri);

		ri->record_path = g_build_path("/", "/", pathv[1], "record", NULL);
		factory = gst_rtsp_media_factory_new();
		gst_rtsp_media_factory_set_latency(factory, 0);
		gst_rtsp_media_factory_set_transport_mode(factory, GST_RTSP_TRANSPORT_MODE_RECORD);
		gst_rtsp_media_factory_set_launch(factory, RECORD_BIN);
		gst_rtsp_mount_points_add_factory(mounts, ri->record_path, factory);
		g_signal_connect(factory, "media-configure",
	                         (GCallback)record_media_configure, ri);

		g_object_unref(mounts);
	}
	ri->ref_count++;
	g_strfreev(pathv);
	g_signal_connect(client, "closed", (GCallback)on_client_closed, ri);
}

static void on_client_connect(GstRTSPServer *server, GstRTSPClient *client, gpointer data)
{
	printf("on_client_connect\n");
	g_signal_connect(client, "options-request", (GCallback)on_options, data);
}

static gboolean factory_equal(gconstpointer v1, gconstpointer v2)
{
	size_t l1 = strlen(v1);
	size_t l2 = strlen(v2);
	return strncmp(v1, v2, l1 ? l1 < l2 : l2) == 0;
}

gboolean exit_handler(gpointer data)
{
	GMainLoop *loop = data;
	if (g_main_loop_is_running(loop)) {
		printf("received signal - quitting event loop\n");
		g_main_loop_quit(loop);
	}
	return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv)
{
	Relay *relay;
	GMainLoop *loop;
	GOptionContext *optctx;
	GError *error = NULL;

	// stdout line buffered even if not connected to terminal
	setvbuf(stdout, NULL, _IOLBF, 0);

	optctx = g_option_context_new("RTSP Relay\n\n");

	g_option_context_add_main_entries(optctx, entries, NULL);
	g_option_context_add_group(optctx, gst_init_get_option_group());
	if (!g_option_context_parse(optctx, &argc, &argv, &error)) {
		fprintf(stderr, "g_option_context_parse: %s\n", error->message);
		g_option_context_free(optctx);
		g_clear_error(&error);
		return -1;
	}
	g_option_context_free(optctx);

	loop = g_main_loop_new(NULL, FALSE);
	relay = malloc(sizeof(*relay));
	relay->factories = g_hash_table_new_full(g_str_hash, factory_equal,
                                                 NULL, relay_instance_free);

	/* create a server instance */
	relay->server = gst_rtsp_server_new();
	g_object_set(relay->server, "service", port, NULL);

	/* attach server to main context */
	gst_rtsp_server_attach(relay->server, NULL);

	/* hook into client-connected to create on demand factories */
	g_signal_connect(relay->server, "client-connected",
                         (GCallback)on_client_connect, relay);

	/* unix signal handlers */
	g_unix_signal_add(SIGINT, exit_handler, loop);
	g_unix_signal_add(SIGTERM, exit_handler, loop);

	printf("listening at rtsp://0.0.0.0:%s\n", port);
	g_main_loop_run(loop);
	printf("exiting...\n");

	g_hash_table_unref(relay->factories);
	g_object_unref(relay->server);
	free(relay);

	return 0;
}
