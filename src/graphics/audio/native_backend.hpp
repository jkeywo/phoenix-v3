#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace phoenix::audio_backend
{
class Voice
{
public:
	struct Data;
	std::shared_ptr<Data> data;
	void start(bool loop, float gain, float pitch = 1);
	void stop();
	void gain(float);
	bool playing() const;
	bool valid() const;
	bool shares_output(const Voice &) const;
};
class Outputs
{
public:
	// A nonempty synthetic inventory selects miniaudio's null device backend.
	explicit Outputs(std::vector<std::pair<std::string, std::string>> synthetic = {});
	~Outputs();
	std::vector<std::pair<std::string, std::string>> enumerate();
	std::string resolve(const std::string &) const;
	Voice load(const std::string &output, const std::vector<std::uint8_t> &bytes);
	void retain(const std::vector<std::string> &outputs);

private:
	struct State;
	std::unique_ptr<State> state;
};
} // namespace phoenix::audio_backend
