// Included in the combat integration case: synthetic loader -> real mechanisms
// -> scene cues -> the production host mixer, without opening a sound device.
void host_audio_contracts()
{
	auto source = powered_fixture();
	// Repairs are outside this fixture's contract; keep Workshop round-tripping
	// independent of the automatic repair-team Controller expansion.
	auto &ship = source["entities"][0];
	ship["systems"].erase("team-a");
	ship["systems"].erase("team-b");
	for (auto &profile : ship["control_layout"]["profiles"])
		profile["consoles"]["observer"]["human"] = Json::array();
	auto &controllers = ship["control_layout"]["controllers"];
	controllers.erase(std::remove_if(controllers.begin(), controllers.end(),
	                                 [](const auto &c)
	                                 {
		                                 return c.at("id") == "team-a" || c.at("id") == "team-b";
	                                 }),
	                  controllers.end());
	ship["stations"][0]["consoles"][1]["feeds"] = {"radar"};
	add_beam(source, 20);
	add_fit(source, "drive",
	        {{"kind", "engine"},
	         {"compartment", "fore"},
	         {"max_speed_mm_s", 17000},
	         {"acceleration_mm_s2", 2000},
	         {"energy_per_second", 20},
	         {"power_store", "cell"}},
	        {{"speed", "speed_mm_s"}});
	const Json loop{{"asset", "test-core:assets/audio-tone.ogg"}, {"gain", .25}};
	source["entities"][0]["audio"] = {{"ambient", loop}, {"engine", loop}};
	const Json shot_clip{{"asset", "test-core:assets/audio-tone.wav"}, {"gain", .2}};
	for (const auto *key : {"ftl_arrival", "ftl_departure", "comms_attention", "critical_warning"})
		source["entities"][0]["audio"][key] = shot_clip;
	source["entities"][0]["systems"]["gun"]["firing_audio"] = {{"asset", "test-core:assets/audio-tone.wav"},
	                                                           {"gain", .9}};
	source["entities"][0]["systems"]["beam"]["firing_audio"] = loop;
	source["entities"][0]["systems"]["gun"]["power_store"] = "cell";
	source["entities"][0]["systems"]["gun"]["discrete_energy"] = 1;
	Json contribution{{"capacity", 1000},
	                  {"initial", 1000},
	                  {"recharge_per_second", 0},
	                  {"off_recharge_per_second", 0},
	                  {"sustain_energy_per_second", 0},
	                  {"warmup_ticks", 1},
	                  {"audio", loop}};
	source["effects"] = Json::array({{{"id", "test.audio-shield"},
	                                  {"kind", "shield"},
	                                  {"contributions", {{"first", contribution}, {"second", contribution}}}}});
	add_fit(
	    source, "shield",
	    {{"kind", "shield"}, {"compartment", "fore"}, {"shield_effect", "test.audio-shield"}, {"power_store", "cell"}},
	    {});
	materialise_fixture_directed(source);
	const auto launch_mode = source["entities"][0]["systems"]["gun"]["directed"]["initial_mode"].get<std::string>();
	const auto beam_mode = source["entities"][0]["systems"]["beam"]["directed"]["initial_mode"].get<std::string>();
	source["entities"][0]["systems"]["gun"]["directed"]["modes"].push_back(beam_mode);
	for (auto &controller : source["entities"][0]["control_layout"]["controllers"])
		if (controller.at("id") == "gun")
			controller["commands"].push_back(
			    {{"id", "operation"}, {"action", "directed_operating_mode"}, {"consumers", {"gun"}}});
	auto pack = fixture_pack(json_bytes(source), "test-core");
	for (const auto *name : {"audio-tone.ogg", "audio-tone.wav"})
		pack.files[std::string("assets/") + name] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/" + name);
	const auto catalog = resolve_packs({read_pack(write_pack(pack))});
	// Exercise authoring metadata independently of runtime-generated Controller trees.
	auto authoring_pack = pack;
	auto authoring = parse_json(pack.files.at("definitions.json"), "audio authoring fixture");
	auto visual = authoring["entities"][1];
	visual["audio"] = source["entities"][0]["audio"];
	authoring["entities"] = Json::array({visual});
	authoring["maps"] = Json::array();
	authoring["equipment"] = Json::array();
	authoring["effects"].push_back({{"id", "test.audio-ability"},
	                                {"kind", "ability"},
	                                {"label", "Synthetic activation"},
	                                {"sustained", true},
	                                {"activation_audio", shot_clip}});
	authoring_pack.files["definitions.json"] = json_bytes(authoring);
	WorkshopDocument document;
	document.load_pack(write_pack(authoring_pack));
	const auto exported = resolve_packs({read_pack(document.export_pack())});
	require(compile_ship_audio(exported, "test.visual").ambient.asset == "test-core:assets/audio-tone.ogg" &&
	            compile_directed_mode(exported, launch_mode).firing_audio.asset == "test-core:assets/audio-tone.wav" &&
	            compile_directed_mode(exported, beam_mode).firing_audio.asset == "test-core:assets/audio-tone.ogg" &&
	            !compile_shield_effect(exported, "test.audio-shield").contributions[0].shield.audio.asset.empty(),
	        "Workshop round-trip retains audio authoring and assets");
	require(exported.effects.at("test.audio-ability").at("activation_audio") == shot_clip,
	        "Workshop round-trip preserves validated shared ability activation audio");
	const auto client = ship_client_assets(catalog, "test.combat", {});
	for (const auto &[path, bytes] : client.files())
		require(!path.ends_with(".ogg") && !path.ends_with(".wav"), "remote Console packs exclude host recordings");
	const auto config = compile_ship_audio(catalog, "test.combat");
	require(config.engine.asset == "test-core:assets/audio-tone.ogg",
	        "host ambience compiles through the production loader");
	require(compile_ship_audio(exported, "test.visual").ftl_departure.asset ==
	                shot_clip.at("asset").get<std::string>() &&
	            config.comms_attention.gain == .2f && config.critical_warning.gain == .2f &&
	            config.ftl_arrival.asset == shot_clip.at("asset").get<std::string>(),
	        "Workshop and verifier retain all selected host cues");
	const auto assets = scenario_assets(catalog, "test.map");
	require(std::find(assets.begin(), assets.end(), config.engine.asset) != assets.end(),
	        "host audio participates in scenario asset closure");
	World world;
	world.load(catalog, "test.map");
	claim(world);
	world.tick();
	HostAudioMixer mixer;
	const std::set<EntityId> visible{1, 2};
	auto frame = mixer.frame(world.scene(), 1, config, visible, world.ticks(), true, .05f);
	require(frame.shots.empty() && frame.loops.size() == 4,
	        "own ambience plus one shield bed per visible ship, independent of arc count");
	require(frame.loops.at("1:engine").gain == 0, "stationary actual thrust keeps engine quiet");
	submit(world, 101, "drive", "speed", 12000);
	submit(world, 101, "gun", "target", 2);
	submit(world, 101, "second", "target", 2);
	submit(world, 101, "beam", "target", 2);
	world.tick();
	frame = mixer.frame(world.scene(), 1, config, visible, world.ticks(), true, .05f);
	require(frame.shots.size() == 1, "one committed cannon discharge produces one sound");
	require(state(world)["systems"]["second"]["shots"] > 0,
	        "an unconfigured weapon can fire without acquiring a generic cannon sound");
	require(frame.loops.at("1:engine").gain > 0,
	        "paid engine output reaches the host mixer through the real scene projection");
	require(frame.loops.contains("1:weapon:beam:test-core:assets/audio-tone.ogg"),
	        "funded cutting beam produces a sustained loop");
	frame = mixer.frame(world.scene(), 1, config, visible, world.ticks(), true, .05f);
	require(frame.shots.empty(), "repeated presentation updates never replay a shot");
	bool hit = false, spike = false;
	for (int i = 0; i < 20; ++i)
	{
		world.tick();
		for (const auto &body : world.scene())
			for (const auto &cue : body.audio)
				hit = hit || (body.id == 2 && cue.kind == SceneAudioKind::Shield && cue.damage > 0);
		frame = mixer.frame(world.scene(), 1, config, visible, world.ticks(), true, .05f);
		for (const auto &[key, clip] : frame.loops)
			spike |= key.find(":shield:") != std::string::npos && clip.gain > .25f;
	}
	require(hit, "native shield resolution publishes actual hit intensity through effect metadata");
	require(spike, "resolved shield hits raise the local envelope above the shared bed");
	submit(world, 101, "beam", "enabled", 0);
	world.tick();
	frame = mixer.frame(world.scene(), 1, config, visible, world.ticks(), true, .05f);
	require(!frame.loops.contains("1:weapon:beam:test-core:assets/audio-tone.ogg"),
	        "disabled beam immediately loses its loop");
	submit(world, 101, "gun", "operation", 1);
	advance(world, 10); // The already committed launch cycle must finish.
	frame = mixer.frame(world.scene(), 1, config, visible, world.ticks(), true, .05f);
	require(frame.loops.contains("1:weapon:gun:test-core:assets/audio-tone.ogg") && frame.shots.empty(),
	        "one fitting can switch from its launch recording to its beam recording");
	submit(world, 101, "gun", "operation", 0);
	world.tick();
	frame = mixer.frame(world.scene(), 1, config, visible, world.ticks(), true, .05f);
	require(!frame.loops.contains("1:weapon:gun:test-core:assets/audio-tone.ogg"),
	        "switching away from beam removes that fitting's sustained audio");
	// Exhaust a small real store while the requested beam remains enabled.
	auto scarce = source;
	scarce["entities"][0]["resources"]["stores"][0]["initial"] = 4;
	auto scarce_pack = fixture_pack(json_bytes(scarce), "test-core");
	for (const auto *name : {"audio-tone.ogg", "audio-tone.wav"})
		scarce_pack.files[std::string("assets/") + name] = pack.files.at(std::string("assets/") + name);
	const auto scarce_catalog = resolve_packs({read_pack(write_pack(scarce_pack))});
	World interrupted;
	interrupted.load(scarce_catalog, "test.map");
	claim(interrupted);
	interrupted.tick();
	submit(interrupted, 101, "beam", "target", 2);
	interrupted.tick();
	bool funded_beam = false;
	for (const auto &body : interrupted.scene())
		for (const auto &cue : body.audio)
			funded_beam |= body.id == 1 && cue.kind == SceneAudioKind::Loop;
	require(funded_beam, "a funded sustained operation publishes a loop");
	advance(interrupted, 10);
	submit(interrupted, 101, "gun", "target", 2);
	interrupted.tick();
	for (const auto &body : interrupted.scene())
		if (body.id == 1)
			for (const auto &cue : body.audio)
				require(cue.kind == SceneAudioKind::Shield,
				        "exhausted funding stops the sustained loop and refuses cannon cues");
	require(state(interrupted)["systems"]["gun"]["shots"] == 0, "refused cannon execution does not commit a discharge");
	std::set<std::uint64_t> warnings;
	for (int i = 0; i < 40; ++i)
	{
		interrupted.tick();
		for (const auto &body : interrupted.scene())
			for (const auto &cue : body.audio)
				if (body.id == 1 && cue.source == "status:critical")
				{
					require(cue.own_only, "critical resource warning stays local to the host ship");
					warnings.insert(cue.event);
				}
	}
	require(warnings.size() == 1, "persistent real power starvation produces one warning rather than a per-tick alarm");
	World recovered_shortage;
	recovered_shortage.restore(scarce_catalog, interrupted.save());
	advance(recovered_shortage, 30);
	for (const auto &body : recovered_shortage.scene())
		for (const auto &cue : body.audio)
			require(cue.source != "status:critical", "recovery baselines the existing critical shortage");
	// Ordinary missile/PDC execution must not acquire the cannon recording.
	auto defence_source = missile_fixture(true);
	World defence;
	defence.load(load(defence_source), "test.map");
	claim(defence);
	require(defence.claim(201, 2, "drive"), "claim audio fixture defender");
	auto intercept = command(defence, 201, "pdc", "mode", 1);
	intercept.target = 2;
	require(defence.submit(201, intercept), "enable audio fixture interception");
	submit(defence, 101, "launcher", "target", 2);
	for (int i = 0; i < 40; ++i)
	{
		defence.tick();
		for (const auto &body : defence.scene())
			for (const auto &cue : body.audio)
				require(cue.kind != SceneAudioKind::Shot, "missiles and PDCs emit no generic cannon cue");
	}
	require(state(defence, 2)["systems"]["pdc"]["shots"] > 0,
	        "silent PDC case executes real committed interception fire");
	defence_source["entities"][0]["systems"]["pdc"]["firing_audio"] = shot_clip;
	auto defence_pack = fixture_pack(json_bytes(defence_source), "test-core");
	defence_pack.files["assets/audio-tone.wav"] = pack.files.at("assets/audio-tone.wav");
	World audible_defence;
	audible_defence.load(resolve_packs({read_pack(write_pack(defence_pack))}), "test.map");
	claim(audible_defence);
	require(audible_defence.claim(201, 2, "drive"), "claim audible PDC defender");
	intercept = command(audible_defence, 201, "pdc", "mode", 1);
	intercept.target = 2;
	require(audible_defence.submit(201, intercept), "enable authored PDC audio");
	submit(audible_defence, 101, "launcher", "target", 2);
	bool pdc_cue = false;
	for (int i = 0; i < 40; ++i)
	{
		audible_defence.tick();
		for (const auto &body : audible_defence.scene())
			for (const auto &cue : body.audio)
				pdc_cue |= cue.kind == SceneAudioKind::Shot && cue.source == "weapon:pdc";
	}
	require(pdc_cue, "authored PDC recording follows committed interception through the shared weapon hook");
	World restored;
	restored.restore(catalog, world.save());
	for (const auto &body : restored.scene())
		for (const auto &cue : body.audio)
			require(!cue.event, "restore reconstructs loops without old shot or impact receipts");
	frame = mixer.frame(restored.scene(), 1, config, visible, restored.ticks(), false, .05f);
	require(frame.loops.empty() && frame.shots.empty(),
	        "non-host, paused or unavailable presentation clears all mission cues");

	// A newly visible source must not replay an event consumed off screen.
	auto bodies = world.scene();
	SceneAudio shot{
	    SceneAudioKind::Shot, "weapon:synthetic", {"test-core:assets/audio-tone.wav", .9f}, 999999, world.ticks()};
	bodies[0].audio.push_back(shot);
	mixer.frame(bodies, 1, config, {}, world.ticks(), true, .05f);
	frame = mixer.frame(bodies, 1, config, visible, world.ticks(), true, .05f);
	require(frame.shots.empty(), "camera changes do not replay off-screen shots");
	bodies[0].forward_thrust = 1000;
	frame = mixer.frame(bodies, 1, config, visible, world.ticks(), true, .05f);
	require(frame.loops.at("1:engine").gain > 0 && frame.loops.at("1:engine").gain < config.engine.gain,
	        "engine gain follows actual output with local smoothing");
	bodies[0].audio_alive = false;
	frame = mixer.frame(bodies, 1, config, visible, world.ticks(), true, .05f);
	require(!frame.loops.contains("1:ambient") && !frame.loops.contains("1:engine"),
	        "departed or destroyed ship loses its interior loops");
	shot.event++;
	shot.own_only = true;
	bodies[0].audio = {shot};
	frame = mixer.frame(bodies, 2, {}, visible, world.ticks(), true, .05f);
	require(frame.shots.empty(), "visible foreign ships cannot leak internal status or Comms audio");
	shot.event++;
	bodies[0].audio = {shot};
	frame = mixer.frame(bodies, 1, config, visible, world.ticks(), true, .05f);
	require(frame.shots.size() == 1 && !frame.loops.contains("1:ambient"),
	        "committed departure can play after retirement without reviving ship loops");

	HostAudioMixer envelope;
	SceneBody shield_body;
	shield_body.id = 1;
	SceneAudio bed{SceneAudioKind::Shield, "shield", {"test-core:assets/audio-tone.ogg", .25f}};
	bed.threshold = 2;
	bed.full_damage = 30;
	shield_body.audio = {bed};
	std::vector<SceneBody> envelope_scene{shield_body};
	envelope.frame(envelope_scene, 1, {}, {1}, 0, true, 0);
	auto impact = bed;
	impact.event = 1;
	impact.damage = 16;
	impact.clip.gain = 0;
	envelope_scene[0].audio.push_back(impact);
	frame = envelope.frame(envelope_scene, 1, {}, {1}, 0, true, 0);
	require(std::abs(frame.loops.begin()->second.gain - .525f) < .0001f,
	        "shield intensity scales from the threshold to the full-spike damage");
	frame = envelope.frame(envelope_scene, 1, {}, {1}, 0, true, .1f);
	require(std::abs(frame.loops.begin()->second.gain - .4425f) < .0001f,
	        "shield decay applies to normalized intensity before mapping bed to peak");
	auto invalid = pack;
	invalid.files["assets/audio-tone.ogg"] = {1, 2, 3, 4};
	bool rejected = false;
	try
	{
		(void)resolve_packs({read_pack(write_pack(invalid))});
	}
	catch (const std::exception &)
	{
		rejected = true;
	}
	require(rejected, "invalid audio bytes fail authored-data verification without a sound device");
}
