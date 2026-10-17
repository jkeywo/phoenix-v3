# Native WebRTC + WebSocket support. Submodule revisions are pinned by each
# immutable parent commit; browser builds use the platform's WebRTC/WebSocket APIs.
if(NOT EMSCRIPTEN)
  set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
  set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG 068ff080b369adfac81509f9b57b2afabaf82dc5
    GIT_SUBMODULES framework
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(mbedtls)
  target_compile_definitions(mbedtls PUBLIC MBEDTLS_SSL_DTLS_SRTP)
  add_library(MbedTLS::MbedTLS INTERFACE IMPORTED GLOBAL)
  set_target_properties(MbedTLS::MbedTLS PROPERTIES INTERFACE_LINK_LIBRARIES "mbedtls;mbedx509;mbedcrypto")
  set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
  set(NO_MEDIA ON CACHE BOOL "" FORCE)
  set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
  set(NO_TESTS ON CACHE BOOL "" FORCE)
  set(NO_WEBSOCKET OFF CACHE BOOL "" FORCE)
  set(RTC_UPDATE_VERSION_HEADER OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(datachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG 6b1e2e620f1e37f0eafeee702eaea0043cb305fd
    GIT_SUBMODULES deps/plog deps/usrsctp deps/libjuice
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(datachannel)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${datachannel_SOURCE_DIR}" -P "${CMAKE_CURRENT_LIST_DIR}/PatchDatachannel.cmake" COMMAND_ERROR_IS_FATAL ANY)
endif()
