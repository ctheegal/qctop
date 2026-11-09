cmake_minimum_required(VERSION 3.13)
project(qctop C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── pkg-config (kept for future optional libraries) ───────────────────────────
find_package(PkgConfig QUIET)

# ── Source files ──────────────────────────────────────────────────────────────
set(SOURCES
    src/main.c
    src/cpu_metrics.c
    src/gpu_metrics.c
    src/hw_info.c
    src/mem_metrics.c
    src/storage_metrics.c
    src/net_metrics.c
    src/thermal_metrics.c
    src/npu_metrics.c
)

add_executable(qctop ${SOURCES})

# ── Include dirs ──────────────────────────────────────────────────────────────
target_include_directories(qctop PRIVATE src)

target_link_libraries(qctop PRIVATE m)

# ── Compiler warnings ─────────────────────────────────────────────────────────
target_compile_options(qctop PRIVATE -Wall -Wextra -Wpedantic)

# ── Static linking ────────────────────────────────────────────────────────────
# Default ON – produces a self-contained binary that runs without any shared
# library dependencies (copy-and-run on any compatible Qualcomm Linux device).
#
# Disable with:  cmake -DSTATIC_BUILD=OFF -B build
#
option(STATIC_BUILD "Link qctop as a fully static executable" ON)

if(STATIC_BUILD)
    message(STATUS "Static build ENABLED  (-static)")
    # -static must come before all library flags
    target_link_options(qctop BEFORE PRIVATE -static)
    # Prefer static variants for all libraries we pull in
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".so")
else()
    message(STATUS "Static build DISABLED (shared libraries)")
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS qctop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
