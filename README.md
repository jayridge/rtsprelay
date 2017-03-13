rtsprelay
===

RTSPrelay relays a video stream provided by an RTSP RECORD session to one or
more PLAY sessions. It is intended as a proof of concept and has several
limitations.

* Ony h.264 video and aac audio streams are supported.
* No authentication/authorization.

## dependencies

* gstreamer-rtsp-server-1.0
* gstreamer-1.0
* gst-plugins-good-1.0
* gst-plugins-bad-1.0

see https://gstreamer.freedesktop.org/modules/gst-rtsp-server.html

## build

To build simply type `make`. This will create the binary `build/rtsprelay`

## run

The binary `rtsprelay` accepts several arguments. Run with `-h` for details.

## test

To test you must create a RECORD session and one or more PLAY sessions. You will need to update the
rtsp location and encode/decode parameters for your environment.

### record

A record pipeline. Video must be the media element 0.

```
gst-launch-1.0 autoaudiosrc ! audioconvert ! avenc_ac3 ! \
  r. autovideosrc is-live=1 ! queue ! \
  x264enc tune=zerolatency byte-stream=true threads=1 key-int-max=15 intra-refresh=true ! \
  video/x-h264,width=640,height=480,framerate=10/1 ! \
  rtspclientsink debug=1 latency=0 location=rtsp://127.0.0.1:8554/test/record name=r
```

### play

A play pipeline.

```
gst-launch-1.0 -v rtspsrc debug=1 latency=0 location=rtsp://127.0.0.1:8554/test name=r r. ! \
  decodebin ! videoconvert ! autovideosink r. ! decodebin ! audioconvert ! autoaudiosink
```
