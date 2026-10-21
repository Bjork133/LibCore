cmake_minimum_required(VERSION 3.20)
project(libcore VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -flto -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE   "-O2 -flto -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto -s")

include(GNUInstallDirs)
find_package(Threads REQUIRED)

# --- SQLite ---
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DQS=0
    SQLITE_THREADSAFE=1
)
target_compile_options(sqlite3 PRIVATE -w)

# --- nlohmann/json ---
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party/json)

# --- scrypt (enceeper, только примитивы) ---
add_library(scrypt STATIC
    third_party/scrypt/lib/scrypt_lib.c
    third_party/scrypt/lib/pbkdf2.c
    third_party/scrypt/lib/hmac_sha2.c
    third_party/scrypt/lib/sha2.c
    third_party/scrypt/lib/util.c
)
target_include_directories(scrypt PUBLIC third_party/scrypt)
target_compile_options(scrypt PRIVATE -w -fno-strict-aliasing)

# --- cpp-httplib ---
add_library(httplib INTERFACE)
target_include_directories(httplib INTERFACE third_party/httplib)

# --- Источники (C + C++) ---
file(GLOB_RECURSE LIBCORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.c"
)

add_executable(libcore ${LIBCORE_SOURCES})

target_include_directories(libcore PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(libcore PRIVATE
    sqlite3
    nlohmann_json
    scrypt
    httplib
    Threads::Threads
)

# --- Вшивание хелпов в бинарник ---
set(HELP_GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${HELP_GEN_DIR}")

file(GLOB HELP_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/help/*.txt")

set(HELP_CPP "${HELP_GEN_DIR}/embedded_help.cpp")
set(HELP_HPP "${HELP_GEN_DIR}/embedded_help.hpp")

file(WRITE "${HELP_HPP}"
"#pragma once\n"
"#include <string_view>\n"
"#include <span>\n"
"\n"
"namespace libcore::help {\n"
"struct Entry { std::string_view name; std::string_view text; };\n"
"std::span<const Entry> embedded();\n"
"}\n"
)

file(WRITE "${HELP_CPP}"
"#include \"embedded_help.hpp\"\n"
"\n"
"namespace libcore::help {\n"
"namespace {\n"
)

foreach(f ${HELP_FILES})
    get_filename_component(fname "${f}" NAME_WE)
    string(TOLOWER "${fname}" fname_lc)
    string(REPLACE "-" "_" fname_lc "${fname_lc}")
    file(READ "${f}" content)
    string(REPLACE "\\" "\\\\" content "${content}")
    string(REPLACE "\"" "\\\"" content "${content}")
    string(REPLACE "\n" "\\n\"\n\"" content "${content}")
    string(REPLACE "\r" "" content "${content}")
    file(APPEND "${HELP_CPP}" "constexpr std::string_view h_${fname_lc} = \"${content}\";\n")
endforeach()

file(APPEND "${HELP_CPP}" "constexpr Entry entries[] = {\n")
foreach(f ${HELP_FILES})
    get_filename_component(fname "${f}" NAME_WE)
    string(TOLOWER "${fname}" fname_lc)
    string(REPLACE "-" "_" fname_lc "${fname_lc}")
    file(APPEND "${HELP_CPP}" "    { \"${fname_lc}\", h_${fname_lc} },\n")
endforeach()
file(APPEND "${HELP_CPP}"
"};\n"
"}\n"
"std::span<const Entry> embedded() {\n"
"    return std::span<const Entry>(entries, sizeof(entries) / sizeof(Entry));\n"
"}\n"
"}\n"
)

target_sources(libcore PRIVATE "${HELP_CPP}")
target_include_directories(libcore PRIVATE "${HELP_GEN_DIR}")

# --- Compile-time defines ---
target_compile_definitions(libcore PRIVATE
    LIBCORE_HELP_DIR="${CMAKE_INSTALL_FULL_DATADIR}/libcore/help"
    LIBCORE_VERSION="${PROJECT_VERSION}"
)

# --- Install ---
install(TARGETS libcore RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY help/ DESTINATION ${CMAKE_INSTALL_DATADIR}/libcore/help)
