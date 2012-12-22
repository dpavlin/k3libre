CC?=gcc
CFLAGS?=-g -O0
LIBS:=
#LDFLAGS=-static
CFLAGS+=-Wall
#CFLAGS+=-Wformat -Wformat-security -Wextra -Wunused -Wshadow -Wmissing-prototypes -Wcast-qual

# Get the proper include/lib searchpath & library name from the pkgconfig db
CPPFLAGS+=$(shell pkg-config --cflags-only-I libusb-1.0)
LDFLAGS+=$(shell pkg-config --libs-only-L libusb-1.0)
LIBS+=$(shell pkg-config --libs-only-l libusb-1.0)

all: k3flasher

k3flasher: k3flasher.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -DNBD_SERVER $< -o $@ $(LIBS)

clean:
	rm -rf k3flasher

.PHONY: all clean
