#pragma once
namespace phoenix
{
struct ConsoleSurface::State final : public Rml::EventListener
{
	ConsoleId route_;
	bool prepare_dirty_ = true, render_dirty_ = true, render_urgent_ = true, prepared_cache_ = false;
	double render_dirty_since_ = 0, last_paint_cost_ = .003;
	std::shared_ptr<const Observation> prepared_source_;
	std::weak_ptr<SessionPort> prepared_owner_;
	std::uint64_t prepared_generation_ = 0, painted_tick_ = 0;
	std::string prepared_environment_;
	std::set<std::string> prepared_pads_;
	std::vector<std::pair<std::string, std::string>> cached_controls_, cached_readouts_;
	std::vector<Application::TextControl> cached_inputs_;
	std::map<std::string, bool> cached_toggles_;
	std::map<std::string, Application::ControlPlacement> cached_placements_;
	std::map<std::string, Application::ControlPlacement> painted_controls_;
	RenderTexture2D ui_texture_{};
	static bool same_observation_scope(const Observation &left, const Observation &right);

	static bool same_observation_scope(const std::shared_ptr<const Observation> &left,
	                                   const std::shared_ptr<const Observation> &right);

	void remember_metadata(const Application &app);

	std::map<std::string, Application::ControlPlacement> interactive_placements() const;

	void check_painted_controls();

	void restore_metadata(Application &app);

	std::string context_name_;
	bool toolbar_enabled_ = true;
	std::uint64_t display_update_tick_ = ~std::uint64_t{0};
	std::string display_announcement_;
	std::vector<std::int64_t> display_camera(const std::string &widget) const;

	bool profile_open_ = false;
	std::string profile_name_;
	Bytes profile_avatar_;
	Json terminal_;
	std::map<std::string, Bytes> crew_portraits_;
	Rml::Context *context_ = nullptr;
	Rml::ElementDocument *document_ = nullptr;
	ConsoleModel model_;
	std::string manual_profile_, manual_console_;
	bool manual_toggle_ = false;
	std::optional<std::string> manual_preset(bool automatic) const;

	NavigationPanel navigation_;
	NavigationFrame navigation_frame(Application &, const ConsoleRegion &, bool pregame);

	ConversationPanel conversations_;
	ConversationFrame conversation_frame(Application &, const ConsoleRegion &);

	TextureCache radar_textures_;
	std::map<std::string, std::unique_ptr<RadarSweep>> radar_sweeps_;
	Application *app_ = nullptr;
	std::string signature_;
	std::vector<std::pair<std::string, std::string>> content_packs_;
	std::map<std::string, std::vector<ConsoleSpec>> resolved_consoles_;
	const std::vector<ConsoleSpec> &console_specs(const Application &app, const std::string &ship);

	const Json *prepared_layout_ = nullptr;
	std::uint64_t prepared_layout_revision_ = ~std::uint64_t{0}, layout_identity_ = 0;
	struct StanceChoice
	{
		std::string action, target, label;
		bool selected;
		bool operator==(const StanceChoice &) const = default;
	};
	std::vector<StanceChoice> command_choices_;
	std::string command_language_, command_markup_;

	std::map<std::string, std::string> displayed_values_;
	std::vector<std::pair<std::string, std::string>> readout_values_, resource_readouts_;
	std::set<std::string> visible_readouts_;
	void measure_readouts();

	bool update_readout(const std::string &name, const std::string &text, bool force);

	ConsoleDocument dom_;

	std::weak_ptr<SessionPort> owner_;
	std::uint64_t generation_ = 0;
	std::vector<std::pair<std::string, std::string>> controls_;
	std::string gameplay_signature_, sensor_controls_signature_;
	std::string placement_signature_;

	// Members of ConsoleSurface. Authored placement uses the same live component elements.
	UiThemeRenderer theme_renderer_;
	Json active_theme_ = Json::object();
	std::optional<UiLayout> authored_program_;
	std::set<std::string> authored_targets_;
	UiResolved authored_resolved_, authored_header_resolved_;
	bool preview_header_ = false;
	std::string authored_geometry_, authored_dom_;
	struct SurfaceBrowsing
	{
		NavigationBrowsing navigation;
		std::string draft;
		std::map<std::string, float> scroll;
		std::map<std::string, std::string> pages;
	};
	std::map<std::string, SurfaceBrowsing> surface_browsing_;
	std::string browsing_console_, restore_draft_;
	bool restore_browsing_ = false, page_focus_pending_ = false;
	void sync_console_browsing();

	void restore_console_browsing();

	std::map<std::string, std::string> authored_pages_;
	std::map<std::string, std::string> authored_parents_;
	std::string preview_pages_signature_;
	bool preview_interact_ = false;
	std::string authored_identity_, authored_tabs_markup_, console_tabs_markup_, theme_identity_;
	std::map<std::string, std::string> authored_page_groups_;
	Rml::Element *authored_component(const std::string &target);

