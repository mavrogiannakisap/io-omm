cmake_minimum_required(VERSION 3.15)
project(FileORAM LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})

if (NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
    message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
    file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/0.18.1/conan.cmake"
            "${CMAKE_BINARY_DIR}/conan.cmake"
            TLS_VERIFY ON)
endif ()

include(${CMAKE_BINARY_DIR}/conan.cmake)

conan_cmake_configure(REQUIRES
        cxxopts/3.0.0
        grpc/1.48.0
        openssl/3.0.5
        protobuf/3.21.4
        GENERATORS cmake_find_package_multi)

conan_cmake_autodetect(settings BUILD_TYPE ${CMAKE_BUILD_TYPE})
conan_cmake_install(PATH_OR_REFERENCE .
        BUILD missing
        REMOTE conancenter
        SETTINGS ${settings})

#if (${CMAKE_BUILD_TYPE} EQUAL Debug)
#    link_libraries(profiler)
#
#   SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
#   SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
#   SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -pg")
#link_libraries(c_p)
#endif ()

find_package(cxxopts REQUIRED CONFIG)
find_package(gRPC REQUIRED CONFIG)
find_package(Protobuf REQUIRED CONFIG)
find_package(OpenSSL REQUIRED CONFIG)

link_libraries(cxxopts::cxxopts)

# Source: https://github.com/buq2/grpc-conan; + cmake_find_package_multi support for multiple build types.
include(cmake/grpc_extras.cmake)

include_directories(${PROJECT_SOURCE_DIR}/src)

add_subdirectory(src)
