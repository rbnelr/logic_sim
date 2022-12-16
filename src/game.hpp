#pragma once
#include "common.hpp"
#include "camera.hpp"
#include "engine/dbgdraw.hpp"
#include "logic_sim.hpp"
#include "opengl/renderer.hpp"

struct Game {
	friend SERIALIZE_TO_JSON(Game)   { SERIALIZE_TO_JSON_EXPAND(sim, cam) }
	friend SERIALIZE_FROM_JSON(Game) {
		t.sim_t = 1;
		t.tick_counter = 0;
		
		t.editor = {}; // reset editor
		t.sim = {}; // reset entire sim
		t.sim.reset_chip_view(t.cam);

		if (j.contains("sim")) from_json(j["sim"], t.sim);

		SERIALIZE_FROM_JSON_EXPAND(cam)
	}

	Camera2D cam = Camera2D(0, 12.5f); // init to 12.5f == max(10,6)*1.25 from reset_chip_view() to avoid anim on load
	
	logic_sim::LogicSim sim;
	logic_sim::Editor   editor;

	float sim_freq = 5.0f;
	bool pause = false;
	bool manual_tick = false;

	// [0,1)  1 means next tick happens, used to animate between prev_state and cur_state
	// start out tick at 1 so that there's not a 1 tick pause where we see every gate and wire off
	// (instead negative gates will start out 'sending' their state to the wire instantly)
	float sim_t = 1;

	// Tick counter just for circuit 'debugging'
	// set to zero using imgui input field, let sim run via unpause and now you know how many ticks something takes
	int tick_counter = 0;

	Game () {
		
	}

