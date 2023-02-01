target_sources(splatoon_server PRIVATE
        certManager.hpp
        certManager.cpp)

if (WIN32)
    target_sources(splatoon_server PRIVATE ${OPENSSL_INCLUDE_DIR}/openssl/applink.c) # Allow file writing of OpenSSL
endif()