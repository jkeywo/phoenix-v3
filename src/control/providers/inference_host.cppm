module;
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>

export module phoenix.control.inference_host;
export import phoenix.simulation;
export import phoenix.inference;

export namespace phoenix
{
// Host-local request scheduling shared by local and network-authoritative hosts.
// Every proposed result still goes through the supplied normal commit path.
class InferenceHost
{
public:
	InferenceHost();
	~InferenceHost();
	void set_connection(std::shared_ptr<InferenceConnection> connection);
	Json status() const;
	void reset();
	void poll(World &world, EntityId ship, const std::function<void(const Json &)> &commit);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace phoenix
