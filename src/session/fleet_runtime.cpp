module;
#include <optional>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

module phoenix.fleet_setup;
import phoenix.blobs;
import phoenix.persistence;
import phoenix.wire;

namespace phoenix
{
struct FleetRuntime::Impl
{
	enum class Phase
	{
		dormant,
		lock,
		opening,
		profile,
		bundle,
		writing_bundle,
		writing_profile,
		journal,
		sealing,
		active,
		failed
	};
	std::optional<PreparedFleet> setup;
	NetworkOptions options;
	std::unique_ptr<PeerStore> lock;
	std::unique_ptr<BlobStore> blobs;
	std::unique_ptr<FleetCommit> fleet;
	Phase phase = Phase::dormant;
	bool recovering = false, expected_journal = false;
	std::string profile_key, bundle_key, fingerprint, error;
	Json profile;
	Impl(std::optional<PreparedFleet> initial, NetworkOptions config)
	    : setup(std::move(initial)), options(std::move(config))
	{
		recovering = !setup;
		if (setup)
		{
			options.link.room = setup->plan.at("room");
			options.link.self = setup->self;
		}
		verify_link_options(options.link);
		const auto name = options.profile + "/" + options.link.room + "/" + options.link.self;
		const auto digest = sha256(Bytes(name.begin(), name.end()));
		profile_key = "server." + digest;
		if (recovering)
			start();
	}
	void start()
	{
		if (phase != Phase::dormant)
			return;
		lock = open_peer_store(options.data_location + "/identities/" + profile_key + ".lock");
		phase = Phase::lock;
	}
	Json metadata(bool expected) const
	{
		const auto &plan = setup->plan;
		const std::string key = plan.at("key");
		const auto roster = plan.at("members").dump();
		return {{"schema", 1},
		        {"self", setup->self},
		        {"room", options.link.room},
		        {"profile", options.profile},
		        {"session", plan.at("session")},
		        {"bundle", bundle_key},
		        {"fingerprint", fingerprint},
		        {"key_hash", sha256(Bytes(key.begin(), key.end()))},
		        {"roster_hash", sha256(Bytes(roster.begin(), roster.end()))},
		        {"journal_expected", expected}};
	}
	void check_profile(const Json &value) const
	{
		if (value.at("schema") != 1 || value.at("self") != options.link.self || value.at("room") != options.link.room ||
		    value.at("profile") != options.profile || !value.at("journal_expected").is_boolean())
			throw std::runtime_error("Stored server identity belongs to another profile or route");
		const std::string bundle = value.at("bundle");
		if (!valid_hex_token(bundle))
			throw std::runtime_error("Stored server bundle identity is invalid");
	}
	void identify()
	{
		fingerprint = sha256(json_bytes(setup->world->authoritative_world().save()));
		bundle_key = sha256(setup->bundle);
	}
	void open_journal()
	{
		FleetOptions config;
		config.roster.session = setup->plan.at("session");
		config.roster.self = setup->self;
		for (const auto &[peer, slot] : setup->plan.at("members").items())
			config.roster.members.push_back(peer);
		config.link = options.link;
		const auto room = "fleet/" + options.link.room + "/" + config.roster.session;
		config.link.room = sha256(Bytes(room.begin(), room.end()));
		config.key = setup->plan.at("key");
		config.fingerprint = fingerprint;
		config.journal = options.data_location + "/fleet/" + config.roster.session + "/" + setup->self + ".journal";
		config.require_existing = expected_journal;
		config.activation_required = true;
		fleet = std::make_unique<FleetCommit>(std::move(config));
		phase = Phase::journal;
	}
	void complete(BlobResult result)
	{
		if (!result.error.empty())
			throw std::runtime_error("Server identity persistence failed: " + result.error);
		if (result.opened)
		{
			if (phase != Phase::opening || !blobs->read(1, profile_key))
				throw std::runtime_error("Cannot read the durable server identity");
			phase = Phase::profile;
			return;
		}
		if (phase == Phase::profile)
		{
			if (result.bytes)
			{
				profile = parse_json(*result.bytes, "server identity");
				check_profile(profile);
			}
			if (recovering)
			{
				if (!result.bytes)
					throw std::runtime_error("No persisted server profile exists for this room/server");
				bundle_key = profile.at("bundle");
				if (!blobs->read(2, "setup." + bundle_key))
					throw std::runtime_error("Cannot read the original private fleet setup");
				phase = Phase::bundle;
			}
			else
			{
				identify();
				if (result.bytes && profile.at("session") == setup->plan.at("session"))
				{
					const auto intended = metadata(profile.at("journal_expected"));
					for (const char *field : {"fingerprint", "key_hash", "roster_hash"})
						if (profile.at(field) != intended.at(field))
							throw std::runtime_error("The same fleet epoch cannot replace its content, key or roster");
					expected_journal = profile.at("journal_expected");
				}
				if (!blobs->write(3, "setup." + bundle_key, setup->bundle))
					throw std::runtime_error("Cannot persist the exact fleet setup bundle");
				phase = Phase::writing_bundle;
			}
		}
		else if (phase == Phase::bundle)
		{
			if (!result.bytes || sha256(*result.bytes) != bundle_key)
				throw std::runtime_error("The original private fleet setup is missing or corrupt");
			setup = read_fleet_setup(*result.bytes, options.link.self);
			if (setup->plan.at("room") != options.link.room)
				throw std::runtime_error("Recovered fleet setup belongs to another network room");
			identify();
			const auto intended = metadata(profile.at("journal_expected"));
			for (const char *field : {"session", "fingerprint", "key_hash", "roster_hash", "room"})
				if (profile.at(field) != intended.at(field))
					throw std::runtime_error("Recovered server identity and verified initial world disagree");
			expected_journal = profile.at("journal_expected");
			open_journal();
		}
		else if (phase == Phase::writing_bundle)
		{
			profile = metadata(expected_journal);
			if (!blobs->write(4, profile_key, json_bytes(profile)))
				throw std::runtime_error("Cannot persist the server identity record");
			phase = Phase::writing_profile;
		}
		else if (phase == Phase::writing_profile)
			open_journal();
		else if (phase == Phase::sealing)
		{
			expected_journal = true;
			if (!fleet->activate())
				throw std::runtime_error("The sealed peer journal cannot activate");
			phase = Phase::active;
		}
	}
	void poll()
	{
		if (phase == Phase::dormant || phase == Phase::failed)
			return;
		try
		{
			if (lock)
				if (auto result = lock->poll())
				{
					if (!result->error.empty())
						throw std::runtime_error("This server profile is already open or unavailable: " +
						                         result->error);
					if (phase == Phase::lock)
					{
						blobs = open_blob_store(options.data_location + ".servers");
						phase = Phase::opening;
					}
				}
			if (blobs)
				if (auto result = blobs->poll())
					complete(std::move(*result));
			if (fleet)
			{
				fleet->poll();
				if (fleet->failed())
					throw std::runtime_error(fleet->status());
				if (phase == Phase::journal && fleet->storage_ready())
				{
					if (expected_journal)
					{
						if (!fleet->activate())
							throw std::runtime_error("The original peer journal cannot activate");
						phase = Phase::active;
					}
					else
					{
						profile = metadata(true);
						if (!blobs->write(5, profile_key, json_bytes(profile)))
							throw std::runtime_error("Cannot seal the recoverable server identity");
						phase = Phase::sealing;
					}
				}
			}
		}
		catch (const std::exception &failure)
		{
			error = failure.what();
			phase = Phase::failed;
			fleet.reset();
		}
	}
};
FleetRuntime::FleetRuntime(PreparedFleet prepared, NetworkOptions options)
    : impl_(std::make_unique<Impl>(std::move(prepared), std::move(options)))
{
}
FleetRuntime::FleetRuntime(NetworkOptions options) : impl_(std::make_unique<Impl>(std::nullopt, std::move(options)))
{
}
FleetRuntime::~FleetRuntime() = default;
void FleetRuntime::start()
{
	impl_->start();
}
void FleetRuntime::poll()
{
	impl_->poll();
}
const PreparedFleet *FleetRuntime::prepared() const
{
	return impl_->setup ? &*impl_->setup : nullptr;
}
bool FleetRuntime::begun() const
{
	return impl_->phase != Impl::Phase::dormant;
}
bool FleetRuntime::failed() const
{
	return impl_->phase == Impl::Phase::failed;
}
bool FleetRuntime::available() const
{
	return impl_->phase == Impl::Phase::active && impl_->fleet->available();
}
bool FleetRuntime::leader() const
{
	return impl_->phase == Impl::Phase::active && impl_->fleet->leader();
}
bool FleetRuntime::started() const
{
	return impl_->fleet && impl_->fleet->started();
}
bool FleetRuntime::propose(const Json &value)
{
	return available() && impl_->fleet->propose(value);
}
bool FleetRuntime::propose_tick(const Json &value)
{
	return available() && impl_->fleet->propose_tick(value);
}
std::vector<CommittedEntry> FleetRuntime::take_committed()
{
	return impl_->fleet ? impl_->fleet->take_committed() : std::vector<CommittedEntry>{};
}
bool FleetRuntime::checkpoint_due() const
{
	return impl_->fleet && impl_->fleet->checkpoint_due();
}
std::uint64_t FleetRuntime::admission_generation() const
{
	return impl_->fleet ? impl_->fleet->admission_generation() : 0;
}
bool FleetRuntime::checkpoint(const Json &state)
{
	return impl_->fleet && impl_->fleet->checkpoint(state);
}
std::string FleetRuntime::status() const
{
	if (failed())
		return "Server recovery stopped: " + impl_->error;
	if (!begun())
		return "Private fleet setup verified; start this ship server when ready";
	if (impl_->fleet)
		return impl_->fleet->status();
	return "Verifying and persisting the server identity and exact fleet content";
}
} // namespace phoenix
