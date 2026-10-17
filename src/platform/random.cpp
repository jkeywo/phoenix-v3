module;
#include <array>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif
#include <string>
#include <stdexcept>

module phoenix.data;

#ifdef __EMSCRIPTEN__
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(bool, phoenix_secure_random, (unsigned char* destination), {
    try { const bytes=new Uint8Array(32); crypto.getRandomValues(bytes); HEAPU8.set(bytes,destination); return true; } catch { return false; }
});
// clang-format on
#endif
namespace phoenix
{
std::string random_token()
{
	std::array<unsigned char, 32> bytes{};
#ifdef __EMSCRIPTEN__
	if (!phoenix_secure_random(bytes.data()))
		throw std::runtime_error("Secure browser randomness is unavailable");
#else
	if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
		throw std::runtime_error("Secure native randomness is unavailable");
#endif
	std::string result;
	const char *hex = "0123456789abcdef";
	for (auto byte : bytes)
	{
		result += hex[byte >> 4];
		result += hex[byte & 15];
	}
	return result;
}
} // namespace phoenix
