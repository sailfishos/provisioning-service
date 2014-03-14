PKGS = glib-2.0 dbus-1 libwbxml2
EXE = provisioning-service
SRC := $(wildcard src/*.c)
OBJFILES := $(addprefix src/,$(notdir $(SRC:.c=.o)))
LIBS = $(shell pkg-config --libs $(PKGS))
LD_FLAGS := -Wall
CFLAGS += -Wall $(shell pkg-config --cflags $(PKGS)) $(INCLUDES)

FILEWRITE := $(strip $(FILEWRITE))

ifdef FILEWRITE
	CFLAGS += -DFILEWRITE='"$(FILEWRITE)"'
endif

src/$(EXE): $(OBJFILES)
	   gcc $(LD_FLAGS) $(LIBS) -o $@ $^

%.o: src/%.c
	   gcc $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o src/*.d src/$(EXE)

install : src/$(EXE)
	mkdir -p $(DESTDIR)/usr/libexec
	mkdir -p $(DESTDIR)/etc/dbus-1/system.d
	mkdir -p $(DESTDIR)/usr/share/dbus-1/system-services
	mkdir -p $(DESTDIR)/etc/ofono/push_forwarder.d
	cp src/$(EXE) $(DESTDIR)/usr/libexec/
	cp src/provisioning.conf $(DESTDIR)/etc/dbus-1/system.d/
	cp src/org.nemomobile.provisioning.service $(DESTDIR)/usr/share/dbus-1/system-services/
	cp ofono-provisioning.conf $(DESTDIR)/etc/ofono/push_forwarder.d/

