SHELL = /bin/sh

CC = gcc

LDFLAGS = -pipe
LDLIBS = $(shell pkg-config --libs xcb xcb-util xcb-keysyms cairo freetype2) -lm
CFLAGS = $(shell pkg-config --cflags xcb xcb-util xcb-keysyms cairo freetype2) -std=c89 -pipe -Wshadow -Wstrict-prototypes -Wcast-qual -Winline -Wall -Wextra -Wpedantic

target = xcb_test
sources = $(target).c nuklear.c nuklear_xcb.c
objects = $(sources:.c=.o)
make_deps = $(sources:.c=.d)

all: | nuklear.h
all: $(target)

$(target): $(objects)

%.o: CFLAGS += -MMD

nuklear.h:
	wget https://raw.githubusercontent.com/vurtun/nuklear/master/nuklear.h

run: all
	./$(target)

clean:
	$(RM) $(target) $(objects) $(make_deps)

.PHONY: all clean run
.SUFFIXES:
.SUFFIXES: .o .c

ifneq ($(MAKECMDGOALS),clean)
-include $(make_deps)
endif
