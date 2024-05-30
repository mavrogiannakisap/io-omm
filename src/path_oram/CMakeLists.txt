add_executable(pathoram_client client.cc path_oram.cc)
target_include_directories(pathoram_client PUBLIC "${_gRPC_PROTO_GENS_DIR}")
target_link_libraries(pathoram_client gRPC::grpc++_unsecure OpenSSL::SSL OpenSSL::Crypto proto)
