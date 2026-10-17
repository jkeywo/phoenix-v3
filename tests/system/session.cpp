#include <phoenix/session.hpp>
#include <iostream>
#include <stdexcept>
using namespace phoenix;
void require(bool result, const char *message)
{
	if (!result)
		throw std::runtime_error(message);
}
int main()
{
	try
	{
		Pack pack;
		pack.manifest = {{"schema", 1}, {"id", "test-core"}};
		pack.files["definitions.json"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json");
		pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
		auto session = make_local_session();
		require(!session->command({1, "speed_mm_s", 1}) && !session->observation(),
		        "Idle session must expose no claimed projection or commands");
		session->add_pack(write_pack(pack));
		auto identity = session->catalog().ordered_packs;
		bool refused = false;
		try
		{
			session->add_pack(Bytes{1, 2, 3});
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && session->catalog().ordered_packs == identity,
		        "A failed import must preserve the selected content");
		session->start({"test.map", {{"test.slot", "test.ship"}}, "test.slot"});
		auto own = session->observation()->own.id;
		require(session->viewscreen().size() == 2 && own != 1,
		        "Selected slot must create and claim its own ship, not a map entity");
		require(!session->command({1, "speed_mm_s", 1000}), "Local adapter must refuse another ship's commands");
		require(session->command({own, "speed_mm_s", 1000}), "Local adapter must admit an owned engine request");
		auto before = session->observation()->own.x;
		for (int i = 0; i < 20; ++i)
			session->tick();
		require(session->observation()->own.x > before && session->ticks() == 20,
		        "The admitted command must execute through the real fixed-tick simulation");
		refused = false;
		try
		{
			session->start({"test.map", {{"test.slot", "missing"}}, "test.slot"});
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && session->ticks() == 20 && session->observation()->own.x > before,
		        "Failed setup must preserve the running world");
		refused = false;
		try
		{
			session->add_pack(write_pack(pack));
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused, "Running content must not be hot-swapped");
		auto portable = session->save();
		auto recovered = make_local_session();
		recovered->restore(portable);
		require(recovered->recovery_pending() && !recovered->running() && !recovered->observation(),
		        "Portable recovery must pause without restoring a live console claim");
		auto paused_tick = recovered->ticks();
		recovered->tick();
		require(recovered->ticks() == paused_tick && !recovered->command({own, "speed_mm_s", 1000}),
		        "Recovery must not advance or admit commands before an explicit reclaim");
		require(recovered->catalog().ordered_packs == identity,
		        "Portable save must carry the exact original pack identities without installed content");
		recovered->resume_local();
		for (int i = 0; i < 20; ++i)
		{
			session->tick();
			recovered->tick();
		}
		auto state = [](const Bytes &archive)
		{
			return parse_json(read_pack(archive).files.at("session.json"), "session.json").at("state");
		};
		require(state(session->save()) == state(recovered->save()),
		        "Fresh console recovery must preserve tick, motion, requests, remainders and delegated work");
		auto corrupt = read_pack(portable);
		auto data = parse_json(corrupt.files.at("session.json"), "session.json");
		data["state"]["packs"][0][1] = "wrong-hash";
		corrupt.files["session.json"] = json_bytes(data);
		auto before_restore = state(recovered->save());
		refused = false;
		try
		{
			recovered->restore(write_pack(corrupt));
		}
		catch (const std::exception &)
		{
			refused = true;
		}
		require(refused && state(recovered->save()) == before_restore,
		        "A mismatched portable save must leave the running session unchanged");
		session->clear_packs();
		require(!session->running() && !session->observation() && session->catalog().ordered_packs.empty(),
		        "Reset must release authority and content together");
		std::cout << "PASS local setup, rollback, admission, projections and portable paused recovery\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
