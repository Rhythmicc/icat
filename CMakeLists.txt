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

# Resize acceleration
if(APPLE)
    option(ENABLE_METAL "Enable Accelerate/vImage SIMD-accelerated resize on macOS" ON)
    if(ENABLE_METAL)
        target_sources(ImagePreview PRIVATE src/resize_metal.mm)
        target_link_libraries(ImagePreview PRIVATE "-framework Accelerate")
        target_compile_definitions(ImagePreview PRIVATE HAVE_METAL)
        message(STATUS "Accelerate/vImage resize enabled")
    endif()
else()
    option(ENABLE_OPENCV "Enable OpenCV GPU-accelerated resize on non-Apple platforms" ON)
    if(ENABLE_OPENCV)
        # Save results from first find before probing CUDA module
        find_package(OpenCV QUIET COMPONENTS core imgproc)
        if(OpenCV_FOUND)
            set(_OCV_LIBS     ${OpenCV_LIBS})
            set(_OCV_INC_DIRS ${OpenCV_INCLUDE_DIRS})
            # Probe for CUDA-accelerated warp module
            find_package(OpenCV QUIET COMPONENTS cudawarping)
            if(OpenCV_FOUND)
                target_compile_definitions(ImagePreview PRIVATE HAVE_OPENCV HAVE_OPENCV_CUDA)
                target_include_directories(ImagePreview PRIVATE ${_OCV_INC_DIRS})
                target_link_libraries(ImagePreview PRIVATE ${OpenCV_LIBS})
                message(STATUS "OpenCV resize enabled (with CUDA)")
            else()
                target_compile_definitions(ImagePreview PRIVATE HAVE_OPENCV)
                target_include_directories(ImagePreview PRIVATE ${_OCV_INC_DIRS})
                target_link_libraries(ImagePreview PRIVATE ${_OCV_LIBS})
                message(STATUS "OpenCV resize enabled (CPU only)")
            endif()
        else()
            message(STATUS "OpenCV not found, using built-in CPU resize")
        endif()
    endif()
endif()

# JPEG DCT prescaling: decode large JPEGs at 1/2, 1/4 or 1/8 resolution in
# the DCT domain, avoiding full-res decode + software resize (same as PIL).
find_package(JPEG QUIET)
if(JPEG_FOUND)
    target_sources(ImagePreview PRIVATE src/jpeg_loader.cpp)
    target_compile_definitions(ImagePreview PRIVATE HAVE_LIBJPEG)
    target_include_directories(ImagePreview PRIVATE ${JPEG_INCLUDE_DIRS})
    target_link_libraries(ImagePreview PRIVATE ${JPEG_LIBRARIES})
    message(STATUS "libjpeg found, JPEG DCT prescaling enabled")
else()
    message(STATUS "libjpeg not found, using stb_image for JPEG (no prescaling)")
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

set(IMAGE_PREVIEW_CONFIG_TEMPLATE "${CMAKE_CURRENT_BINARY_DIR}/ImagePreviewConfig.cmake.in")
file(WRITE "${IMAGE_PREVIEW_CONFIG_TEMPLATE}" [=[@PACKAGE_INIT@

include(CMakeFindDependencyMacro)

# OpenMP might be needed
if(NOT TARGET OpenMP::OpenMP_CXX AND NOT APPLE)
    find_dependency(OpenMP)
endif()

# PkgConfig modules
find_dependency(PkgConfig)

if(@HAVE_POPPLER_INTERNAL@)
    pkg_check_modules(POPPLER IMPORTED_TARGET poppler-cpp)
endif()

if(@HAVE_RSVG_INTERNAL@)
    pkg_check_modules(RSVG IMPORTED_TARGET librsvg-2.0)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/ImagePreviewTargets.cmake")
check_required_components(ImagePreview)
]=])

configure_package_config_file(
    ${IMAGE_PREVIEW_CONFIG_TEMPLATE}
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
