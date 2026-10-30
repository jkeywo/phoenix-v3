// Private browsing state shared by the existing workflow renderers.
struct OperationsWidgets::State
{
public:
	std::string mission_selected_, mission_picker_, assignment_post_, assignment_mission_, assignment_slot_;
	std::string mission_markup(const OperationsWidgetContext &);
	std::string assignment_markup(const OperationsWidgetContext &);
	bool mission_action(const OperationsWidgetContext &, const std::string &);
	bool assignment_action(const OperationsWidgetContext &, const std::string &);
	WidgetPanel teams_panel(const OperationsWidgetContext &context);
	std::string personnel_selected_, personnel_notice_;
	std::vector<std::pair<std::string, std::string>> personnel_readouts_;
	std::string personnel_markup(const OperationsWidgetContext &context);

	bool personnel_action(const OperationsWidgetContext &context, const std::string &action);

	std::string away_notice_;
	std::vector<std::pair<std::string, std::string>> away_readouts_;

	std::string away_markup(const OperationsWidgetContext &context);

	bool away_action(const OperationsWidgetContext &context, const std::string &action);

	std::string craft_notice_;
	std::vector<std::pair<std::string, std::string>> craft_readouts_;
	std::string craft_markup(const OperationsWidgetContext &context);

	bool craft_action(const OperationsWidgetContext &context, const std::string &action);

	std::string supply_notice_;
	std::int64_t supply_quantity_ = 10;
	std::vector<std::pair<std::string, std::string>> supply_readouts_;
	std::string supply_line(const OperationsWidgetContext &context, const std::string &text);

	std::string supply_button(const OperationsWidgetContext &context, const std::string &kind,
	                          const std::string &action, const std::string &system, const std::string &text,
	                          const std::string &label);

	std::string logistics_markup(const OperationsWidgetContext &context);

	std::string docking_markup(const OperationsWidgetContext &context);

	bool supply_action(const OperationsWidgetContext &context, const std::string &action);
};
