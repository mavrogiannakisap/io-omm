add_executable(pathomap_client client.cc path_omap.cc ../path_oram/path_oram.cc) # The rel path is a hack. TODO: fix.
target_include_directories(pathomap_client PUBLIC "${_gRPC_PROTO_GENS_DIR}")
target_link_libraries(pathomap_client gRPC::grpc++_unsecure OpenSSL::SSL OpenSSL::Crypto proto)
