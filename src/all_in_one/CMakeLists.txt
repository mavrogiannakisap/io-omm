add_executable(pathoram_allinone pathoram.cc
        ../remote_store/server.cc
        ../path_oram/path_oram.cc
        ../path_omap/path_omap.cc) # The rel paths are a hack. TODO: fix.
target_include_directories(pathoram_allinone PUBLIC "${_gRPC_PROTO_GENS_DIR}")
target_link_libraries(pathoram_allinone gRPC::grpc++_unsecure OpenSSL::SSL OpenSSL::Crypto proto)
