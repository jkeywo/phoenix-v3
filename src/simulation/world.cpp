#include "body.hpp"
#include <flecs.h>
#include <algorithm>
#include <set>
#include <stdexcept>
namespace phoenix::fixed { NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec,x,y) }
namespace phoenix {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TreeState,state,elapsed,entered)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Body,id,definition,position,velocity,remainder,heading)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Operation,speed,heading,acceleration_remainder,turn_remainder,engine_enabled,radar_enabled,tree)
namespace {
constexpr std::int64_t rate = World::ticks_per_second;
struct Clock { std::uint64_t tick = 0; };
BodyView view(const Body& b) { return {b.id,b.definition,b.position.x,b.position.y,b.velocity.x,b.velocity.y,b.heading}; }
bool admissible(const Json& definition, const std::string& action, std::int64_t value) {
    const auto& systems = definition.at("systems");
    if (action == "speed_mm_s") return systems.contains("engine") && value >= 0 && value <= systems["engine"]["max_speed_mm_s"].get<std::int64_t>();
    if (action == "heading_mdeg") return systems.contains("steering") && value >= 0 && value < 360000;
    if (action == "engine_enabled") return systems.contains("engine") && (value == 0 || value == 1);
    if (action == "radar_enabled") return systems.contains("radar") && (value == 0 || value == 1);
    return false;
}
void apply(Operation& operation, const std::string& action, std::int64_t value) {
    if (action == "speed_mm_s") operation.speed = value;
    if (action == "heading_mdeg") operation.heading = value;
    if (action == "engine_enabled") operation.engine_enabled = value != 0;
    if (action == "radar_enabled") operation.radar_enabled = value != 0;
}
}
struct World::Impl {
    flecs::world ecs;
    Catalog content;
    std::string map;
    std::map<std::string,std::string> ships;
    std::map<std::string,EntityId> slot_entities;
    std::map<EntityId,flecs::entity> entities;
    std::map<std::uint64_t,EntityId> claims;
    std::vector<OperatingCommand> pending;
    Impl() { ecs.set<Clock>({0}); }
    bool owned(EntityId id) const { for (auto [console,entity] : claims) if (entity == id) return true; return false; }
    void create(EntityId id, const std::string& definition, const Json& transform) {
        const auto& collision = content.entities.at(definition).at("collision");
        Body body; body.id = id; body.definition = definition;
        body.position = {transform.at("x_mm"),transform.at("y_mm")}; body.heading = transform.at("heading_mdeg");
        body.mass = collision.at("mass_grams"); body.box = collision.at("shape") == "box";
        if (body.box) { body.half_width = collision.at("half_width_mm"); body.half_length = collision.at("half_length_mm"); }
        else body.radius = collision.at("radius_mm");
        Operation operation; operation.heading = body.heading;
        entities[id] = ecs.entity().set<Body>(body).set<Operation>(operation);
    }
};
World::World() : impl_(std::make_unique<Impl>()) {}
World::~World() = default;
void World::load(const Catalog& content, const std::string& map, const std::map<std::string,std::string>& ships) {
    verify_catalog(content);
    if (!content.maps.contains(map)) throw std::runtime_error("Unknown map: "+map);
    auto candidate = std::make_unique<Impl>();
    candidate->content.entities = content.entities; candidate->content.trees = content.trees; candidate->content.ordered_packs = content.ordered_packs;
    candidate->map = map; candidate->ships = ships;
    const auto& data = content.maps.at(map); EntityId next = 1;
    for (const auto& instance : data.at("entities")) { EntityId id = instance.at("id"); candidate->create(id,instance.at("definition"),instance); next = std::max(next,id+1); }
    std::map<std::string,Json> slots;
    for (const auto& slot : data.at("ship_slots")) slots[slot.at("id").get<std::string>()] = slot;
    for (const auto& [slot,ship] : ships) {
        if (!slots.contains(slot)) throw std::runtime_error("Unknown ship slot: "+slot);
        const auto& allowed = slots.at(slot).at("allowed_ships");
        if (std::find(allowed.begin(),allowed.end(),Json(ship)) == allowed.end()) throw std::runtime_error("Ship is not allowed in slot: "+slot);
        candidate->slot_entities[slot] = next;
        candidate->create(next++,ship,slots.at(slot));
    }
    impl_ = std::move(candidate);
}
bool World::claim(std::uint64_t console, EntityId ship) {
    if (!console || impl_->claims.contains(console) || impl_->owned(ship) || !impl_->entities.contains(ship)) return false;
    const auto& body = impl_->entities.at(ship).get<Body>(); const auto& systems = impl_->content.entities.at(body.definition).at("systems");
    if (!body.mass || !systems.contains("engine") || !systems.contains("steering")) return false;
    impl_->claims[console] = ship; return true;
}
std::optional<EntityId> World::ship_for_slot(const std::string& slot) const {
    auto found = impl_->slot_entities.find(slot);
    if (found == impl_->slot_entities.end()) return {};
    return found->second;
}
bool World::release(std::uint64_t console) {
    auto found = impl_->claims.find(console); if (found == impl_->claims.end()) return false;
    auto entity = impl_->entities.at(found->second); auto& operation = entity.get_mut<Operation>();
    operation.tree = {}; operation.speed = 0; operation.heading = entity.get<Body>().heading;
    impl_->pending.erase(std::remove_if(impl_->pending.begin(),impl_->pending.end(),[&](const auto& command){return command.target == found->second;}),impl_->pending.end());
    impl_->claims.erase(found); return true;
}
bool World::submit(std::uint64_t console, const OperatingCommand& command) {
    auto claim = impl_->claims.find(console);
    if (claim == impl_->claims.end() || claim->second != command.target || impl_->pending.size() >= 4096) return false;
    const auto& body = impl_->entities.at(command.target).get<Body>();
    if (!admissible(impl_->content.entities.at(body.definition),command.action,command.value)) return false;
    impl_->pending.push_back(command); return true;
}
void World::tick() {
    for (const auto& command : impl_->pending) apply(impl_->entities.at(command.target).get_mut<Operation>(),command.action,command.value);
    impl_->pending.clear();
    for (const auto& [id,entity] : impl_->entities) {
        auto& body = entity.get_mut<Body>(); auto& operation = entity.get_mut<Operation>();
        const auto& definition = impl_->content.entities.at(body.definition); const auto& systems = definition.at("systems");
        if (!impl_->owned(id) && definition.contains("controller")) {
            auto outputs = step_tree(impl_->content.trees.at(definition.at("controller").get<std::string>()),operation.tree);
            for (auto output = outputs.begin(); output != outputs.end(); ++output) {
                auto value = output.value().is_boolean() ? (output.value().get<bool>() ? 1 : 0) : output.value().get<std::int64_t>();
                if (admissible(definition,output.key(),value)) apply(operation,output.key(),value);
            }
        }
        if (!body.mass) continue;
        if (systems.contains("steering")) {
            auto turn = systems["steering"]["turn_mdeg_s"].get<std::int64_t>()+operation.turn_remainder;
            auto step = turn/rate; operation.turn_remainder = turn%rate;
            auto difference = (operation.heading-body.heading+540000)%360000-180000;
            body.heading = (body.heading+std::clamp(difference,-step,step)+360000)%360000;
        }
        if (systems.contains("engine") && operation.engine_enabled) {
            auto available = systems["engine"]["acceleration_mm_s2"].get<std::int64_t>()+operation.acceleration_remainder;
            auto step = available/rate; operation.acceleration_remainder = available%rate;
            auto desired = fixed::scale(fixed::direction(body.heading),operation.speed);
            auto delta = desired-body.velocity; auto magnitude = fixed::length(delta);
            body.velocity = magnitude <= step ? desired : body.velocity+fixed::scale(fixed::normal(delta),step);
        }
        body.velocity.x = std::clamp(body.velocity.x,-fixed::unit,fixed::unit); body.velocity.y = std::clamp(body.velocity.y,-fixed::unit,fixed::unit);
        body.remainder = body.remainder+body.velocity;
        body.position = body.position+fixed::Vec{body.remainder.x/rate,body.remainder.y/rate};
        body.remainder.x %= rate; body.remainder.y %= rate;
        body.position.x = std::clamp(body.position.x,-fixed::unit,fixed::unit); body.position.y = std::clamp(body.position.y,-fixed::unit,fixed::unit);
    }
    // Stable authored IDs define collision order, independently of ECS storage.
    for (int pass = 0; pass < 4; ++pass) for (auto a = impl_->entities.begin(); a != impl_->entities.end(); ++a) for (auto b = std::next(a); b != impl_->entities.end(); ++b) collide(a->second.get_mut<Body>(),b->second.get_mut<Body>());
    ++impl_->ecs.get_mut<Clock>().tick;
}
unsigned long long World::ticks() const { return impl_->ecs.get<Clock>().tick; }
std::vector<BodyView> World::inspect() const { std::vector<BodyView> result; for (const auto& [id,entity] : impl_->entities) result.push_back(view(entity.get<Body>())); return result; }
std::optional<Observation> World::observe(std::uint64_t console) const {
    auto found = impl_->claims.find(console); if (found == impl_->claims.end()) return {};
    auto entity = impl_->entities.at(found->second); const auto& own = entity.get<Body>(); Observation result{view(own),{}};
    const auto& systems = impl_->content.entities.at(own.definition).at("systems");
    if (!systems.contains("radar") || !entity.get<Operation>().radar_enabled) return result;
    auto range = systems["radar"]["range_mm"].get<std::int64_t>();
    for (const auto& [id,other] : impl_->entities) if (id != found->second && fixed::length(other.get<Body>().position-own.position) <= range) result.contacts.push_back(view(other.get<Body>()));
    return result;
}
Json World::save() const {
    if (!impl_->pending.empty()) throw std::runtime_error("Save at a tick boundary after admitted commands execute");
    Json result{{"schema",1},{"tick",ticks()},{"map",impl_->map},{"ships",impl_->ships},{"packs",impl_->content.ordered_packs},{"claims",impl_->claims},{"bodies",Json::array()}};
    for (const auto& [id,entity] : impl_->entities) result["bodies"].push_back({{"body",entity.get<Body>()},{"operation",entity.get<Operation>()}});
    return result;
}
void World::restore(const Catalog& content, const Json& saved) {
    if (saved.at("schema") != 1 || saved.at("packs") != Json(content.ordered_packs)) throw std::runtime_error("Saved state schema/content identity mismatch");
    World candidate; candidate.load(content,saved.at("map"),saved.at("ships").get<std::map<std::string,std::string>>());
    if (!saved.at("bodies").is_array() || saved.at("bodies").size() != candidate.impl_->entities.size()) throw std::runtime_error("Saved state entity roster mismatch");
    std::set<EntityId> seen;
    for (const auto& value : saved.at("bodies")) {
        auto restored = value.at("body").get<Body>(); auto operation = value.at("operation").get<Operation>();
        if (!seen.insert(restored.id).second || !candidate.impl_->entities.contains(restored.id)) throw std::runtime_error("Saved state duplicate/unknown entity");
        auto entity = candidate.impl_->entities.at(restored.id); auto& body = entity.get_mut<Body>();
        if (restored.definition != body.definition) throw std::runtime_error("Saved state definition mismatch");
        if (!body.mass && (restored.velocity.x || restored.velocity.y)) throw std::runtime_error("Saved static entity cannot have velocity");
        for (auto v : {restored.position.x,restored.position.y,restored.velocity.x,restored.velocity.y}) if (v < -fixed::unit || v > fixed::unit) throw std::runtime_error("Saved state position/velocity outside supported bounds");
        if (restored.heading < 0 || restored.heading >= 360000 || restored.remainder.x <= -rate || restored.remainder.x >= rate || restored.remainder.y <= -rate || restored.remainder.y >= rate || operation.heading < 0 || operation.heading >= 360000 || operation.acceleration_remainder < 0 || operation.acceleration_remainder >= rate || operation.turn_remainder < 0 || operation.turn_remainder >= rate) throw std::runtime_error("Saved state invalid numeric remainder/heading");
        const auto& definition = content.entities.at(body.definition);
        if (operation.speed < 0 || operation.speed > fixed::unit || (definition["systems"].contains("engine") && !admissible(definition,"speed_mm_s",operation.speed))) throw std::runtime_error("Saved state invalid speed request");
        if (definition.contains("controller")) operation.tree = load_tree(content.trees.at(definition.at("controller").get<std::string>()),value.at("operation").at("tree"));
        body.position = restored.position; body.velocity = restored.velocity; body.remainder = restored.remainder; body.heading = restored.heading;
        entity.set<Operation>(operation);
    }
    for (const auto& [console,ship] : saved.at("claims").get<std::map<std::uint64_t,EntityId>>()) if (!candidate.claim(console,ship)) throw std::runtime_error("Saved state invalid claim");
    auto tick = saved.at("tick").get<std::int64_t>(); if (tick < 0) throw std::runtime_error("Saved state invalid tick");
    candidate.impl_->ecs.get_mut<Clock>().tick = static_cast<std::uint64_t>(tick);
    impl_ = std::move(candidate.impl_);
}
}
