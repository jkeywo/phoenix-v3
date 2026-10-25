std::string comms_widget_, comms_list_signature_, comms_choice_signature_, comms_header_signature_;
std::string comms_board_signature_;
std::uint64_t comms_visible_thread_ = 0, comms_read_thread_ = 0, comms_read_message_ = 0;
bool comms_list_only_ = false;
std::vector<std::pair<std::string, std::string>> comms_controls_;
bool conversation_action(const std::string &action)
{
	if (!action.starts_with("comms/"))
		return false;
	const auto board = model_.conversations();
	if (action == "comms/back")
	{
		comms_list_only_ = true;
		return true;
	}
	if (action == "comms/screen")
		return model_.on_screen(comms_widget_, {0, 0, 1});
	if (action.starts_with("comms/hail/"))
	{
		comms_list_only_ = false;
		return model_.conversation_action("hail", 0, action.substr(11));
	}
	if (action.starts_with("comms/select/"))
	{
		comms_list_only_ = false;
		return model_.conversation_action("select", std::stoull(action.substr(13)));
	}
	if (action.starts_with("comms/answer/"))
		for (const auto &thread : board.value("threads", Json::array()))
			if (thread.at("id") == board.value("selected", std::uint64_t{0}))
				return model_.conversation_action("answer", thread.at("id"), action.substr(13), thread.at("prompt"));
	if (action == "comms/send")
		if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("comms/draft"))))
			if (model_.conversation_action("text", board.value("selected", std::uint64_t{0}), field->GetValue()))
			{
				field->SetValue("");
				return true;
			}
	return false;
}
void refresh_conversations(Application &app, const ConsoleRegion &region)
{
	auto *root = document_->GetElementById("comms-layer");
	comms_widget_.clear();
	if (model_.layout())
		for (const auto &w : model_.layout()->at("widgets"))
			if (w.at("kind") == "conversations")
				comms_widget_ = w.at("id");
	const auto board = model_.conversations();
	const bool active = (!comms_widget_.empty() || (model_.layout() && model_.layout()->contains("layout"))) &&
	                    !board.empty() && app.screen != Screen::server_lobby;
	const bool authored = model_.layout() && model_.layout()->contains("layout");
	if (!authored)
		property(root, "display", active ? "block" : "none");
	if (!active)
		return;
	const auto board_signature = board.dump();
	if (board_signature != comms_board_signature_)
	{
		elements_.clear();
		properties_.clear();
		comms_board_signature_ = board_signature;
	}
	if (!authored)
	{
		property(document_->GetElementById("gameplay"), "display", "none");
		property(document_->GetElementById("widgets"), "display", "none");
	}
	const auto escape = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	const auto button = [&](const std::string &key, const std::string &label)
	{
		comms_controls_.emplace_back(key, label);
		return "<button id=\"" + escape(id(key)) + "\">" + escape(label) + "</button>";
	};
	auto *compose = document_->GetElementById("comms-compose");
	if (!compose->HasChildNodes())
		compose->SetInnerRML("<input id=\"" + id("comms/draft") + "\" type=\"text\" maxlength=\"4096\"/><button id=\"" +
		                     id("comms/send") + "\">" + escape(localise("Send")) + "</button>");
	comms_controls_.clear();
	std::string listing;
	for (const auto &thread : board.at("threads"))
	{
		const auto key = thread.at("id").get<std::uint64_t>();
		auto title = localise(thread.at("title").get<std::string>());
		if (thread.value("unread", false))
			title = "* " + title;
		listing += button("comms/select/" + std::to_string(key), title);
	}
	listing += "<h3>" + escape(localise("Hail")) + "</h3>";
	for (const auto &p : board.at("directory"))
		if (p.value("available", false) && p.at("id") != "ship:" + std::to_string(model_.observation()->own.id))
			listing += button("comms/hail/" + p.at("id").get<std::string>(),
			                  localise(p.at("name").get<std::string>()) + " (#" +
			                      std::to_string(p.at("host").get<EntityId>()) + ")");
	if (listing != comms_list_signature_)
	{
		elements_.clear();
		properties_.clear();
		const auto focused = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
		document_->GetElementById("comms-list")->SetInnerRML(listing);
		comms_list_signature_ = listing;
		if (!focused.empty())
			if (auto *element = document_->GetElementById(focused))
				element->Focus();
	}
	const auto selected = board.value("selected", std::uint64_t{0});
	const Json *thread = nullptr;
	for (const auto &v : board.at("threads"))
		if (v.at("id") == selected)
			thread = &v;
	const bool split =
	    authored && std::any_of(authored_resolved_.placements.begin(), authored_resolved_.placements.end(),
	                            [](const auto &p)
	                            {
		                            return p.component.starts_with("conversation-");
	                            });
	const auto panel_width =
	    authored ? std::max(100.f, root->GetClientWidth() / context_->GetDensityIndependentPixelRatio()) : region.width;
	const bool narrow = !split && panel_width < 700;
	const bool detail_visible = split ? authored_component_visible("conversation-history")
	                                  : (!authored || authored_component_visible("conversations")) &&
	                                        (!narrow || (thread && !comms_list_only_));
	if (!split)
	{
		property(document_->GetElementById("comms-list"), "width", narrow ? "100%" : "30%");
		property(document_->GetElementById("comms-list"), "display",
		         narrow && thread && !comms_list_only_ ? "none" : "block");
		auto *conversation = document_->GetElementById("comms-conversation");
		property(conversation, "left", narrow ? "0%" : "32%");
		property(conversation, "width", narrow ? "100%" : "68%");
		property(conversation, "display", narrow && (!thread || comms_list_only_) ? "none" : "block");
	}
	const std::string width = "100%";
	std::string header = "<div style=\"display:block;width:" + width + ";height:56dp;\">" +
	                     button("comms/back", localise("Conversations")) +
	                     button("comms/screen", localise("On screen") + ": " +
	                                                localise(model_.is_on_screen(comms_widget_) ? "Live" : "Standby")) +
	                     "</div>";
	if (thread)
	{
		header += "<div style=\"display:block;width:" + width + ";font-size:1.1em;margin:4dp 0;\">" +
		          escape(localise(thread->at("title").get<std::string>())) + "</div>";
		for (const auto &p : thread->at("members"))
			header += "<div style=\"display:block;width:" + width + ";\">" +
			          escape(localise(p.at("name").get<std::string>())) + " — " +
			          escape(localise(p.at("connected").get<bool>() ? "Connected" : "Out of contact")) + "</div>";
	}
	auto *head = document_->GetElementById("comms-header");
	if (!authored)
		property(head, "width", width);
	if (comms_header_signature_ != header)
	{
		elements_.clear();
		properties_.clear();
		head->SetInnerRML(header);
		comms_header_signature_ = header;
	}
	std::vector<ConsoleMessageView> messages;
	if (thread)
		for (const auto &m : thread->at("messages"))
		{
			messages.push_back(message_ui::conversation_message(
			    m, "comms/message/" + std::to_string(selected) + "/" + std::to_string(m.at("id").get<std::uint64_t>()),
			    app.session->catalog()));
		}
	auto *history = document_->GetElementById("comms-history");
	const bool at_bottom = history->GetScrollTop() + history->GetClientHeight() >= history->GetScrollHeight() - 8;
	const auto readout_count = app.screen_readouts.size();
	message_ui::refresh(*document_, *history, messages, app, id(""));

	if (thread)
		for (const auto &message : thread->at("messages"))
			if (auto *row = document_->GetElementById(id("comms/message/" + std::to_string(selected) + "/" +
			                                             std::to_string(message.at("id").get<std::uint64_t>()))))
			{
				property(row, "width", "100%");
				property(row, "box-sizing", "border-box");
				property(row, "margin-left", "0dp");
				property(row, "margin-right", "0dp");
			}

	if (!detail_visible)
		app.screen_readouts.resize(readout_count);
	if (detail_visible && thread && !thread->at("messages").empty())
	{
		const auto latest = thread->at("messages").back().at("id").get<std::uint64_t>();
		if (comms_read_thread_ != selected || comms_read_message_ != latest)
		{
			if (model_.conversation_action("select", selected))
			{
				comms_read_thread_ = selected;
				comms_read_message_ = latest;
			}
		}
	}
	if (at_bottom || comms_visible_thread_ != selected)
		history->SetScrollTop(history->GetScrollHeight());
	comms_visible_thread_ = selected;
	std::string incoming;
	if (thread)
		for (const auto &p : thread->at("incoming"))
			incoming += localise(p.at("name").get<std::string>()) + ": " + localise("Incoming message") +
			            (app.reduced_motion ? "..." : std::string(1 + static_cast<int>(GetTime() * 2) % 3, '.')) + " ";
	if (displayed_values_["comms-incoming"] != incoming)
	{
		document_->GetElementById("comms-incoming")->SetInnerRML(escape(incoming));
		displayed_values_["comms-incoming"] = incoming;
	}
	std::string choices;
	if (thread)
		for (const auto &c : thread->at("choices"))
			if (c.value("enabled", true))
				choices +=
				    button("comms/answer/" + c.at("id").get<std::string>(), localise(c.at("text").get<std::string>()));
			else
				choices += "<p>" + escape(localise(c.at("text")) + " — " + localise(c.value("reason", "Unavailable"))) +
				           "</p>";
	if (choices != comms_choice_signature_)
	{
		elements_.clear();
		properties_.clear();
		document_->GetElementById("comms-choices")->SetInnerRML(choices);
		comms_choice_signature_ = choices;
	}
	const bool text = (split ? authored_component_visible("conversation-compose") : detail_visible) && thread &&
	                  thread->value("text_allowed", false);
	if (!authored)
		property(document_->GetElementById("comms-compose"), "display", text ? "block" : "none");
	if (text)
	{
		comms_controls_.emplace_back("comms/send", localise("Send"));
		if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("comms/draft"))))
		{
			app.screen_inputs.push_back({id("comms/draft"), localise("Message"), field->GetValue(), false});
			place(app, region, id("comms/draft"));
		}
	}
	for (const auto &[key, label] : comms_controls_)
	{
		const bool list_control = key.starts_with("comms/select/") || key.starts_with("comms/hail/");
		if (narrow && (list_control == detail_visible))
			continue;
		app.screen_controls.emplace_back(id(key), label);
		place(app, region, id(key));
	}
	app.screen_toggles[id("comms/screen")] = model_.is_on_screen(comms_widget_);
	if (detail_visible && !incoming.empty())
		app.screen_readouts.emplace_back("comms-incoming", localise("Incoming message"));
}

