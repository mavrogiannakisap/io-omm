protobuf_generate_grpc_cpp(GRPC_SRCS GRPC_HDRS remote_store.proto)

link_libraries(gRPC::grpc++_unsecure)

add_library(proto "${GRPC_HDRS}" "${GRPC_SRCS}")

