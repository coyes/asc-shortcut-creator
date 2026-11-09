CC      = gcc
TARGET  = asc

CFLAGS  = -std=c11 -Wall -Wextra -O2 \
          $(shell pkg-config --cflags gtk4) \
          -Wno-unused-parameter

LDFLAGS = $(shell pkg-config --libs gtk4) \
          -lm

SRCS    = main.c ui.c core.c util.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Header dependencies
main.o:  main.c  state.h core.h ui.h
ui.o:    ui.c    state.h core.h ui.h
core.o:  core.c  state.h util.h core.h stb_image.h stb_image_write.h
util.o:  util.c  state.h util.h

PREFIX  ?= $(HOME)/.local
install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)
