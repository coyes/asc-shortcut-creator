CC      = gcc
CFLAGS  = -std=c11 -O2 -Wall -Wextra \
           $(shell sdl2-config --cflags) \
           -Wno-unused-parameter \
           -Wno-missing-field-initializers \
           -Wno-pedantic
LDFLAGS = $(shell sdl2-config --libs) -lGL -lm -lpthread

TARGET  = asc
SRC     = main.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC) nuklear.h nuklear_sdl_gl2.h stb_image.h stb_image_resize2.h stb_image_write.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)
	@echo "Build successful: ./$(TARGET)"

# Install binary to ~/.local/bin
install: $(TARGET)
	@mkdir -p $(HOME)/.local/bin
	cp $(TARGET) $(HOME)/.local/bin/$(TARGET)
	@echo "Installed to ~/.local/bin/$(TARGET)"

clean:
	rm -f $(TARGET)
