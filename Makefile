CC = gcc
CFLAGS = -g -O0 -Wall -Wextra -Iextensions/portaudio/include -Isrc -Isrc/ui -Isrc/effects -Iextensions/BiquadFilter -Isrc/util -mconsole
LDFLAGS = -Lextensions/portaudio/build
LDLIBS = -lportaudio -lwinmm -lole32 -luuid -lsetupapi -lstdc++ -lws2_32

PA_DIR = extensions/portaudio
PA_BUILD_DIR = $(PA_DIR)/build
PA_LIB = $(PA_BUILD_DIR)/libportaudio.a

SRC = src/pedal_processor.c src/ui/ui_listener.c src/effects_engine.c src/amp_model.c $(wildcard src/effects/*.c) src/util/dsp_tools.c extensions/BiquadFilter/one_pole.c extensions/BiquadFilter/Biquad.c
TARGET = pedal_processor.exe

.PHONY: all clean clean-pa

all: $(TARGET)

$(TARGET): $(SRC) $(PA_LIB)
	$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) $(LDLIBS) -o $(TARGET)

$(PA_LIB):
	cmake -S $(PA_DIR) -B $(PA_BUILD_DIR) -G "MinGW Makefiles"
	cmake --build $(PA_BUILD_DIR)

clean:
	rm -f $(TARGET)

clean-pa:
	rm -rf $(PA_BUILD_DIR)