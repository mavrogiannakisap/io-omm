add_executable(remote_store_server runserver.cc server.cc async_server.cc)
target_include_directories(remote_store_server PUBLIC "${_gRPC_PROTO_GENS_DIR}")
target_link_libraries(remote_store_server gRPC::grpc++_unsecure proto OpenSSL::SSL OpenSSL::Crypto)

add_executable(remote_store_client client.cc)
target_include_directories(remote_store_client PUBLIC "${_gRPC_PROTO_GENS_DIR}")
target_link_libraries(remote_store_client gRPC::grpc++_unsecure proto)

add_executable(test_store test_store.cc)