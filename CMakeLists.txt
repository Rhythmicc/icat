cmake_minimum_required(VERSION 3.10)
project(ImagePreview VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(ENABLE_PDF "Enable PDF/EPS support using Poppler" ON)
option(ENABLE_SVG "Enable SVG support using librsvg" ON)
option(FORCE_KITTY_PROTOCOL "Force Kitty protocol output regardless of terminal detection" OFF)

find_package(PkgConfig REQUIRED)

# OpenMP support (especially for macOS)
if(APPLE)
    # Search for Homebrew's libomp
    execute_process(COMMAND brew --prefix libomp OUTPUT_VARIABLE LIBOMP_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(LIBOMP_PREFIX)
        message(STATUS "Found libomp at: ${LIBOMP_PREFIX}")
        set(OPENMP_INCLUDE_DIRS "${LIBOMP_PREFIX}/include")
        set(OPENMP_LIBRARIES "${LIBOMP_PREFIX}/lib/libomp.dylib")
        set(OPENMP_FLAGS "-Xpreprocessor" "-fopenmp")
        set(OPENMP_FOUND TRUE)
    else()
        find_package(OpenMP)
    endif()
else()
    find_package(OpenMP REQUIRED)
endif()

# Library
add_library(ImagePreview src/image_preview.cpp)
target_include_directories(ImagePreview PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(FORCE_KITTY_PROTOCOL)
    target_compile_definitions(ImagePreview PRIVATE FORCE_KITTY_PROTOCOL)
endif()

if(OPENMP_FOUND)
    if(APPLE AND LIBOMP_PREFIX)
        target_compile_options(ImagePreview PRIVATE ${OPENMP_FLAGS})
        target_include_directories(ImagePreview PRIVATE ${OPENMP_INCLUDE_DIRS})
        target_link_libraries(ImagePreview PRIVATE ${OPENMP_LIBRARIES})
        target_compile_definitions(ImagePreview PRIVATE OpenMP_FOUND)
    else()
        target_link_libraries(ImagePreview PRIVATE OpenMP::OpenMP_CXX)
    endif()
endif()

set(HAVE_POPPLER_INTERNAL OFF)
if(ENABLE_PDF)
    pkg_check_modules(POPPLER IMPORTED_TARGET poppler-cpp)
    if(POPPLER_FOUND)
        message(STATUS "Poppler found, PDF/EPS support enabled")
        target_compile_definitions(ImagePreview PRIVATE HAVE_POPPLER)
        target_link_libraries(ImagePreview PRIVATE PkgConfig::POPPLER)
        set(HAVE_POPPLER_INTERNAL ON)
    else()
        message(WARNING "Poppler not found, PDF/EPS support will be disabled")
    endif()
endif()

set(HAVE_RSVG_INTERNAL OFF)
if(ENABLE_SVG)
    pkg_check_modules(RSVG IMPORTED_TARGET librsvg-2.0)
    if(RSVG_FOUND)
        message(STATUS "librsvg found, SVG support enabled")
        target_compile_definitions(ImagePreview PRIVATE HAVE_RSVG)
        target_link_libraries(ImagePreview PRIVATE PkgConfig::RSVG)
        set(HAVE_RSVG_INTERNAL ON)
    else()
        message(WARNING "librsvg not found, SVG support will be disabled")
    endif()
endif()

# Executable
add_executable(icat main.cpp)
target_link_libraries(icat PRIVATE ImagePreview)

# Installation
include(GNUInstallDirs)

install(TARGETS ImagePreview icat
    EXPORT ImagePreviewTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES 
    include/image_preview.h 
    include/stb_image.h 
    include/stb_image_write.h 
    include/stb_image_resize.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# CMake Package Configuration
install(EXPORT ImagePreviewTargets
    FILE ImagePreviewTargets.cmake
    NAMESPACE ImagePreview::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ImagePreview
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ImagePreviewConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ImagePreview
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ImagePreviewConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ImagePreviewConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ImagePreviewConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ImagePreview
)
