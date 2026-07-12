cmake_minimum_required(VERSION 3.20)

project(scsi_sptd LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "scsi_sptd preserves a 32-bit Windows hook DLL. Configure with -A Win32 or a 32-bit MinGW toolchain.")
endif()

option(SCSI_SPTD_ENABLE_DLLMAIN "Install hooks from DllMain using argv[1] as the CHD path." OFF)

set(SCSI_SPTD_SOURCES
    src/fs_hooks.c
    src/iat_hook.c
    src/sptd.c
)

if(SCSI_SPTD_ENABLE_DLLMAIN)
    list(APPEND SCSI_SPTD_SOURCES src/dllmain.c)
endif()

add_library(scsi_sptd SHARED ${SCSI_SPTD_SOURCES})

target_include_directories(scsi_sptd PRIVATE include)
target_compile_definitions(scsi_sptd PRIVATE
    NOMINMAX
    SCSI_SPTD_BUILD
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(scsi_sptd PRIVATE /W4)
else()
    target_compile_options(scsi_sptd PRIVATE -Wall -Wextra)
endif()

target_link_libraries(scsi_sptd PRIVATE shell32)

# libchd.dll is an external runtime dependency and is not shipped here. If a
# compatible 32-bit copy is placed at third_party/libchd/libchd.dll, copy it
# next to the built DLL for convenience; otherwise supply it on the search path.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libchd/libchd.dll")
    add_custom_command(
        TARGET scsi_sptd
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libchd/libchd.dll"
            "$<TARGET_FILE_DIR:scsi_sptd>/libchd.dll"
    )
endif()
