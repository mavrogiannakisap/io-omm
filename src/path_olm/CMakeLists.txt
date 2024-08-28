add_executable(patholm_client client.cc path_olm.cc ../path_oram/path_oram.cc
       ../remote_store/async_server.cc
        ../remote_store/server.cc
    ) # The rel path is a hack. TODO: fix.
target_include_directories(patholm_client PUBLIC "${_gRPC_PROTO_GENS_DIR}")
target_link_libraries(patholm_client gRPC::grpc++_unsecure OpenSSL::SSL OpenSSL::Crypto proto)
