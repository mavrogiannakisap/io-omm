add_executable(pathosm_client client.cc path_osm.cc ../path_oram/path_oram.cc) # The rel path is a hack. TODO: fix.
target_include_directories(pathosm_client PUBLIC "${_gRPC_PROTO_GENS_DIR}")
target_link_libraries(pathosm_client gRPC::grpc++_unsecure OpenSSL::SSL OpenSSL::Crypto proto)
