CC = gcc
CFLAGS = -Wall -Wextra -Iextensions/portaudio/include -Isrc -Iextensions/BiquadFilter -Isrc/Util -mconsole
LDFLAGS = -Lextensions/portaudio/build
LDLIBS = -lportaudio -lwinmm -lole32 -luuid -lsetupapi -lstdc++

PA_DIR = extensions/portaudio
PA_BUILD_DIR = $(PA_DIR)/build
PA_LIB = $(PA_BUILD_DIR)/libportaudio.a

SRC = src/pedal_processor.c src/distortion_effects.c src/distortion_exp.c src/Util/dsp_tools.c extensions/BiquadFilter/Biquad.c
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