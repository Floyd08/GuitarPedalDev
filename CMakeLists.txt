cmake_minimum_required(VERSION 3.20)
project(GuitarPedalDev LANGUAGES C CXX)

include(FetchContent)


set(PA_USE_WASAPI OFF CACHE BOOL "" FORCE)						# turn off any protocol other than ASIO, to make sure we use it
set(PA_USE_WDMKS OFF CACHE BOOL "" FORCE)						# testing showed ASIO is by far faster for live Audio
set(PA_USE_DS OFF CACHE BOOL "" FORCE)
set(PA_USE_ASIO ON CACHE BOOL "" FORCE)
set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PA_USE_WDMKS_DEVICE_INFO OFF CACHE BOOL "" FORCE)			#without WDMKS, the standard device info won't compile

FetchContent_Declare(
	portaudio
	GIT_REPOSITORY https://github.com/PortAudio/portaudio
	GIT_TAG e8138474e338f545361065776a30878022c26106			# checked last on: 2026-11-25
)
FetchContent_MakeAvailable(portaudio)


add_library(dsplib STATIC)
target_sources(dsplib PRIVATE extensions/BiquadFilter/Biquad.c extensions/BiquadFilter/one_pole.c src/util/dsp_tools.c)
target_include_directories(dsplib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extensions/BiquadFilter ${CMAKE_CURRENT_SOURCE_DIR}/src/util)
target_compile_options(dsplib PRIVATE -Wall -Wextra)
target_link_libraries(dsplib PRIVATE m)


add_executable(util_tests)
target_sources(util_tests PRIVATE src/util/util_tests.c)
target_link_libraries(util_tests PRIVATE dsplib)
target_compile_options(util_tests PRIVATE -Wall -Wextra)


add_executable(pedal_processor)
target_sources(pedal_processor PRIVATE src/pedal_processor.c src/ui/ui_listener.c src/analysis/scope_tap.c src/effects_engine.c src/amp_model.c 
				src/effects/amplitude_effects.c src/effects/bitcrush_effects.c src/effects/compressor_effects.c src/effects/delay_effects.c
				src/effects/EQ_effects.c src/effects/gain_effects.c src/effects/master_effects.c src/effects/modulation_effects.c src/effects/reverb_effects.c)
target_include_directories(pedal_processor PRIVATE src src/ui src/analysis src/effects)
target_link_libraries(pedal_processor PRIVATE dsplib portaudio ws2_32)
target_compile_options(pedal_processor PRIVATE -Wall -Wextra)

if(MINGW)
	#target_link_options(pedal_processor PRIVATE -static-libgcc -static-libstdc++)
	target_link_options(pedal_processor PRIVATE -static)
endif()