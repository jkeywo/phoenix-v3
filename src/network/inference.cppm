module;
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.inference;
export import phoenix.transport;
export import phoenix.data;

export namespace phoenix
{
// Host-local connection, deliberately separate from replicated Controller state.
class InferenceConnection
{
public:
	InferenceConnection(std::string token, std::string service);
	~InferenceConnection();
	Json status() const;
	void reset_requests();
	void poll();
	bool busy() const;
	bool request(const Json &snapshot);
	std::vector<Json> completed();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace phoenix
