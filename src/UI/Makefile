CC       = gcc
TARGET   = audio_knobs.exe
SRC      = audio_knobs.c

CFLAGS   := -Wall -Wextra $(filter-out -Dmain=SDL_main,$(shell pkg-config --cflags sdl2 SDL2_ttf))
LDFLAGS  := $(shell pkg-config --libs sdl2 SDL2_ttf) -lm

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGET)