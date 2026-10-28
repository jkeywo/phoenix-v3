module;
#include <cstdint>
#include <string>
export module phoenix.gameplay.sensors.acquisition;
export import phoenix.gameplay.sensors.state;
export import phoenix.content.sensing.types;
export namespace phoenix
{
// The physical adapter supplies native payloads and routes earned publications.
class SensorAcquisitionPort
{
public:
	virtual ~SensorAcquisitionPort();
	virtual Json read_capability(const std::string &capability) const = 0;
	virtual void publish_capability(const std::string &capability, const Json &data, std::uint64_t tick) = 0;
};
struct SensorAcquisitionInput
{
	const BodyView &body;
	const SensorDefinition &active, &passive;
	const std::string &mode;
	std::uint64_t tick;
	bool apparent = false, timed = false;
};
SensorTrack *observe_sensor_contact(SensorRuntime &state, const BodyView &body, bool apparent, std::uint64_t tick);
void acquire_sensor_capabilities(SensorRuntime &state, SensorTrack &track, const SensorAcquisitionInput &input,
                                 SensorAcquisitionPort &port);
Json sensor_track_findings(const SensorTrack &track);
} // namespace phoenix
