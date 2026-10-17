#include <phoenix/session.hpp>
#include <phoenix/world.hpp>
#include <algorithm>
#include <stdexcept>
namespace phoenix {
namespace {
class LocalSession final : public SessionPort {
    std::vector<Pack> packs_;
    Catalog catalog_;
    std::unique_ptr<World> world_;
    static constexpr std::uint64_t local_console = 1;
public:
    void add_pack(const Bytes& archive) override {
        if (world_) throw std::runtime_error("End the running session before changing packs");
        if (packs_.size() >= 32) throw std::runtime_error("At most 32 ordered packs are supported");
        auto candidate = packs_;
        candidate.push_back(read_pack(archive));
        auto resolved = resolve_packs(candidate);
        packs_ = std::move(candidate); catalog_ = std::move(resolved);
    }
    void clear_packs() override {
        world_.reset(); packs_.clear(); catalog_ = {};
    }
    const Catalog& catalog() const override { return catalog_; }
    void start(const GameSetup& setup) override {
        if (!catalog_.maps.contains(setup.mission)) throw std::runtime_error("Select a loaded mission");
        if (!setup.ships.contains(setup.local_slot)) throw std::runtime_error("Select a ship for the local slot");
        const auto& ship = catalog_.entities.at(setup.ships.at(setup.local_slot));
        if (!ship.contains("console")) throw std::runtime_error("The local ship needs an authored console");
        auto candidate = std::make_unique<World>();
        candidate->load(catalog_,setup.mission,setup.ships);
        const auto controlled = candidate->ship_for_slot(setup.local_slot);
        if (!controlled || !candidate->claim(local_console,*controlled)) throw std::runtime_error("The selected ship cannot admit a local console");
        world_ = std::move(candidate);
    }
    bool running() const override { return !!world_; }
    void tick() override { if (world_) world_->tick(); }
    std::uint64_t ticks() const override { return world_ ? world_->ticks() : 0; }
    bool command(const OperatingCommand& command) override { return world_ && world_->submit(local_console,command); }
    std::optional<Observation> observation() const override { return world_ ? world_->observe(local_console) : std::nullopt; }
    std::vector<BodyView> viewscreen() const override { return world_ ? world_->inspect() : std::vector<BodyView>{}; }
};
}
std::shared_ptr<SessionPort> make_local_session() { return std::make_shared<LocalSession>(); }
}
