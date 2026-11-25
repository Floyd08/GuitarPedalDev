include(FetchContent)
cmake_minimum_required(VERSION 3.20)

target_compile_options(pedal_processor PRIVATE -Wall -Wextra)
target_compile_options(dsplib PRIVATE -Wall -Wextra)

project(util_tests LANGUAGES C)

set(PA_USE_WASAPI OFF CACHE BOOL "" FORCE)	
set(PA_USE_WDMKS OFF CACHE BOOL "" FORCE)
set(PA_USE_DS OFF CACHE BOOL "" FORCE)
set(PA_USE_ASIO ON CACHE BOOL "" FORCE)				

FetchContent_Declare(
	portaudio
	GIT_REPOSITORY https://github.com/PortAudio/portaudio
	GIT_TAG master
)
FetchContent_MakeAvailable(portaudio)


add_library(dsplib STATIC)
target_sources(dsplib PRIVATE extensions/BiquadFilter/Biquad.c extensions/BiquadFilter/one_pole.c src/util/dsp_tools.c)
target_include_directories(dsplib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extensions/BiquadFilter ${CMAKE_CURRENT_SOURCE_DIR}/src/util)


# add_executable(util_tests)
# target_sources(util_tests PRIVATE src/util/util_tests.c)
# target_link_libraries(util_tests PRIVATE dsplib)

add_executable(pedal_processor)
target_sources(pedal_processor PRIVATE src/pedal_processor.c src/ui/ui_listener.c src/analysis/scope_tap.c src/effects_engine.c src/amp_model.c $(wildcard src/effects/*.c))
target_include_directories(pedal_processor PRIVATE src/ui src/analysis src/effects)
target_link_libraries(pedal_processor PRIVATE dsplib portaudio ws2_32)

# -Isrc -Isrc/ui -Isrc/analysis -Isrc/effects -Iextensions/BiquadFilter -Isrc/util