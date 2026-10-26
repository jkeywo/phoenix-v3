#pragma once
// Shared Workshop/GM skin. Standard ImGui items retain their layout, input and navigation behavior.
void apply_tool_theme(const Catalog &);
bool tool_button(const char *label, const ImVec2 &size = ImVec2(0, 0));
bool tool_small_button(const char *label);
