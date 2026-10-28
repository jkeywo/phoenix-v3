# Resource networks

Start with `resources.cppm` (`phoenix.resources`). `resource_definitions.cpp` validates the immutable network; `resources.cpp` owns native allocation, grants, thermal accounting and continuation. The module imports the independently owned stock and processor definition contracts. `validation.hpp` remains private to this module.

`phoenix_resources_networks_definitions` links foundation plus stock/processor definitions. `phoenix_resources_networks` adds network execution and stock accounting. Fitted equipment adapters live in `resources/equipment`; they do not duplicate allocation policy.

Keep deterministic request/resolve/consume ordering, finite stocks, fractional remainders, thermal state and one committed network tick. Existing stock, processor, power-allocation and paid-flight integration cases protect those contracts without freezing shipped tuning.
