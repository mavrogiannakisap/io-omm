add_executable(ofilestore_client client.cc o_file_store.cc
        ../path_omap/path_omap.cc
        ../path_oram/path_oram.cc
        ../path_osegtree/path_osegtree.cc) # The rel paths are a hack. TODO: fix.
target_include_directories(ofilestore_client PUBLIC "${_gRPC_PROTO_GENS_DIR}")
target_link_libraries(ofilestore_client gRPC::grpc++_unsecure OpenSSL::SSL OpenSSL::Crypto proto)
