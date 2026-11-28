cmake_minimum_required(VERSION 3.20)
project(GuitarPedalDev LANGUAGES C CXX)


# --- options
option(ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehavourSanitizer" OFF)

if(ENABLE_SANITIZERS AND MINGW)
    message(FATAL_ERROR "Sanitizers aren't supported with MinGW; use the Linux CI build or WSL")
endif()

if(ENABLE_SANITIZERS)
	add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
	add_link_options(-fsanitize=address,undefined)
endif()

include(FetchContent)


FetchContent_Declare(
	unity
	GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity
	GIT_TAG b6763fbd9cedfacaa89e2ad9fd00d615a234e355			# v2.7.0	checked last on: 2026-11-26
)
FetchContent_MakeAvailable(unity)

enable_testing()

# --- libraries
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

# --- Testing
add_executable(test_dsp_tools)
target_sources(test_dsp_tools PRIVATE src/tests/test_dsp_tools.c)
target_link_libraries(test_dsp_tools PRIVATE unity::framework dsp_lib)
target_compile_options(test_dsp_tools PRIVATE -Wall -Wextra)

add_executable(test_amp_model)
target_sources(test_amp_model PRIVATE src/tests/test_amp_model.c)
target_link_libraries(test_amp_model PRIVATE unity::framework fx_core)
target_compile_options(test_amp_model PRIVATE -Wall -Wextra)


add_test(NAME test_dsp_tools COMMAND test_dsp_tools)
add_test(NAME test_amp_model COMMAND test_amp_model)


# --- Windows host machine: PA + ASIO + Winsock ---
if(WIN32)
	
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

	add_executable(pedal_processor)
	target_sources(pedal_processor PRIVATE src/pedal_processor.c src/ui/ui_listener.c src/analysis/scope_tap.c)
	target_include_directories(pedal_processor PRIVATE src src/ui src/analysis)
	target_link_libraries(pedal_processor PRIVATE dsp_lib fx_core portaudio ws2_32)
	target_compile_options(pedal_processor PRIVATE -Wall -Wextra)

	if(MINGW)
		#target_link_options(pedal_processor PRIVATE -static-libgcc -static-libstdc++)
		target_link_options(pedal_processor PRIVATE -static)
	endif()

endif()