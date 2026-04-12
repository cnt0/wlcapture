wscan ?= $(shell pkg-config --variable=wayland_scanner wayland-scanner)
protos ?= $(shell pkg-config --variable=pkgdatadir wayland-protocols)

PKGCONF_DEPS := wayland-client libavutil libavcodec libswscale

LDLIBS += $(shell pkg-config --libs $(PKGCONF_DEPS))
CFLAGS += -std=c23 -MMD -MP $(shell pkg-config --cflags $(PKGCONF_DEPS))

# CFLAGS += -fsanitize=address,undefined
# LDFLAGS += -fsanitize=address,undefined

PROTO_HEADERS := ext-image-capture-source-v1.h ext-image-copy-capture-v1.h ext-foreign-toplevel-list-v1.h
PROTO_OBJECTS := $(PROTO_HEADERS:.h=.o)
PROTO_FILES   := $(PROTO_HEADERS:.h=.c)

MAIN_OBJECTS := wlcapture.o $(PROTO_OBJECTS)
MAIN_DEPS := $(MAIN_OBJECTS:.o=.d)

wlcapture: $(MAIN_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(MAIN_OBJECTS): $(PROTO_HEADERS)

ext-image-capture-source-v1.h: $(protos)/staging/ext-image-capture-source/ext-image-capture-source-v1.xml
	$(wscan) client-header $^ $@

ext-image-capture-source-v1.c: $(protos)/staging/ext-image-capture-source/ext-image-capture-source-v1.xml
	$(wscan) private-code $^ $@

ext-image-copy-capture-v1.h: $(protos)/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml
	$(wscan) client-header $^ $@

ext-image-copy-capture-v1.c: $(protos)/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml
	$(wscan) private-code $^ $@

ext-foreign-toplevel-list-v1.h: $(protos)/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml
	$(wscan) client-header $^ $@

ext-foreign-toplevel-list-v1.c: $(protos)/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml
	$(wscan) private-code $^ $@

-include $(MAIN_DEPS)

.PHONY: clean
clean:
	rm -f wlcapture $(MAIN_OBJECTS) $(MAIN_DEPS) $(PROTO_HEADERS) $(PROTO_FILES)