// Fit controls after the authored slots have their measured pixel geometry.
// Nested percentage widths can shrink to their intrinsic minimum on a density change.
void fit_conversation_controls(const ConsoleRegion &region)
{
	if (!model_.layout() || !model_.layout()->contains("layout") || !authored_component_visible("conversation-history"))
		return;
	auto *history = document_->GetElementById("comms-history");
	const float width = history->GetClientWidth() / context_->GetDensityIndependentPixelRatio();
	const auto units = [](float value)
	{
		return std::to_string(std::max(0.f, value)) + "dp";
	};
	for (const auto *name : {"comms-header", "comms-choices", "comms-incoming", "comms-compose"})
	{
		auto *panel = document_->GetElementById(name);
		property(panel, "width", units(width));
		property(panel, "box-sizing", "border-box");
	}
	auto *header = document_->GetElementById("comms-header");
	for (int i = 0; i < header->GetNumChildren(); ++i)
		property(header->GetChild(i), "width", units(width));
	Rml::ElementList buttons;
	header->GetElementsByTagName(buttons, "button");
	for (auto *button : buttons)
		property(button, "width", units(std::min(190.f, (width - 24) / 2)));
	buttons.clear();
	document_->GetElementById("comms-choices")->GetElementsByTagName(buttons, "button");
	for (auto *button : buttons)
		property(button, "width", units(width));
	if (auto *draft = document_->GetElementById(id("comms/draft")))
		property(draft, "width", units(width - 140));
	context_->Update();
	elements_.clear();
	for (const auto &[name, label] : comms_controls_)
		place(*app_, region, id(name));
	place(*app_, region, id("comms/draft"));
}