	bool authored_component_visible(const std::string &target);

	void return_authored_components();

	void restore_authored_components();

	void apply_console_theme(const Json *layout);

	bool authored_action(const std::string &action);

	struct StationPage
	{
		std::string console, group, page, label, component, action;
	};
	std::vector<StationPage> station_pages_;
	std::weak_ptr<SessionPort> station_program_owner_;
	std::map<std::string, std::pair<std::string, UiLayout>> station_programs_;
	void prepare_console_tabs(const ConsoleRegion &region);

	std::string layout_node_id(const UiNode &node) const;

	void build_authored_tree(const UiNode &root);

	void style_authored_tree(const UiNode &node, const UiNode *parent = nullptr, bool visible = true);

	void place_authored_instruments(const UiNode &node);

	void fit_authored_aspects(const UiNode &node);

	void measure_authored_geometry();

	void prepare_authored_layout(const Json *layout, const ConsoleRegion &region);

	std::string gameplay_readout_signature_, fault_dialog_signature_, fault_dialog_system_;
	std::string engineering_signature_;
	std::set<std::string> held_clicks_;
	std::map<std::string, Texture2D> hud_;
	TextureCache hud_cache_;
	std::map<std::string, Json> hud_metadata_;
	std::optional<std::array<float, 3>> heading_dial_;
	std::map<std::uint64_t, std::string> heading_contacts_;
	std::map<std::uint64_t, std::string> throttle_contacts_;
	std::map<std::string, Rectangle> throttle_tracks_;
	ManualFlightInput manual_input_;
	std::map<std::string, FlightAxes> pad_axes_;
	FlightAxes manual_sent_;
	std::optional<std::uint64_t> joystick_contact_;
	Rectangle joystick_area_{};
	std::optional<std::uint64_t> manual_pulse_tick_;
	bool manual_was_active_ = false;
	std::string manual_identity_, manual_markup_;
	std::array<bool, 3> keyboard_flight_{};
	std::uint64_t manual_sent_tick_ = ~std::uint64_t{0};
	bool syncing_values_ = false;
	bool settings_open_ = false;
	bool station_loading_ = false;
	std::string station_loading_markup_;
	std::string local_settings_tab_ = "display", local_settings_markup_;
	int text_scale_ = 100;
	std::string local_language_ = "en";
	std::vector<std::pair<std::string, std::string>> local_settings_controls_;
	std::string settings_button(const std::string &name, const std::string &label);

	std::string star_choice(const std::string &action, const std::string &profile,
	                        const std::map<std::string, int> &levels, const std::string &selected, bool settings);

	bool select_lobby_complexity(const std::string &profile);

	OperationsWidgets operations_;
	OperationsWidgetContext operations_context();

	// A granted radar-only combat Console can share its operator's flight surface.
	// Commands still pass through that Console's own model and ordinary admission.
	TacticalWidgets tactical_;
	bool systems_open_ = false;
	Json help_level_ = Json::object();
	std::string help_page_, help_label_, help_identity_, help_open_identity_;
	void prepare_help(const ConsoleRegion &region);
	std::vector<std::pair<std::string, std::string>> popup_controls_;

	bool select_radar_contact(EntityId contact);

	void prepare_system_popup(const ConsoleRegion &region, const std::vector<EngineeringCard> &cards);

	std::string radar_choices_signature_;
	void prepare_radar_choices(const ConsoleRegion &region);

	// Members of ConsoleSurface: local drag feedback never changes flight authority.
	ThrottleFeedback throttles_;
	bool all_stop(const std::string &key);

	// Compose the granted Support workload without changing Console command ownership.
	std::string support_root_, support_comms_, support_navigation_, support_page_ = "support";
	bool support_detail_ = false;
	std::map<std::string, std::unique_ptr<ConsoleModel>> support_models_;
	std::string support_popup_, support_layout_signature_;
	bool support_layout_requested_ = false;
	bool support_active() const;

	bool support_primary() const;

	void refresh_support_panels(Application &app);

	bool support_action(const std::string &action);

	bool support_repair_control(const std::string &name, const Json &descriptor) const;

	void finish_support_panels(const ConsoleRegion &region);

	// One persistent board per engineering watch. Telemetry updates never recreate a live slider.
	std::map<std::string, std::string> engineering_signatures_;
	std::string power_identity_;
	std::weak_ptr<SessionPort> power_owner_;
	std::uint64_t power_generation_ = 0;
	std::map<std::string, PowerSetting> power_controls_;
	struct PowerPending
	{
		std::int64_t value = 0;
		std::uint64_t until = 0;
	};
	std::map<std::string, PowerPending> power_pending_;
	std::map<std::string, std::string> support_choices_;
	bool support_value(const std::string &name, const std::string &text);

	void prepare_engineering_component(const std::string &component, const ConsoleRegion &region, bool pregame);

	// Registered task components. Each consumes only its permitted model/command family.
	std::map<std::string, std::string> task_actions_;
	std::map<int, std::string> condition_choices_;
	std::optional<std::pair<int, std::uint64_t>> condition_pending_;
	int condition_selected_ = 0;
	std::vector<Json> shield_sectors_;
	std::optional<SpriteMaterial> shield_ship_sprite_;
	Rectangle shield_ship_crop_{};
	std::string shield_ship_definition_;
	std::vector<std::pair<ConsoleModel *, std::string>> task_sources();

	void prepare_task_panels(const ConsoleRegion &region);

	void draw_support_shields(float ox, float oy);

	// Live components are placed only by the authored layout, including Workshop previews.
	std::map<std::string, std::string> station_component_markup_;
	std::string station_component_scope_;
	void ensure_station_components();

	bool station_component_active(const std::string &component);

	void prepare_station_components(const ConsoleRegion &region, bool pregame);
	void prepare_control_widgets(const ConsoleRegion &region);
	std::string control_detail_return_focus_;

	void prepare_local_settings(Application &app, const ConsoleRegion &region);

	std::map<std::string, double> throttle_sent_at_;
	std::map<std::string, double> heading_sent_at_;
	std::map<std::pair<std::string, std::string>, std::int64_t> engineering_probe_requests_;
	std::map<std::string, std::array<float, 3>> heading_dials_;
	std::map<std::string, Rectangle> sharing_pods_;
	std::map<std::uint64_t, std::string> sharing_contacts_;
	bool sharing_hit(const std::string &key, float x, float y) const;

	std::map<std::string, std::string> dial_systems_;
	std::string asset_path(const std::string &name) const;

	Texture2D hud(const std::string &name);

	static void image(Texture2D texture, Rectangle area);

	Vector2 widget_offset(const std::string &key) const;

	Rectangle throttle_rect(const std::string &key) const;

	void place(Application &app, const ConsoleRegion &region, const std::string &name);

	std::string id(const std::string &action) const;

	std::string on_screen_markup(const std::string &action) const;

	std::string button(const std::string &action, const std::string &label, const std::string &visual = "");

	std::string console_markup(std::string markup) const;

public:
	explicit State(ConsoleId route, const std::string &prefix = "console-", bool toolbar = true);

	~State();

	void preview_text_scale(int scale);

	const UiResolved &authored_geometry() const;

	void preview_pages(const Json &layout);

	Rml::Context &context();

	std::optional<bool> local_settings_action(const std::string &name, const std::string &action);
	bool open_local_settings(const std::string &action);
	std::optional<bool> lobby_profile_action(const std::string &action);
	std::optional<bool> camera_navigation_action(const std::string &action);
	std::optional<bool> popup_action(const std::string &action);
	std::optional<bool> manual_flight_action(const std::string &action);
	bool action(const std::string &name);

	bool controller_hold(const std::string &name, bool pressed);

	std::string controller_identity() const;

	bool controller_flight() const;

	Rml::Element *controller_element(const std::string &name);

	void controller_cancel();

	bool controller_back();

	void ProcessEvent(Rml::Event &event) override;

	void focus(const std::string &name);

	bool set_value(const std::string &name, const std::string &text);

	void flush_manual();

	void cancel_gamepad(const std::string &device);

	void gamepad(const std::string &device, const std::string &control, float value);

	void manual_frame(bool keyboard_selected, FlightAxes keyboard_axes, bool focused);

	void prepare_manual(Application &app, const ConsoleRegion &region, bool pregame);

	bool pointer(std::uint64_t contact, float x, float y, DeviceEventKind kind);

	void cancel_pointer();

	void update_view();

	bool probe_throttle(unsigned stage);

	bool probe_conversation() const;

	bool probe_tab(const std::string &action);

	bool probe_navigation(unsigned stage);

	bool probe_fit() const;

	bool probe_support_visuals();

	bool probe_readout_scroll();

	bool probe_report();

	bool probe_ready();

	bool probe_on_screen(const std::string &widget);

	bool probe_helm(bool verify = false);

	bool probe_manual(unsigned stage);

	bool probe_engineering(unsigned stage);

	bool preparation_urgent() const;

	void prepare(Application &app, const ConsoleRegion &physical_region, const std::set<std::string> &pads,
	             bool allow_fresh = true);

	bool paint_pending();

	double paint_wait() const;

	double paint_cost() const;

	void render_ui(UiRenderer &renderer, const ConsoleRegion &region, bool allow_paint = true);

	void prepare_loading(const LoadingProgress *loading, const ConsoleRegion &region);

	void prepare_current(Application &app, const ConsoleRegion &physical_region, const std::set<std::string> &pads);

	void draw_profile(const ConsoleRegion &region);

	void draw(const ConsoleRegion &region);
};
} // namespace phoenix
