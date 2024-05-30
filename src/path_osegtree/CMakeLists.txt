add_executable(pathosegtree_client client.cc path_osegtree.cc ../path_oram/path_oram.cc) # The rel path is a hack. TODO: fix.
target_include_directories(pathosegtree_client PUBLIC "${_gRPC_PROTO_GENS_DIR}")
target_link_libraries(pathosegtree_client gRPC::grpc++_unsecure OpenSSL::SSL OpenSSL::Crypto proto)
