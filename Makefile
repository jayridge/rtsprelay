BLDDIR = build
TARGET = rtsprelay

CFLAGS = -g -O0 --std=c99 -Wall
LIBS =

PKGS = gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-1.0 gstreamer-rtsp-server-1.0

CFLAGS += $(shell pkg-config --cflags ${PKGS})
LDFLAGS += $(shell pkg-config --libs ${PKGS})


all: $(BLDDIR)/$(TARGET)

$(BLDDIR)/$(TARGET): rtsprelay.c
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS)

clean:
	rm -rf $(BLDDIR) || true
