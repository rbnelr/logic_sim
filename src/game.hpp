#pragma once
#include "common.hpp"
#include "camera.hpp"
#include "engine/dbgdraw.hpp"

struct GateInfo {
	int inputs;
	int outputs;

	lrgba color;
};

enum GateType {
	GT_NULL  = -1,
	GT_NOT  = 0,
	GT_AND  = 1,
	GT_OR   = 2,
	GT_XOR  = 3,
	GT_NAND = 4,
	GT_NOR  = 5,
};
constexpr GateInfo gate_info[] = {
	/* GT_NOT  */ { 1, 1, lrgba(   0,    0,    1,1) },
	/* GT_AND  */ { 2, 1, lrgba(   1,    0,    0,1) },
	/* GT_OR   */ { 2, 1, lrgba(   1, 0.5f,    0,1) },
	/* GT_XOR  */ { 2, 1, lrgba(   0,    1,    0,1) },
	/* GT_NAND */ { 2, 1, lrgba(0.5f,    1,    0,1) },
	/* GT_NOR  */ { 2, 1, lrgba(   0,    1, 0.5f,1) },
};

struct LogicSim {
	SERIALIZE(LogicSim, gates, snapping_size, snapping)

	struct Gate {
		SERIALIZE(Gate, type, pos)

		GateType type;
		float2 pos;
	};

	std::vector<Gate> gates;
	
	Gate gate_to_place = { GT_NULL };

	struct Selection {
		enum Type {
			NONE=0,
			TO_PLACE,
			GATE,
			INPUT,
			OUTPUT,
		};
		Type type;
		int gate_idx;
		int io_idx;

		operator bool () {
			return type != NONE;
		}
	};
	Selection sel = {};

	float snapping_size = 0.25f;
	bool snapping = true;
	
	bool _cursor_valid;
	float3 _cursor_pos;
	
	void add_gate (Gate gate) {
		assert(gate.type > GT_NULL);
		gates.push_back(gate_to_place);
	}
	void remove_gate (int gate_idx) {
		assert(gate_idx >= 0);
		assert(gates.size() > 0);
		// replace gate to be removed with last gate and shrink vector by one to not leave holes
		gates[gate_idx] = gates.back();
		gates.pop_back();
	}

	void imgui (Input& I) {
		if (imgui_Header("LogicSim", true)) {

			ImGui::Checkbox("snapping", &snapping);
			ImGui::SameLine();
			ImGui::InputFloat("###snapping_size", &snapping_size);

			if (ImGui::BeginTable("Gates", 2, ImGuiTableFlags_Borders)) {
				
				ImGui::TableNextColumn();
				if (ImGui::Selectable("AND" , gate_to_place.type == GT_AND )) gate_to_place.type = GT_AND;
				ImGui::TableNextColumn();
				if (ImGui::Selectable("NAND", gate_to_place.type == GT_NAND)) gate_to_place.type = GT_NAND;
				
				ImGui::TableNextColumn();
				if (ImGui::Selectable("OR"  , gate_to_place.type == GT_OR  )) gate_to_place.type = GT_OR;
				ImGui::TableNextColumn();
				if (ImGui::Selectable("NOR" , gate_to_place.type == GT_NOR )) gate_to_place.type = GT_NOR;
				
				ImGui::TableNextColumn();
				if (ImGui::Selectable("NOT" , gate_to_place.type == GT_NOT )) gate_to_place.type = GT_NOT;
				ImGui::TableNextColumn();
				if (ImGui::Selectable("XOR" , gate_to_place.type == GT_XOR )) gate_to_place.type = GT_XOR;

				ImGui::EndTable();
			}

			ImGui::Text("Gates: %d", (int)gates.size());

			ImGui::PopID();
		}
	}

	void update (Input& I, View3D& view) {
		// unselect gate if imgui to-be-placed is selected
		if (gate_to_place.type > GT_NULL)
			sel = { Selection::TO_PLACE };

		// deselect gate via right click ie. get rid of to-be-placed gate preview
		if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
			sel = {};
			gate_to_place.type = GT_NULL;
		}

		_cursor_valid = view.cursor_ray(I, &_cursor_pos);

