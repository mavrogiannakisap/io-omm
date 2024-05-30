include_directories(PUBLIC "${_gRPC_PROTO_GENS_DIR}")
link_libraries(gRPC::grpc++_unsecure OpenSSL::SSL OpenSSL::Crypto proto)

add_executable(csv_headers csv_headers.cc)

add_executable(locality locality.cc)

add_executable(buildfiles build_store_files.cc
        ../o_file_store/o_file_store.cc
        ../path_osegtree/path_osegtree.cc
        ../path_omap/path_omap.cc
        ../path_oram/path_oram.cc
        ../remote_store/async_server.cc
        ../remote_store/server.cc
        ) # The rel paths are a hack. TODO: fix.

add_executable(sizes sizes.cc
        ../o_file_store/o_file_store.cc
        ../path_osegtree/path_osegtree.cc
        ../path_omap/path_omap.cc
        ../path_oram/path_oram.cc
        ../remote_store/async_server.cc
        ../remote_store/server.cc
        ) # The rel paths are a hack. TODO: fix.

add_executable(omap omap.cc
        ../o_file_store/o_file_store.cc
        ../path_osegtree/path_osegtree.cc
        ../path_omap/path_omap.cc
        ../path_oram/path_oram.cc
        ../remote_store/async_server.cc
        ../remote_store/server.cc
        ) # The rel paths are a hack. TODO: fix.

add_executable(append-ofs ofs-update.cc
       ../o_file_store/o_file_store.cc
       ../path_osegtree/path_osegtree.cc
       ../path_omap/path_omap.cc
       ../path_oram/path_oram.cc
       ../remote_store/async_server.cc
       ../remote_store/server.cc
       ) # The rel paths are a hack. TODO: fix.

add_executable(ofs ofs.cc
        ../o_file_store/o_file_store.cc
        ../path_osegtree/path_osegtree.cc
        ../path_omap/path_omap.cc
        ../path_oram/path_oram.cc
        ../remote_store/async_server.cc
        ../remote_store/server.cc
        ) # The rel paths are a hack. TODO: fix.

add_executable(osm osm.cc
        ../path_osm/path_osm.cc
        ../path_omap/path_omap.cc
        ../path_oram/path_oram.cc
        ../remote_store/async_server.cc
        ../remote_store/server.cc
        ) # The rel paths are a hack. TODO: fix.


