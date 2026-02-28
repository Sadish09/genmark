cmake_minimum_required(VERSION 3.20)

project(genmark_core
        VERSION 0.1.0
        DESCRIPTION "GenMark Core Watermarking Engine"
        LANGUAGES CXX
)

# -----------------------------------------
# Options
# -----------------------------------------

option(GENMARK_BUILD_SHARED "Build GenMark as shared library" OFF)

# -----------------------------------------
# Detect source files
# -----------------------------------------

file(GLOB_RECURSE GENMARK_CORE_SOURCES
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

# -----------------------------------------
# Library Type
# -----------------------------------------

if(GENMARK_CORE_SOURCES)
    if(GENMARK_BUILD_SHARED)
        add_library(genmark_core SHARED ${GENMARK_CORE_SOURCES})
    else()
        add_library(genmark_core STATIC ${GENMARK_CORE_SOURCES})
    endif()
else()
    # Header-only for now
    add_library(genmark_core INTERFACE)
endif()

# -----------------------------------------
# Include Directories
# -----------------------------------------

target_include_directories(genmark_core
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# -----------------------------------------
# C++ Standard
# -----------------------------------------

if(TARGET genmark_core AND NOT GENMARK_CORE_SOURCES)
    target_compile_features(genmark_core INTERFACE cxx_std_20)
else()
    target_compile_features(genmark_core PUBLIC cxx_std_20)
endif()

# -----------------------------------------
# Warnings (only if compiled)
# -----------------------------------------

if(GENMARK_CORE_SOURCES)
    target_compile_options(genmark_core
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
    )
endif()

# -----------------------------------------
# Third-party (currently empty)
# -----------------------------------------

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CMakeLists.txt)
    add_subdirectory(third_party)
endif()

# -----------------------------------------
# Position Independent Code (for shared later)
# -----------------------------------------

set_target_properties(genmark_core
        PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

# -----------------------------------------
# Install Rules
# -----------------------------------------

include(GNUInstallDirs)

install(TARGETS genmark_core
        EXPORT genmarkTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT genmarkTargets
        FILE genmarkTargets.cmake
        NAMESPACE genmark::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/genmark
)

# -----------------------------------------
# Export for parent projects
# -----------------------------------------

export(EXPORT genmarkTargets
        FILE ${CMAKE_CURRENT_BINARY_DIR}/genmarkTargets.cmake
        NAMESPACE genmark::
)