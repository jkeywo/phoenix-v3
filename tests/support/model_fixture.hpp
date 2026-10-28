#pragma once
#include <cstring>
#include <cstdint>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.data;
namespace phoenix::test
{
// A static triangle with a translated/scaled parent and an inactive scene node.
inline Bytes model_fixture(Json patch = Json::object())
{
	Json document = {
	    {"asset", {{"version", "2.0"}}},
	    {"scene", 0},
	    {"scenes", Json::array({{{"nodes", {0}}}, {{"nodes", {2}}}})},
	    {"nodes", Json::array({{{"translation", {1, 2, 3}}, {"scale", {2, 1, 1}}, {"children", {1}}},
	                           {{"mesh", 0}},
	                           {{"mesh", 0}, {"translation", {50, 0, 0}}}})},
	    {"meshes", Json::array({{{"primitives", Json::array({{{"attributes", {{"POSITION", 0}}}, {"mode", 4}}})}}})},
	    {"buffers", Json::array({{{"byteLength", 36}}})},
	    {"bufferViews", Json::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}}})},
	    {"accessors", Json::array({{{"bufferView", 0},
	                                {"componentType", 5126},
	                                {"count", 3},
	                                {"type", "VEC3"},
	                                {"min", {0, 0, 0}},
	                                {"max", {1, 1, 0}}}})}};
	document.update(patch);
	auto json = document.dump();
	while (json.size() % 4)
		json += ' ';
	Bytes result;
	auto word = [&](std::uint32_t value)
	{
		for (unsigned shift = 0; shift < 32; shift += 8)
			result.push_back(static_cast<std::uint8_t>(value >> shift));
	};
	word(0x46546c67);
	word(2);
	word(static_cast<std::uint32_t>(28 + json.size() + 36));
	word(static_cast<std::uint32_t>(json.size()));
	word(0x4e4f534a);
	result.insert(result.end(), json.begin(), json.end());
	word(36);
	word(0x004e4942);
	for (float value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f})
	{
		std::uint32_t bits;
		std::memcpy(&bits, &value, 4);
		word(bits);
	}
	return result;
}
} // namespace phoenix::test
