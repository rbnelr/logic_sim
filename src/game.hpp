#pragma once
#include "common.hpp"
#include "camera.hpp"
#include "engine/dbgdraw.hpp"
#include "logic_sim.hpp"

struct Game {
	SERIALIZE_RESET_ON_LOAD(Game, cam, sim, sim_freq, pause)
	
	Camera2D cam = Camera2D();
	
	DebugDraw dbgdraw;
	
	LogicSim sim;

	float sim_freq = 10.0f;
	float sim_t = 0; // [0,1)  1 means next tick happens, used to animate between prev_state and cur_state
	bool pause = false;
	bool manual_tick = false;

	Game () {
		
	}

	void imgui (Input& I) {
		ZoneScoped;

		if (ImGui::Begin("Misc")) {
			
			if (imgui_Header("Game", true)) {

				cam.imgui("cam");
				sim.imgui(I);

				ImGui::SliderFloat("Sim Freq", &sim_freq, 0.1f, 200, "%.1f", ImGuiSliderFlags_Logarithmic);

				ImGui::Checkbox("Pause [Space]", &pause);
				ImGui::SameLine();
				manual_tick = ImGui::Button("Man. Tick [T]");

				ImGui::PopID();
			}
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
		
		sim.update(I, view, window);

		if (!pause && sim_freq >= 0.1f) {
			
			for (int i=0; i<10 && sim_t >= 1.0f; ++i) {
				
				sim.simulate(I);
				
				sim_t -= 1.0f;
			}
			assert(sim_t >= 0.0f && sim_t < 1.0f);
			
			sim_t += I.dt * sim_freq;
		}
		else if (manual_tick) {
			sim.simulate(I);
			sim_t = 0.5f;
		}
		manual_tick = false;
	}
};
