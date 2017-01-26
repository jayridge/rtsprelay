BLDDIR = build
TARGET = rtsprelay

CFLAGS = -g -O2 --std=c99 -Wall
LIBS =

CFLAGS += `pkg-config --cflags gstreamer-1.0 gstreamer-plugins-base-1.0 gstreamer-rtsp-server-1.0`
LDFLAGS += `pkg-config --libs gstreamer-1.0 gstreamer-plugins-base-1.0 gstreamer-rtsp-server-1.0`


all: $(BLDDIR)/$(TARGET)

$(BLDDIR)/$(TARGET): rtsprelay.c
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)

clean:
	rm -rf $(BLDDIR) || true
