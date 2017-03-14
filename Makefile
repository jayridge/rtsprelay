BLDDIR = build
TARGET = rtsprelay

CFLAGS = -g -O1 --std=c99 -D_XOPEN_SOURCE=600
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
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
