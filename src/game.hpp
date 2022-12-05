#pragma once
#include "common.hpp"
#include "camera.hpp"
#include "engine/dbgdraw.hpp"
#include "logic_sim.hpp"

struct Game {
	friend SERIALIZE_TO_JSON(Game) { SERIALIZE_TO_JSON_EXPAND(cam, sim, sim_freq, pause) }
	friend SERIALIZE_FROM_JSON(Game) {
		t.editor = {}; // reset editor
		t.sim = {}; // reset entire sim
		SERIALIZE_FROM_JSON_EXPAND(sim, cam, sim_freq, pause);
		t.sim_t = 1;
		t.tick_counter = 0;
	}

	Camera2D cam = Camera2D();
	
	DebugDraw dbgdraw;
	
	logic_sim::LogicSim sim;
	logic_sim::Editor   editor;

	float sim_freq = 10.0f;
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

		if (ImGui::Begin("LogicSim")) {

			cam.imgui("cam");
				
			ImGui::Separator();
			
			if (imgui_Header("Simulation", true)) {
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

				ImGui::PopID();
			}

			sim.imgui(I);
			editor.imgui(sim);
		}
		ImGui::End();
	}

	View3D view;

	float3 sun_dir;

	void update (Window& window) {
		ZoneScoped;

		auto& I = window.input;

		manual_tick = I.buttons['T'].went_down || manual_tick;
		if (I.buttons[' '].went_down) pause = !pause;

		dbgdraw.clear();

		view = cam.update(I, (float2)I.window_size);
		
		editor.update(I, sim, view);

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
