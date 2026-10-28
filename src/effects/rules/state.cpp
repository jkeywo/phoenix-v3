module;
#include <flecs.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

module phoenix.effects.rule_state;

namespace phoenix
{
void to_json(Json &out, const RuleExecutionState &value)
{
	out["key"] = value.key;
	out["definition"] = value.definition;
	out["binding"] = value.binding;
	out["source_key"] = value.source_key;
	out["owner_kind"] = value.owner_kind;
	out["context"] = value.context;
	out["applied_context"] = value.applied_context;
	out["generation"] = value.generation;
	out["started"] = value.started;
	out["next"] = value.next;
	out["last"] = value.last;
	out["lease_tick"] = value.lease_tick;
	out["lease_permille"] = value.lease_permille;
	out["stop_tick"] = value.stop_tick;
	out["eligible"] = value.eligible;
	out["active"] = value.active;
	out["stopped"] = value.stopped;
	out["credits"] = value.credits;
	out["output"] = value.output;
	out["blocker"] = value.blocker;
}
void from_json(const Json &in, RuleExecutionState &value)
{
	in.at("key").get_to(value.key);
	in.at("definition").get_to(value.definition);
	in.at("binding").get_to(value.binding);
	in.at("source_key").get_to(value.source_key);
	in.at("owner_kind").get_to(value.owner_kind);
	in.at("context").get_to(value.context);
	in.at("applied_context").get_to(value.applied_context);
	in.at("generation").get_to(value.generation);
	in.at("started").get_to(value.started);
	in.at("next").get_to(value.next);
	in.at("last").get_to(value.last);
	in.at("lease_tick").get_to(value.lease_tick);
	in.at("lease_permille").get_to(value.lease_permille);
	in.at("stop_tick").get_to(value.stop_tick);
	in.at("eligible").get_to(value.eligible);
	in.at("active").get_to(value.active);
	in.at("stopped").get_to(value.stopped);
	in.at("credits").get_to(value.credits);
	in.at("output").get_to(value.output);
	in.at("blocker").get_to(value.blocker);
}

void to_json(Json &out, const RuleRuntime &value)
{
	out["next_generation"] = value.next_generation;
	out["executions"] = value.executions;
	out["regions"] = value.regions;
	out["membership_initialised"] = value.membership_initialised;
}
void from_json(const Json &in, RuleRuntime &value)
{
	in.at("next_generation").get_to(value.next_generation);
	in.at("executions").get_to(value.executions);
	in.at("regions").get_to(value.regions);
	in.at("membership_initialised").get_to(value.membership_initialised);
}

void to_json(Json &out, const ActiveRuleEffect &value)
{
	out["id"] = value.id;
	out["source"] = value.source;
	out["definition"] = value.definition;
	out["group_source"] = value.group_source;
	out["context"] = value.context;
	out["origin"] = value.origin;
	out["expiry"] = value.expiry;
	out["clocks"] = value.clocks;
}
void from_json(const Json &in, ActiveRuleEffect &value)
{
	in.at("id").get_to(value.id);
	in.at("source").get_to(value.source);
	in.at("definition").get_to(value.definition);
	in.at("group_source").get_to(value.group_source);
	in.at("context").get_to(value.context);
	in.at("origin").get_to(value.origin);
	in.at("expiry").get_to(value.expiry);
	in.at("clocks").get_to(value.clocks);
}

void to_json(Json &out, const PendingRuleExecution &value)
{
	out["id"] = value.id;
	out["due"] = value.due;
	out["phase"] = value.phase;
	out["rule"] = value.rule;
	out["source_key"] = value.source_key;
	out["damage"] = value.damage;
	out["context"] = value.context;
}
void from_json(const Json &in, PendingRuleExecution &value)
{
	in.at("id").get_to(value.id);
	in.at("due").get_to(value.due);
	in.at("phase").get_to(value.phase);
	in.at("rule").get_to(value.rule);
	in.at("source_key").get_to(value.source_key);
	in.at("damage").get_to(value.damage);
	in.at("context").get_to(value.context);
}

} // namespace phoenix