	void imgui (Input& I) {
		ZoneScoped;

		ImGui::Separator();
			
		if (imgui_Header("Simulation", true)) {

			cam.imgui("View");
			
			ImGui::SliderFloat("Sim Freq", &sim_freq, 0.1f, 200, "%.1f", ImGuiSliderFlags_Logarithmic);

			ImGui::Checkbox("Pause [Space]", &pause);
			ImGui::SameLine();
			manual_tick = ImGui::Button("Man. Tick [T]");

			if (ImGui::SliderFloat("sim_t", &sim_t, 0, 1)) {
				sim_t = min(sim_t, 0.999f); // don't trigger tick while dragging
			}

			{
				ImGui::InputInt("##Tick", &tick_counter, 0,0);
				ImGui::SameLine();
				if (ImGui::Button("0", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
					tick_counter = 0;

				ImGui::SameLine();
				ImGui::TextEx("Tick");
			}
			
			sim.imgui(I);

			ImGui::PopID();
		}

		editor.imgui(sim, cam);
	}

	void update (Window& window, ogl::Renderer& renderer) {
		ZoneScoped;

		auto& I = window.input;

		manual_tick = I.buttons['T'].went_down || manual_tick;
		if (I.buttons[' '].went_down) pause = !pause;

		renderer.view = cam.update(I, (float2)I.window_size);
		
		editor.update(I, sim, renderer);

		if (!pause && sim_freq >= 0.1f) {
			
			for (int i=0; i<10 && sim_t >= 1.0f; ++i) {
				
				sim.simulate(I);
				tick_counter++;
				
				sim_t -= 1.0f;
			}
			assert(sim_t >= 0.0f && sim_t < 1.0f);
			
			sim_t += I.dt * sim_freq;
		}
		else if (manual_tick) {
			sim.simulate(I);
			tick_counter++;

			sim_t = 0.5f;
		}
		manual_tick = false;
		
		// toggle gate after simulate to overwrite simulated state for that gate
		editor.update_toggle_gate(I, sim, window);
	}
};

inline void imgui_style () {
	auto& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	colors[ImGuiCol_Text]                   = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
	colors[ImGuiCol_TextDisabled]           = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
	colors[ImGuiCol_WindowBg]               = ImVec4(0.09f, 0.09f, 0.11f, 0.83f);
	colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_PopupBg]                = ImVec4(0.11f, 0.11f, 0.14f, 0.92f);
	colors[ImGuiCol_Border]                 = ImVec4(0.50f, 0.50f, 0.50f, 0.50f);
	colors[ImGuiCol_BorderShadow]           = ImVec4(0.05f, 0.06f, 0.07f, 0.80f);
	colors[ImGuiCol_FrameBg]                = ImVec4(0.43f, 0.43f, 0.43f, 0.39f);
	colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.47f, 0.47f, 0.69f, 0.40f);
	colors[ImGuiCol_FrameBgActive]          = ImVec4(0.42f, 0.41f, 0.64f, 0.69f);
	colors[ImGuiCol_TitleBg]                = ImVec4(0.27f, 0.27f, 0.54f, 0.83f);
	colors[ImGuiCol_TitleBgActive]          = ImVec4(0.32f, 0.32f, 0.63f, 0.87f);
	colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.40f, 0.40f, 0.80f, 0.20f);
	colors[ImGuiCol_MenuBarBg]              = ImVec4(0.40f, 0.40f, 0.55f, 0.80f);
	colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.20f, 0.25f, 0.30f, 0.60f);
	colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.40f, 0.40f, 0.80f, 0.30f);
	colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.80f, 0.40f);
	colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.41f, 0.39f, 0.80f, 0.60f);
	colors[ImGuiCol_CheckMark]              = ImVec4(0.90f, 0.90f, 0.90f, 0.50f);
	colors[ImGuiCol_SliderGrab]             = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
	colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.41f, 0.39f, 0.80f, 0.60f);
	colors[ImGuiCol_Button]                 = ImVec4(0.35f, 0.40f, 0.61f, 0.62f);
	colors[ImGuiCol_ButtonHovered]          = ImVec4(0.40f, 0.48f, 0.71f, 0.79f);
	colors[ImGuiCol_ButtonActive]           = ImVec4(0.46f, 0.54f, 0.80f, 1.00f);
	colors[ImGuiCol_Header]                 = ImVec4(0.40f, 0.40f, 0.90f, 0.45f);
	colors[ImGuiCol_HeaderHovered]          = ImVec4(0.45f, 0.45f, 0.90f, 0.80f);
	colors[ImGuiCol_HeaderActive]           = ImVec4(0.53f, 0.53f, 0.87f, 0.80f);
	colors[ImGuiCol_Separator]              = ImVec4(0.50f, 0.50f, 0.50f, 0.60f);
	colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.60f, 0.60f, 0.70f, 1.00f);
	colors[ImGuiCol_SeparatorActive]        = ImVec4(0.70f, 0.70f, 0.90f, 1.00f);
	colors[ImGuiCol_ResizeGrip]             = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
	colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.78f, 0.82f, 1.00f, 0.60f);
	colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.78f, 0.82f, 1.00f, 0.90f);
	colors[ImGuiCol_Tab]                    = ImVec4(0.34f, 0.34f, 0.68f, 0.79f);
	colors[ImGuiCol_TabHovered]             = ImVec4(0.45f, 0.45f, 0.90f, 0.80f);
	colors[ImGuiCol_TabActive]              = ImVec4(0.40f, 0.40f, 0.73f, 0.84f);
	colors[ImGuiCol_TabUnfocused]           = ImVec4(0.28f, 0.28f, 0.57f, 0.82f);
	colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.35f, 0.35f, 0.65f, 0.84f);
	colors[ImGuiCol_DockingPreview]         = ImVec4(0.40f, 0.40f, 0.90f, 0.31f);
	colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	colors[ImGuiCol_PlotLines]              = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.27f, 0.27f, 0.38f, 1.00f);
	colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.45f, 1.00f);
	colors[ImGuiCol_TableBorderLight]       = ImVec4(0.26f, 0.26f, 0.28f, 1.00f);
	colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.29f);
	colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
	colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.00f, 0.00f, 1.00f, 0.35f);
	colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight]           = ImVec4(0.45f, 0.45f, 0.90f, 0.80f);
	colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);


	style.WindowPadding     = ImVec2(5,5);
	style.FramePadding      = ImVec2(6,2);
	style.CellPadding       = ImVec2(4,2);
	style.ItemSpacing       = ImVec2(12,3);
	style.ItemInnerSpacing  = ImVec2(3,3);
	style.IndentSpacing     = 18;
	style.GrabMinSize       = 14;

	style.WindowRounding    = 3;
	style.FrameRounding     = 6;
	style.PopupRounding     = 3;
	style.GrabRounding      = 6;

	style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
}
