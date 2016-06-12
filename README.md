rtsprelay
===

RTSPrelay relays a video stream provided by an RTSP RECORD session to one or
more PLAY sessions. It is intended as a proof of concept and has several
limitations.

* Only one RECORD session is supported.
* Only h264 video is supported.


## dependencies

* gstreamer-rtsp-server-1.0
* gstreamer-1.0
* gst-plugins-good-1.0
* gst-plugins-bad-1.0

( see https://gstreamer.freedesktop.org/modules/gst-rtsp-server.html )

## build

```make```

This will create the binary `build/rtsprelay`

## run

`rtsprelay` accepts several arguments. Run with `-h` for details.

## test

To test you must create a RECORD session and one or more PLAY sessions.

### record

```
gst-launch-1.0 videotestsrc is-live=1 ! vtenc_h264 max-keyframe-interval=30 realtime=1 allow-frame-reordering=0 !     video/x-h264,profile=baseline,width=640,height=480,framerate=10/1 ! queue ! rtspclientsink debug=0 latency=0 location=rtsp://127.0.0.1:8554/test/record
```

### play

A simple generic play pipeline.

```
gst-launch-1.0 rtspsrc location="rtsp://localhost:8554/test" latency=50 debug=1 ! decodebin ! autovideosink sync=0
```

A less simple OSX pipeline that works around some bugs with the vt decoder. This example also illustrates forcing a connection to use TCP.

```
gst-launch-1.0 rtspsrc location="rtsp://localhost:8554/test" latency=50 debug=1 protocols=tcp ! rtph264depay ! h264parse ! avdec_h264 ! autovideosink sync=0
```
