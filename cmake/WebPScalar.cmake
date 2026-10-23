# libwebp 1.6.0 adds -mno-sse2 when its hand-written SIMD paths are disabled.
# x86-64 still needs SSE2 registers for the floating-point ABI (notably SharpYUV).
# Restore that ABI for every WebP target, including its object libraries. The
# generated config.h still leaves WEBP_HAVE_* undefined, keeping codec SIMD off.
if(
	NOT EMSCRIPTEN
	AND CMAKE_SIZEOF_VOID_P EQUAL 8
	AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$"
	AND CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$"
)
	get_property(phoenix_webp_targets DIRECTORY "${webp_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
	foreach(phoenix_webp_target IN LISTS phoenix_webp_targets)
		target_compile_options(${phoenix_webp_target} PRIVATE -msse2)
	endforeach()
endif()
