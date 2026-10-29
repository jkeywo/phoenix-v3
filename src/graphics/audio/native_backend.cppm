module;
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

export module phoenix.graphics.audio_backend;

export namespace phoenix::audio_backend
{
class Outputs;
class Voice
{
public:
	void start(bool loop, float gain, float pitch = 1);
	void stop();
	void gain(float);
	bool playing() const;
	// Clip initialization succeeded; its output may later become unavailable.
	bool loaded() const;
	bool valid() const;
	bool shares_output(const Voice &) const;

private:
	struct Data;
	std::shared_ptr<Data> data;
	friend class Outputs;
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
