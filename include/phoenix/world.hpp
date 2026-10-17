#pragma once
#include <memory>
#include <optional>
#include <phoenix/content.hpp>
namespace phoenix {
using EntityId = std::uint64_t;
struct BodyView { EntityId id; std::string definition; std::int64_t x, y, vx, vy, heading; };
struct Observation { BodyView own; std::vector<BodyView> contacts; };
struct OperatingCommand { EntityId target; std::string action; std::int64_t value; };
class World {
public:
    static constexpr int ticks_per_second = 20;
    World();
    ~World();
    void tick();
    unsigned long long ticks() const;
    void load(const Catalog& content, const std::string& map, const std::map<std::string,std::string>& ships = {});
    bool claim(std::uint64_t console, EntityId ship);
    bool release(std::uint64_t console);
    bool submit(std::uint64_t console, const OperatingCommand& command);
    std::optional<Observation> observe(std::uint64_t console) const;
    std::vector<BodyView> inspect() const;
    Json save() const;
    void restore(const Catalog& content, const Json& saved);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