		if (_cursor_valid) {
			// move to-be-placed gate preview with cursor
			gate_to_place.pos = _cursor_pos - 0.5f;

			// snap gate
			if (snapping)
				gate_to_place.pos = round(gate_to_place.pos / snapping_size) * snapping_size;

			// place gate on left click
			if (gate_to_place.type > GT_NULL && I.buttons[MOUSE_BUTTON_LEFT].went_down) {
				add_gate(gate_to_place); // preview becomes real gate
				// preview of gate still exists
			}
		}
	}

	bool gate_hitbox (Gate& gate, float2 point) {
		return point.x >= gate.pos.x && point.x < gate.pos.x + 1 &&
		       point.y >= gate.pos.y && point.y < gate.pos.y + 1;
	}
	int gate_input_hitbox (Gate& gate, float2 point) {
		int count = gate_info[gate.type].inputs;
		float sizey = 1.0f / (float)count;

		for (int i=0; i<count; ++i) {
			float x = gate.pos.x - 0.125f;
			float y = gate.pos.y + sizey * (float)i;

			if ( point.x >= x && point.x < x + 0.5f &&
			     point.y >= y && point.y < y + sizey)
				return i;
		}

		return -1;
	}
	int gate_output_hitbox (Gate& gate, float2 point) {
		int count = gate_info[gate.type].outputs;
		float sizey = 1.0f / (float)count;

		for (int i=0; i<count; ++i) {
			float x = gate.pos.x + 0.625f;
			float y = gate.pos.y + sizey * (float)i;

			if ( point.x >= x && point.x < x + 0.5f &&
			     point.y >= y && point.y < y + sizey)
				return i;
		}

		return -1;
	}

	Selection gate_hitboxes (Gate& gate, int gate_idx, float2 point) {
		Selection s = {};

		// do here for efficincy (avoid iterating twice)
		if (gate_hitbox(gate, _cursor_pos)) {
			s = { Selection::GATE, gate_idx };
		}

		int inp = gate_input_hitbox(gate, _cursor_pos);
		if (inp >= 0) {
			s = { Selection::INPUT, gate_idx, inp };
		}
		int outp = gate_output_hitbox(gate, _cursor_pos);
		if (outp >= 0) {
			s = { Selection::OUTPUT, gate_idx, outp };
		}

		return s;
	}

	void simulate (Input& I, DebugDraw& dbgdraw) {
		
		bool try_select = sel.type != Selection::TO_PLACE
			&& _cursor_valid && I.buttons[MOUSE_BUTTON_LEFT].went_down;

		Selection highlighted = {};

		for (int i=0; i<(int)gates.size(); ++i) {
			auto& g = gates[i];
			assert(g.type > GT_NULL);
			
			// do here for efficincy (avoid iterating twice)
			Selection hit = gate_hitboxes(g, i, _cursor_pos);
			if (hit) {
				if (try_select) { // gate was clicked
					sel = hit;
				} else { // gate was hovered
					highlighted = hit;
				}
			}
		}

		if (sel && sel.type != Selection::TO_PLACE) {
			auto& gate = gates[sel.gate_idx];
			float2 pos, size;
			if (sel.type == Selection::GATE) {
				pos = gate.pos;
				size = 1;
			}
			else if (sel.type == Selection::INPUT) {
				size = float2(0.5f, 1.0f / gate_info[gate.type].inputs);
				pos.x = gate.pos.x - 0.125f;
				pos.y = gate.pos.y + (float)sel.io_idx * size.y;
			}
			else {
				size = float2(0.5f, 1.0f / gate_info[gate.type].outputs);
				pos.x = gate.pos.x + 0.625f;
				pos.y = gate.pos.y + (float)sel.io_idx * size.y;
			}
			dbgdraw.wire_quad(float3(pos, 5.0f), size, lrgba(1,1,1,1));
		}
		if (highlighted && highlighted.type != Selection::TO_PLACE) {
			auto& gate = gates[highlighted.gate_idx];
			float2 pos, size;
			if (highlighted.type == Selection::GATE) {
				pos = gate.pos;
				size = 1;
			}
			else if (highlighted.type == Selection::INPUT) {
				size = float2(0.5f, 1.0f / gate_info[gate.type].inputs);
				pos.x = gate.pos.x - 0.125f;
				pos.y = gate.pos.y + (float)highlighted.io_idx * size.y;
			}
			else {
				size = float2(0.5f, 1.0f / gate_info[gate.type].outputs);
				pos.x = gate.pos.x + 0.625f;
				pos.y = gate.pos.y + (float)highlighted.io_idx * size.y;
			}
			dbgdraw.wire_quad(float3(pos, 5.0f), size, lrgba(0.25f,0.25f,0.25f,1));
		}

		// remove gates via DELETE key
		if (sel.type == Selection::GATE && I.buttons[KEY_DELETE].went_down) {
			remove_gate(sel.gate_idx);
			sel = {};
		}
	}
};

struct Game {
	SERIALIZE(Game, cam, sim)
	
	Camera2D cam = Camera2D();

	DebugDraw dbgdraw;
	
	LogicSim sim;

	Game () {
		
	}

	void imgui (Input& I) {
		ZoneScoped;

		if (ImGui::Begin("Misc")) {
			
			if (imgui_Header("Game", true)) {

				cam.imgui("cam");
				sim.imgui(I);

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

		dbgdraw.clear();

		view = cam.update(I, (float2)I.window_size);
		
		sim.update(I, view);
		sim.simulate(I, dbgdraw);
	}
};
