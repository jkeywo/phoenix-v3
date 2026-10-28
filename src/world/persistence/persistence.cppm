module;
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <string>
#include <utility>

export module phoenix.world.persistence;
export import phoenix.world.state;

export namespace phoenix
{
// These checks accept immutable checkpoint/content facts. Candidate construction,
// native restoration and the final successful swap remain World responsibilities.
void validate_checkpoint_envelope(const Json &, std::span<const std::pair<std::string, std::string>> packs);
EntityId checkpoint_next_entity(const Json &, EntityId largest_existing);
void validate_checkpoint_entity_authority(const Json &);
void validate_checkpoint_body(const SavedBody &, const std::string &definition, bool movable);
void validate_checkpoint_altitude(std::int64_t);
void validate_checkpoint_vertical(const VerticalMotion &);
class CheckpointRoster
{
public:
	CheckpointRoster(const Json &bodies, std::span<const EntityId> expected);
	void accept(EntityId);

private:
	std::set<EntityId> expected_, seen_;
};
} // namespace phoenix
