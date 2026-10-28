module;
#include <cstdint>
#include <span>
#include <map>
#include <string>
#include <tuple>
export module phoenix.gameplay.psychic.interference;
export import phoenix.gameplay.psychic.contributions;
export namespace phoenix
{
using PsychicSource = std::tuple<EntityId, std::string, std::string>;
struct PsychicInterferenceSource
{
	EntityId ship;
	const std::string *grant;
	const ChoirDefinition *choir;
	const PsychicContribution *contribution;
	const AbilityGrantState *execution;
};
class PsychicInterferencePort
{
public:
	virtual ~PsychicInterferencePort();
	virtual bool is_ward(const PsychicInterferenceSource &source) const = 0;
	virtual bool covers(const PsychicInterferenceSource &source, EntityId receiver) const = 0;
};
// Sources retain fitted apparatus/grant order; wards win equal-strength ties.
PsychicSource select_psychic_interference(EntityId receiver, std::span<const PsychicInterferenceSource> sources,
                                          const PsychicInterferencePort &port);

// Derived receiver caches are rebuilt in explicit native phases and never saved.
class PsychicInterferenceMemory
{
public:
	void begin_reception();
	void observe(EntityId receiver, std::span<const PsychicInterferenceSource> sources,
	             const PsychicInterferencePort &port);
	PsychicSource strongest(EntityId receiver) const;
	void begin_concealment();
	void set_concealment(EntityId ship, std::int64_t strength);
	std::int64_t concealment(EntityId ship) const;

private:
	std::map<EntityId, PsychicSource> winners_;
	std::map<EntityId, std::int64_t> concealment_;
};
} // namespace phoenix
