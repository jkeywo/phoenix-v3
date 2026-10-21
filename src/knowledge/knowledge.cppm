module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module phoenix.knowledge;
export import phoenix.data;

export namespace phoenix
{
using KnowledgeId = std::uint64_t;
using KnowledgeKey = std::uint32_t;
using KnowledgeTick = std::uint64_t;
using KnowledgeValue = std::variant<std::int64_t, bool, std::string>;
enum class KnowledgeType
{
	Integer,
	Boolean,
	Text
};
enum class EvidenceKind
{
	Fact,
	Claim,
	Intent,
	Prediction
};
enum class KnowledgeScope
{
	Console,
	Ship
};
enum class ControlOrigin
{
	Human,
	AI,
	Scenario,
	Reporter
};
enum class MessageDestinationKind
{
	System,
	Console,
	Station,
	Ship,
	Controller
};
struct KnowledgeField
{
	KnowledgeKey key = 0;
	std::string name;
	KnowledgeType type = KnowledgeType::Integer;
	bool required = true;
	bool publishable = false;
};
struct KnowledgeSchema
{
	KnowledgeKey key = 0;
	std::string name;
	std::vector<KnowledgeField> fields;
	std::vector<KnowledgeKey> producers, readers;
	bool ship_publishable = false;
	KnowledgeTick stale_after = 0;
};
struct CompiledKnowledge
{
	std::vector<std::string> roles;
	std::vector<KnowledgeSchema> schemas;
	std::size_t history_limit = 256;
	KnowledgeKey role(std::string_view name) const;
	KnowledgeKey schema(std::string_view name) const;
	KnowledgeKey field(KnowledgeKey schema, std::string_view name) const;
};
// Authoring boundary. Throws an actionable diagnostic for malformed data.
CompiledKnowledge compile_knowledge(const Json &definition);
struct ConsoleKnowledgeBinding
{
	KnowledgeId id = 0, ship = 0, station = 0;
	std::vector<KnowledgeKey> reader_roles;
	bool human_controlled = false;
};
struct EvidenceProvenance
{
	KnowledgeId source = 0;
	KnowledgeTick observed_tick = 0;
	std::uint64_t revision = 0;
	std::int64_t uncertainty = 0;
	EvidenceKind kind = EvidenceKind::Fact;
};
struct KnowledgeRecordInput
{
	KnowledgeKey schema = 0, producer = 0;
	KnowledgeId subject = 0;
	EvidenceProvenance provenance;
	std::map<KnowledgeKey, KnowledgeValue> fields;
	bool withdrawn = false;
};
struct KnowledgeRecord
{
	KnowledgeId id = 0, owner = 0;
	KnowledgeScope scope = KnowledgeScope::Console;
	KnowledgeRecordInput value;
	KnowledgeTick received_tick = 0;
	KnowledgeId publication = 0; // Independent sharing stream; source provenance is preserved.
};
struct KnowledgePublication
{
	KnowledgeId id = 0, source_console = 0, subject = 0;
	KnowledgeKey schema = 0;
	std::vector<KnowledgeKey> fields;
	bool live = false, active = true;
	KnowledgeId target_console = 0; // Zero publishes shipwide; otherwise a scoped Console feed.
};
struct MessageDestination
{
	MessageDestinationKind kind = MessageDestinationKind::Console;
	KnowledgeId id = 0;
};
struct MessagePresentation
{
	std::string title_key, body_key;
	std::map<std::string, std::string> parameters;
	std::uint8_t urgency = 0;
	bool automatic = true;
	std::string speaker_name, speaker_portrait;
	Bytes speaker_avatar;
};
struct CoordinationInput
{
	KnowledgeId sender_system = 0;
	ControlOrigin origin = ControlOrigin::AI;
	MessageDestination destination;
	KnowledgeRecordInput payload;
	MessagePresentation presentation;
	KnowledgeTick delay = 1, expires_tick = 0;
	KnowledgeId request_key = 0;
	bool human_seeking = false;
};
struct CoordinationMessage
{
	KnowledgeId id = 0, sender_console = 0, sender_ship = 0;
	CoordinationInput value;
	KnowledgeTick sent_tick = 0, delivery_tick = 0;
	std::vector<KnowledgeId> recipients;
};
struct CoordinationDelivery
{
	KnowledgeId sequence = 0, console = 0;
	CoordinationMessage message;
	KnowledgeTick delivered_tick = 0;
	bool notify_human = false;
	bool actionable = true;  // False in retained history after expiry/replacement/withdrawal.
	bool offer_only = false; // Human observer history; never an extra AI inbox delivery.
	bool public_copy = false;
};
struct KnowledgePrivateMemory
{
	KnowledgeId console = 0, receive_cursor = 0;
	std::map<KnowledgeKey, KnowledgeValue> values;
};
struct KnowledgeStatusLatch
{
	bool initialised = false, reported = false, candidate = false;
	KnowledgeTick candidate_since = 0, last_report = 0;
	bool reported_once = false;
};
class KnowledgeRuntime
{
public:
	explicit KnowledgeRuntime(CompiledKnowledge schema = {});
	const CompiledKnowledge &definition() const;
	void register_console(ConsoleKnowledgeBinding binding);
	void register_system(KnowledgeId system, KnowledgeId console, bool human_controlled = false);
	void register_controller(KnowledgeId controller, KnowledgeId console, bool human_controlled = false);
	void register_ai(KnowledgeId ai, KnowledgeId console);
	void set_human_control(KnowledgeId console, bool human);
	void set_system_human_control(KnowledgeId system, bool human);
	// Returns false for an older/duplicate observation; invalid permissions throw.
	bool write_console(KnowledgeId console, KnowledgeRecordInput input, KnowledgeTick tick);
	// Explicit scenario/objective publisher; the caller supplies the permitted record.
	bool write_ship(KnowledgeId ship, KnowledgeRecordInput input, KnowledgeTick tick);
	// execute=false validates availability without mutation and returns zero.
	KnowledgeId publish(KnowledgeId console, KnowledgeKey schema, KnowledgeId subject, std::vector<KnowledgeKey> fields,
	                    bool live, KnowledgeTick tick, bool execute = true);
	KnowledgeId feed(KnowledgeId source_console, KnowledgeId target_console, KnowledgeKey schema, KnowledgeId subject,
	                 std::vector<KnowledgeKey> fields, KnowledgeTick tick);
	void stop_publication(KnowledgeId publication);
	std::vector<KnowledgeRecord> read_console(KnowledgeId console) const;
	std::vector<KnowledgeRecord> read_ship(KnowledgeId console) const;
	bool stale(const KnowledgeRecord &record, KnowledgeTick now) const;
	KnowledgePrivateMemory &private_memory(KnowledgeId ai);
	// execute=false validates availability without mutation and returns zero.
	KnowledgeId send(CoordinationInput input, KnowledgeTick tick, bool execute = true);
	bool withdraw_request(KnowledgeId sender_system, KnowledgeId request_key);
	std::vector<CoordinationDelivery> advance(KnowledgeTick tick);
	std::vector<CoordinationDelivery> message_history(KnowledgeId console) const;
	std::vector<CoordinationDelivery> receive(KnowledgeId ai);
	// Authoritative diagnostic snapshot only. At most 256 entries, ordered by due tick/id.
	std::vector<CoordinationMessage> pending_messages(std::size_t limit = 32) const;
	// Reports transitions only after a stable candidate and a minimum report gap.
	bool status_change(KnowledgeId producer, KnowledgeKey status, bool value, KnowledgeTick tick,
	                   KnowledgeTick debounce, KnowledgeTick recovery_hysteresis, KnowledgeTick minimum_gap = 0,
	                   bool report_initial = true);
	Json save() const;
	void restore(const Json &state);

private:
	CompiledKnowledge schema_;
	KnowledgeId next_id_ = 1, next_delivery_ = 1;
	KnowledgeTick current_tick_ = 0;
	std::map<KnowledgeId, ConsoleKnowledgeBinding> consoles_;
	std::map<KnowledgeId, KnowledgeId> systems_;
	std::set<KnowledgeId> controllers_;
	std::map<KnowledgeId, bool> system_human_;
	std::map<KnowledgeId, KnowledgePrivateMemory> memory_;
	std::vector<KnowledgeRecord> records_;
	std::vector<KnowledgePublication> publications_;
	std::vector<CoordinationMessage> pending_;
	std::vector<CoordinationDelivery> deliveries_;
	std::set<KnowledgeId> inactive_requests_;
	std::map<std::pair<KnowledgeId, KnowledgeKey>, KnowledgeStatusLatch> status_;
	const KnowledgeSchema &schema(KnowledgeKey key) const;
	void validate(const KnowledgeRecordInput &input, bool publication = false, bool subset = false) const;
	bool readable(KnowledgeId console, KnowledgeKey schema) const;
	bool write(KnowledgeScope scope, KnowledgeId owner, KnowledgeRecordInput input, KnowledgeTick tick,
	           KnowledgeId publication = 0);
	std::vector<KnowledgeId> resolve_recipients(KnowledgeId ship, const MessageDestination &destination,
	                                            KnowledgeKey schema) const;
	void trim();
	bool public_message_candidate(const CoordinationInput &input) const;
};
namespace knowledge_detail
{
void knowledge_require(bool condition, const std::string &reason);
KnowledgeId knowledge_unsigned(const Json &value);
bool knowledge_has(const std::vector<KnowledgeKey> &keys, KnowledgeKey key);
const KnowledgeSchema &knowledge_schema(const CompiledKnowledge &definition, KnowledgeKey key);
} // namespace knowledge_detail
} // namespace phoenix
