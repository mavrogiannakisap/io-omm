add_executable(crypto_test crypto_test.cc)
target_link_libraries(crypto_test OpenSSL::SSL OpenSSL::Crypto proto)
