#pragma once
#include <memory>
namespace phoenix {
class World {
public:
    World();
    ~World();
    void tick();
    unsigned long long ticks() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
