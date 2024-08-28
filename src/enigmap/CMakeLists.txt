add_executable(enigmap_client client.cc enigmap.cc
    ../path_osm/path_osm.cc
    ../path_omap/path_omap.cc
    ../path_oram/path_oram.cc
    ../remote_store/async_server.cc
    ../remote_store/server.cc
) # The rel path is a hack. TODO: fix.
target_include_directories(enigmap_client PUBLIC "${_gRPC_PROTO_GENS_DIR}")
target_link_libraries(enigmap_client gRPC::grpc++_unsecure OpenSSL::SSL OpenSSL::Crypto proto)
