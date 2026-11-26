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


add_library(dsp_lib STATIC)
target_sources(dsp_lib PRIVATE extensions/BiquadFilter/Biquad.c extensions/BiquadFilter/one_pole.c src/util/dsp_tools.c src/util/test_signals.c)
target_include_directories(dsp_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extensions/BiquadFilter ${CMAKE_CURRENT_SOURCE_DIR}/src/util)
target_compile_options(dsp_lib PRIVATE -Wall -Wextra)
target_link_libraries(dsp_lib PRIVATE m)

add_library(fx_core STATIC)
target_sources(fx_core PRIVATE src/fx_chain.c src/effects_engine.c src/amp_model.c src/effects/amplitude_effects.c src/effects/bitcrush_effects.c 
				src/effects/compressor_effects.c src/effects/delay_effects.c src/effects/EQ_effects.c src/effects/gain_effects.c 
				src/effects/master_effects.c src/effects/modulation_effects.c src/effects/reverb_effects.c)
target_include_directories(fx_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/effects)
target_link_libraries(fx_core PUBLIC dsp_lib)
target_compile_options(fx_core PRIVATE -Wall -Wextra)


# add_executable(util_tests)
# target_sources(util_tests PRIVATE src/util/util_tests.c)
# target_link_libraries(util_tests PRIVATE dsplib)
# target_compile_options(util_tests PRIVATE -Wall -Wextra)

add_executable(pedal_processor)
target_sources(pedal_processor PRIVATE src/pedal_processor.c src/ui/ui_listener.c src/analysis/scope_tap.c)
target_include_directories(pedal_processor PRIVATE src src/ui src/analysis)
target_link_libraries(pedal_processor PRIVATE dsp_lib fx_core portaudio ws2_32)
target_compile_options(pedal_processor PRIVATE -Wall -Wextra)

if(MINGW)
	#target_link_options(pedal_processor PRIVATE -static-libgcc -static-libstdc++)
	target_link_options(pedal_processor PRIVATE -static)
endif()