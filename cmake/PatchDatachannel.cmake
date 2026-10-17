# libdatachannel 0.24.6 disables all Windows WSS certificate verification.
# Our Mbed TLS backend receives an explicit Windows-root/CA bundle and supports
# verified TLS on Windows. Keep the legacy bypass only for other TLS backends.
set(path "${SOURCE_DIR}/src/impl/websocket.cpp")
file(READ "${path}" original)
string(REPLACE "\r\n" "\n" source "${original}")
set(before [=[#ifdef _WIN32
		if (std::exchange(verify, false)) {
			PLOG_WARNING << "TLS certificate verification with root CA is not supported on Windows";
		}
#endif]=])
set(after [=[#if defined(_WIN32) && !USE_MBEDTLS
		if (std::exchange(verify, false)) {
			PLOG_WARNING << "TLS certificate verification with root CA is not supported on Windows";
		}
#endif]=])
string(FIND "${source}" "${before}" unpatched)
string(FIND "${source}" "${after}" patched)
if(NOT unpatched EQUAL -1)
  string(REPLACE "${before}" "${after}" source "${source}")
  file(WRITE "${path}" "${source}")
elseif(patched EQUAL -1)
  message(FATAL_ERROR "Pinned libdatachannel Windows TLS patch no longer matches; review verification before building")
endif()
