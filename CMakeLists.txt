cmake_minimum_required(VERSION 2.8.12)

project(image-compare)

# Standalone build
if(NOT OBS_OUTPUT_DIR)
    set(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        "${CMAKE_SOURCE_DIR}/cmake/"
        )
    include(ObsHelpers)

    set(CMAKE_C_STANDARD 11)

    find_path(OBS_INCLUDE_DIRS
        obs-module.h
        )
    find_library(OBS_LIBRARIES NAMES
        libobs
        obs
        )
# Build as submodule of obs-studio
else()
    set(OBS_INCLUDE_DIRS
        ../../libobs
        )
    set(OBS_LIBRARIES
        libobs
        )
endif()

include_directories(
    ${OBS_INCLUDE_DIRS}
    )

set(image-compare_SOURCES
    image-compare.c
    )

add_library(image-compare MODULE
    ${image-compare_SOURCES}
    )
target_compile_options(image-compare PUBLIC
    -mmmx
    -msse
    -msse2
    )
target_link_libraries(image-compare
    ${OBS_LIBRARIES}
    )

install_obs_plugin_with_data(image-compare data)
if(WIN32)
	add_custom_command(TARGET image-compare POST_BUILD
		COMMAND ${CMAKE_COMMAND}
			"-DCONFIG=$<CONFIGURATION>"
			"-DTARGET_FILE=$<TARGET_FILE:image-compare>"
			"-DLIB_SUFFIX=${_lib_suffix}"
			-P "${CMAKE_SOURCE_DIR}/cmake/CopyDependencies.cmake")
endif()
