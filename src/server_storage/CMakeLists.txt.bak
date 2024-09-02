cmake_minimum_required(VERSION 3.15)
project(Storage LANGUAGES CXX)
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


find_package(cxxopts REQUIRED CONFIG)

link_libraries(cxxopts::cxxopts)

add_executable(storage_client local_server.cc client.cc) # The rel path is a hack. TODO: fix.
# target_include_directories(pathomap_client PUBLIC "${_gRPC_PROTO_GENS_DIR}")
# target_link_libraries(pathomap_client gRPC::grpc++_unsecure OpenSSL::SSL OpenSSL::Crypto proto)
