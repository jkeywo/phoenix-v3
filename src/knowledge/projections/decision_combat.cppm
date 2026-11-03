module;
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <tuple>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.knowledge.decision_combat;
export import phoenix.contracts;
export import phoenix.knowledge;
export namespace phoenix
{
struct CombatCompartmentDecision
{
	std::string id{};
	std::int64_t index{};
	std::string deck{};
	std::int64_t hp{};
	std::int64_t maximum{};
	bool disabled{};
	bool inspected{};
	bool damaged{};
	std::int64_t x_mm{};
	std::int64_t y_mm{};
	std::int64_t half_width_mm{};
	std::int64_t half_length_mm{};
	bool reported_inspection{};
	std::uint64_t inspection_tick{};
	std::uint64_t inspection_revision{};
	std::int64_t inspection_fault_count{};
	std::int64_t inspection_hp{};
	std::int64_t inspection_maximum{};
};
struct CombatTeamDecision
{
	std::string id{};
	bool assignment_manual{};
	bool assignment_held{};
	std::int64_t home{};
	std::uint64_t observed_tick{};
	bool local_complete{};
	std::int64_t location{};
	std::int64_t destination{};
	std::int64_t travel{};
	std::int64_t edge_to{};
	std::int64_t edge_total{};
};
struct CombatArcDecision
{
	std::string id{};
	std::string kind{};
	std::int64_t bearing_mdeg{};
	std::int64_t rest_mdeg{};
	std::int64_t arc_mdeg{};
	bool coverage{};
	std::string label{};
	std::string family{};
	bool active{};
	bool disabled{};
};
struct CombatFaultDecision
{
	std::string system{};
	std::string compartment{};
	std::int64_t compartment_index{};
	std::string symptom{};
	bool diagnosed{};
	std::string diagnosis{};
	bool destroyed{};
	std::int64_t work{};
	std::int64_t required_work{};
	std::uint64_t observed_tick{};
	std::uint64_t revision{};
	std::string source{};
	std::int64_t uncertainty{};
	std::uint64_t expires_tick{};
	bool evidence_present = false;
};
struct CombatMagazineDecision
{
	std::string id{};
	std::string selected_resource{};
	std::vector<std::string> resource_choices{};
	std::map<std::string, std::string> resource_labels{};
	std::vector<std::string> selectable_resources{};
	std::uint64_t stock_revision{};
	bool manual{};
};
struct CombatAmmunitionDecision
{
	std::int64_t index{};
	std::string id{};
	std::int64_t range_mm{};
	std::string guidance{};
	std::int64_t quantity{};
	std::int64_t use{};
	bool available{};
};
struct CombatWeaponDecision
{
	std::string id{};
	bool enabled{};
	bool durable_payload{};
	bool ammunition_free{};
	std::string label{};
	std::vector<std::string> magazines{};
	std::int64_t selected{};
	std::int64_t shots{};
	std::int64_t loaded{};
	std::string loaded_resource{};
	std::int64_t loaded_quantity{};
	std::int64_t loading{};
	std::int64_t unloading{};
	std::int64_t load_ticks{};
	std::int64_t unload_ticks{};
	std::int64_t mode{};
	bool point_defence{};
	std::uint64_t intercept{};
	std::vector<CombatAmmunitionDecision> ammunition_options{};
};
struct CombatShieldDecision
{
	std::string id{};
	std::string contribution{};
	std::uint64_t effect_id{};
	std::string component_id{};
	bool controllable{};
	std::int64_t strength{};
	std::int64_t capacity{};
	std::int64_t warmup{};
	std::int64_t warmup_ticks{};
	std::int64_t arc_mdeg{};
	std::int64_t bearing_mdeg{};
	std::int64_t layer{};
	bool discharging{};
};
struct InspectionPatientDecision
{
	std::string id{};
	std::string condition{};
	std::int64_t location{};
	std::string facility{};
	std::string compartment{};
	std::string name{};
	bool stabilised{};
	std::string escort{};
	std::uint64_t observed_tick{};
	std::uint64_t revision{};
	std::string source{};
	std::int64_t uncertainty{};
	std::uint64_t expires_tick{};
};
struct SensorProgressDecision
{
	std::string capability{};
	std::int64_t ticks{};
	std::int64_t required{};
};
struct SensorScannerDecision
{
	std::string id{};
	EntityId target{};
	bool powered{};
	std::map<std::string, std::int64_t> capabilities{};
	std::int64_t bearing_mdeg{};
	std::int64_t arc_mdeg{};
	bool radar_arc{};
	std::int64_t range_mm{};
	std::int64_t x{};
	std::int64_t y{};
	std::string blocker{};
	std::vector<SensorProgressDecision> progress{};
};
struct SensorTextFindingDecision
{
	std::string text;
	std::uint64_t observed = 0;
};
struct SensorIdentityDecision
{
	std::string classification, faction, faction_label, disposition;
};
struct SensorFindingDecision
{
	std::optional<SensorIdentityDecision> identity;
	std::uint64_t observed{};
	bool live{};
	bool data_present{};
	std::string classification{};
	std::vector<SensorTextFindingDecision> report_findings;
	std::uint64_t revision{};
	std::string source{};
};
struct SensorContactDecision
{
	EntityId id{};
	std::map<std::string, SensorFindingDecision> findings{};
};
struct CombatDecisionSnapshot
{
	bool present = false, destroyed = false;
	std::vector<CombatCompartmentDecision> compartments;
	std::vector<CombatTeamDecision> teams;
	std::vector<CombatArcDecision> arcs;
	std::vector<CombatFaultDecision> faults;
	std::vector<CombatShieldDecision> shields;
	std::vector<CombatWeaponDecision> weapons;
	std::vector<CombatMagazineDecision> magazines;
	std::optional<std::string> result;
	std::map<std::string, std::int64_t> directed_targets;
};
struct SensorDecisionSnapshot
{
	std::vector<SensorScannerDecision> scanners;
	std::vector<SensorContactDecision> contacts;
};
struct ContactReportDecision
{
	BodyView body;
	KnowledgeTick identity_tick = 0;
	bool valid = false;
};
struct InspectionReportDecision
{
	bool valid = false;
	EntityId ship = 0;
	std::string compartment;
	bool medical = false, repair = false;
	std::int64_t hp = 0, maximum = 0;
	std::vector<CombatFaultDecision> faults;
	std::vector<InspectionPatientDecision> patients;
};
struct SensorReportDecision
{
	bool valid = false;
	std::string capability;
	std::map<std::string, SensorFindingDecision> findings;
	// Dialogue accepts reported string tags independently of scanner detail validity.
	std::vector<std::string> finding_tags;
};
using DecisionEvidencePayload =
    std::variant<std::monostate, ContactReportDecision, InspectionReportDecision, SensorReportDecision>;
using DecisionEvidenceKey = std::tuple<KnowledgeId, std::uint64_t, KnowledgeScope, KnowledgeId, KnowledgeId,
                                       KnowledgeId, KnowledgeKey, KnowledgeKey, KnowledgeId, KnowledgeId, KnowledgeTick,
                                       std::uint64_t, std::int64_t, EvidenceKind, bool, KnowledgeTick, KnowledgeTick>;
DecisionEvidenceKey decision_evidence_key(const KnowledgeRecord &);
using DecisionEvidenceCache = std::map<DecisionEvidenceKey, std::shared_ptr<const DecisionEvidencePayload>>;
// Wire/schema decoding is performed only when an immutable record is admitted.
DecisionEvidencePayload decode_decision_evidence(const KnowledgeRecordInput &, const CompiledKnowledge &);
SensorFindingDecision sensor_finding_decision(const Json &, std::uint64_t observed);
Json combat_decision_json(const CombatDecisionSnapshot &);
// Explicit supplied projection/provider decoding, never ordinary tick capture.
CombatDecisionSnapshot combat_decision_from_json(const Json &);
SensorDecisionSnapshot sensor_decision_from_json(const Json &);
} // namespace phoenix
