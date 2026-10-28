#include "../support/assertions.hpp"
#include "../../src/graphics/audio/native_backend.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

int main()
{
	try
	{
		using phoenix::audio_backend::Outputs;
		using phoenix::test::require;
		Outputs outputs({{"fixture:a", "Speakers A"}, {"fixture:b", "Speakers B"}});
		require(outputs.enumerate().size() == 2, "Synthetic endpoints use the production native audio backend");
		std::ifstream wav(std::string(PHOENIX_FIXTURE_DIR) + "/audio-tone.wav", std::ios::binary);
		const std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(wav), {});
		require(!bytes.empty(), "The synthetic WAV fixture is available");
		auto host = outputs.load("default", bytes);
		auto console = outputs.load("fixture:b", bytes);
		auto shared = outputs.load("fixture:a", bytes);
		require(host.valid() && console.valid() && shared.valid(),
		        "Separate outputs decode through production native audio");
		require(host.shares_output(shared) && !host.shares_output(console),
		        "System default and an explicit choice share one engine for the same endpoint");
		require(!outputs.load("missing:speakers", bytes).valid() && !outputs.load("muted", bytes).valid(),
		        "Missing and muted outputs cannot fall back to another endpoint");
		host.start(true, .5f);
		console.start(true, .5f);
		require(host.playing() && console.playing(), "Two output engines play concurrently");
		host.stop();
		require(!host.playing() && console.playing(), "Stopping one output leaves the other screen playing");
		std::ifstream ogg(std::string(PHOENIX_FIXTURE_DIR) + "/audio-tone.ogg", std::ios::binary);
		auto loop = outputs.load("fixture:b", {std::istreambuf_iterator<char>(ogg), {}});
		require(loop.valid(), "Native streaming retains the pinned Vorbis decoder");
		loop.start(true, .2f);
		require(loop.playing(), "Vorbis streams play through the native engine");
		std::cout << "Native screen audio routing passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
