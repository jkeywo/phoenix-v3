#include "../support/assertions.hpp"
#include <raylib.h>
#include <rlgl.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
import phoenix.ui.rml;
import phoenix.ui.information_widgets;
import phoenix.presentation;
import phoenix.navigation;
import phoenix.console;
using namespace phoenix;
using phoenix::test::require;
int main()
{
	try
	{
		SetConfigFlags(FLAG_WINDOW_HIDDEN);
		open_window();
		UiRenderer renderer;
		auto *context = Rml::CreateContext("message-smoke", {900, 700}, &renderer);
		auto *document = context->LoadDocumentFromMemory(R"(<rml><head><style>
body {width:100%;height:100%;font-family:LatoLatin;font-size:20px;color:#f2eddf;background:#172124;}
#history {position:absolute;left:12px;right:12px;top:52px;bottom:12px;overflow-y:auto;overflow-x:hidden;}
button {height:36px;background:#eee;color:#111;}
scrollbarvertical {width:14px;background:#263638;}
scrollbarvertical sliderbar {min-height:32px;background:#ccc2af;}
scrollbarvertical slidertrack {width:14px;}
</style></head><body><button id="focus">Keep reading</button><div id="history"></div></body></rml>)");
		document->Show();
		auto *history = document->GetElementById("history");
		Application app;
		std::vector<ConsoleMessageView> messages{
		    {"one", "Helm", "Course laid in.", "messages/console-1.png", {}},
		    {"two",
		     "Engineering",
		     "Power is available. We can maintain this speed while the damaged coolant loop is isolated. Please give "
		     "us time to complete the repairs before requesting full thrust.",
		     "messages/console-2.png",
		     {}},
		    {"three", "Science", "Signal received: café, mañana, Straße. مرحبا بكم", "messages/console-3.png", {}},
		    {"four",
		     "Morgan",
		     "<This is text, not markup.>\nA second line remains readable.",
		     "messages/console-4.png",
		     {}}};
		const auto frame = [&]
		{
			app.screen_readouts.clear();
			message_ui::refresh(*document, *history, messages, app.session ? &app.session->catalog() : nullptr,
			                    app.screen_readouts);
			context->Update();
			BeginDrawing();
			ClearBackground(BLACK);
			renderer.viewport({0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())});
			context->Render();
			renderer.finish();
			EndDrawing();
		};
		const auto capture = [&](const char *name)
		{
			frame();
			frame();
#ifdef __EMSCRIPTEN__
			// clang-format off
			EM_ASM({
                const name = UTF8ToString($0); const data = Module.canvas.toDataURL('image/png');
                Module.messageCaptures ||= {}; Module.messageCaptures[name] = data;
                const image = document.createElement('img'); image.src = data; image.alt = name;
                image.style.cssText = 'display:block;max-width:100%;margin:16px auto'; document.body.appendChild(image);
            }, name);
			// clang-format on
#else
			TakeScreenshot(name);
#endif
		};
		SetWindowSize(900, 700);
		capture("messages-wide.png");
		context->SetDimensions({360, 740});
		SetWindowSize(360, 740);
		document->SetProperty("font-size", "26px");
		capture("messages-narrow.png");
		auto *one = document->GetElementById("one");
		auto *two = document->GetElementById("two");
		require(one->GetOffsetWidth() > 200 && one->GetOffsetHeight() < 200 &&
		            two->GetAbsoluteOffset(Rml::BoxArea::Border).y >=
		                one->GetAbsoluteOffset(Rml::BoxArea::Border).y + one->GetOffsetHeight(),
		        "narrow bubbles retain usable width and separate vertical rows");
		document->GetElementById("focus")->Focus();
		history->SetScrollTop(110);
		context->Update();
		const auto scroll = history->GetScrollTop();
		auto *first = document->GetElementById("one");
		messages.push_back({"five", "Comms", "A new message arrives while you read.", "messages/console-4.png", {}});
		frame();
		frame();
		require(document->GetElementById("one") == first && context->GetFocusElement()->GetId() == "focus" &&
		            history->GetScrollTop() == scroll,
		        "arrival retains row identity, focus and scroll");
		require(app.screen_readouts.size() == messages.size() &&
		            app.screen_readouts[0].second == messages[0].accessible_text(),
		        "visual messages and screen-reader labels use the same model");
		capture("messages-scrolled.png");
		// Exercise the extracted production popup lifecycle with permitted presentation messages.
		document->Close();
		document = context->LoadDocumentFromMemory(R"(<rml><head><style>
body {width:100%;height:100%;font-family:LatoLatin;font-size:20px;color:#f2eddf;background:#172124;}
#messages-popup {position:absolute;left:12px;right:12px;top:52px;bottom:12px;}
#message-history {height:500px;overflow-y:auto;}
button {height:36px;background:#eee;color:#111;}
</style></head><body><div id="messages-control"/><div id="messages-popup"><div id="messages-header"/><div id="message-history"/></div><div id="message-arrival"/></body></rml>)");
		document->Show();
		ConsoleDocument dom;
		dom.bind(*document);
		MessageHistoryPanel popup;
		double popup_now = 0;
		std::string popup_scope = "session/one";
		const auto refresh_popup = [&]
		{
			app.screen_controls.clear();
			app.screen_readouts.clear();
			app.control_placements.clear();
			return popup.refresh(messages, {dom,
			                                *context,
			                                {app.screen_controls, app.screen_readouts, app.screen_inputs,
			                                 app.screen_toggles, app.control_placements},
			                                {0, 0, 0, 360, 740},
			                                nullptr,
			                                "popup/",
			                                popup_scope,
			                                true,
			                                false,
			                                popup_now,
			                                8});
		};
		popup.advance_clock(*context, popup_now);
		require(!refresh_popup().cue && !popup.refresh_due(100), "initial history establishes a silent baseline");
		messages.push_back(
		    {"urgent", "Engineering", "Coolant flow is failing.", "messages/console-2.png", {}, "Support", 6, 10, 2});
		popup_now = 1;
		popup.advance_clock(*context, popup_now);
		const auto notice = refresh_popup();
		require(notice.cue && notice.announcement == messages.back().accessible_text() && !popup.refresh_due(1),
		        "new urgent messages return one cue and matching accessible announcement");
		popup.action(dom, "messages");
		popup.advance_clock(*context, 10);
		require(!popup.refresh_due(10), "opening the history pauses the arrival deadline");
		popup.action(dom, "messages-close");
		popup.action(dom, "messages-arrival");
		popup_now = 10;
		require(!refresh_popup().cue && popup.open(), "arrival opens history without replaying a cue");
		context->Update();
		BeginDrawing();
		ClearBackground(BLACK);
		renderer.viewport({0, 0, 360, 740});
		context->Render();
		renderer.finish();
		EndDrawing();
		TakeScreenshot("messages-popup.png");
		popup.action(dom, "messages-filter/Support");
		refresh_popup();
		popup.action(dom, "messages-close");
		refresh_popup();
		require(!popup.open() && context->GetFocusElement() && context->GetFocusElement()->GetId() == "popup/messages",
		        "closing history restores focus to its stable toggle");
		popup_scope = "session/two";
		require(!refresh_popup().cue && !popup.refresh_due(100), "reconnect suppresses historical arrival replay");
		Rml::RemoveContext("message-smoke");
		close_window();
		std::cout << "Message visual smoke passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
