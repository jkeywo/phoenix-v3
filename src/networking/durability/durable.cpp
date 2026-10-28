module;
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>

module phoenix.persistence;

namespace phoenix
{
struct DurableConsensus::Impl
{
	PeerRoster roster;
	JournalRecovery recovery;
	std::unique_ptr<PeerStore> store;
	std::unique_ptr<PeerConsensus> core;
	bool writing = false, restored = false, durable = false;
	Impl(PeerRoster value, const std::string &location, JournalRecovery mode)
	    : roster(std::move(value)), recovery(mode), store(open_peer_store(location))
	{
	}
};
DurableConsensus::DurableConsensus(PeerRoster roster, const std::string &location, JournalRecovery recovery)
    : impl_(std::make_unique<Impl>(std::move(roster), location, recovery))
{
}
DurableConsensus::~DurableConsensus() = default;
void DurableConsensus::poll()
{
	auto &s = *impl_;
	if (auto event = s.store->poll())
	{
		if (!event->error.empty())
		{
			if (s.core && !event->opened)
				s.core->persisted(event->generation, false);
			throw std::runtime_error(event->error);
		}
		if (event->opened)
		{
			s.restored = !event->restored.is_null();
			if (s.recovery == JournalRecovery::require && !s.restored)
				throw std::runtime_error("The original durable peer journal is missing");
			if (s.recovery == JournalRecovery::fresh && s.restored)
				throw std::runtime_error("Unexpected existing log for a new session epoch");
			s.core = std::make_unique<PeerConsensus>(s.roster, event->restored);
			s.durable = s.restored;
		}
		else if (s.core)
		{
			s.core->persisted(event->generation, true);
			s.writing = false;
			s.durable = true;
		}
	}
}
void DurableConsensus::flush()
{
	auto &s = *impl_;
	if (!s.core)
		return;
	if (s.core->role() == PeerRole::failed)
		throw std::runtime_error("Peer consensus stopped after failed durability");
	if (!s.writing)
		if (auto write = s.core->pending_write())
			s.writing = s.store->write(*write);
}
PeerConsensus *DurableConsensus::peer() const
{
	return impl_->core.get();
}
bool DurableConsensus::restored() const
{
	return impl_->restored;
}
bool DurableConsensus::durable() const
{
	return impl_->durable;
}
} // namespace phoenix
