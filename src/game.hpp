#pragma once
#include "common.hpp"
#include "camera.hpp"
#include "engine/dbgdraw.hpp"

struct GateInfo {
	int   inputs;
	float input_x;

	int   outputs;
	float output_x;

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
	/* GT_NOT  */ { 1, 0.2f,  1, 0.88f, lrgba(   0,    0,    1,1) },
	/* GT_AND  */ { 2, 0.2f,  1, 0.78f, lrgba(   1,    0,    0,1) },
	/* GT_OR   */ { 2, 0.2f,  1, 0.78f, lrgba(   1, 0.5f,    0,1) },
	/* GT_XOR  */ { 2, 0.2f,  1, 0.78f, lrgba(   0,    1,    0,1) },
	/* GT_NAND */ { 2, 0.2f,  1, 0.88f, lrgba(0.5f,    1,    0,1) },
	/* GT_NOR  */ { 2, 0.2f,  1, 0.88f, lrgba(   0,    1, 0.5f,1) },
};

struct LogicSim {
	SERIALIZE(LogicSim, gates, snapping_size, snapping)

	static constexpr float IO_SIZE = 0.2f;

//// Logic Sim Datas
	struct Gate;

	struct WireConnection {
		Gate* gate;
		int   io_idx;
	};

	struct Gate {
		GateType type;
		float2 pos;

		std::vector<WireConnection> inputs;

		void init_inputs () {
			inputs.assign(gate_info[type].inputs, WireConnection{}); // no connections
			inputs.shrink_to_fit();
		}

		float2 get_io_pos (int i, int count, float x, float y, float h) {
			h -= 0.2f*2;
			y += 0.2f;
			
			float step = h / (float)count;
			return float2(x, y + step * ((float)i + 0.5f));
		}
		float2 get_input_pos (int i) {
			assert(type >= GT_NULL);
			return get_io_pos(i, gate_info[type].inputs, pos.x + gate_info[type].input_x, pos.y, 1);
		}
		float2 get_output_pos (int i) {
			assert(type >= GT_NULL);
			return get_io_pos(i, gate_info[type].outputs, pos.x + gate_info[type].output_x, pos.y, 1);
		}
	};

	// Wrapper struct so I can manually write _just_ this part of the serialization
	struct Gates {

		//SERIALIZE(Gate, type, pos)
		friend void to_json(nlohmann::ordered_json& j, const Gates& gates) {
			json list = {};

			for (auto& gp : gates.gates) {
				auto& gate = *gp;

				assert(gate.type > GT_NULL);

				json j_inputs = {};
				for (auto& inp : gate.inputs) {
					int idx = inp.gate == nullptr ? -1 : indexof(gates.gates, inp.gate, [] (std::unique_ptr<Gate> const& ptr, Gate* r) { return ptr.get() == r; });
					j_inputs.emplace_back(json{ {"g", idx}, {"i", inp.io_idx} });
				}

				json j = {
					{"type", gate.type},
					{"pos",  gate.pos},
					{"inputs", std::move(j_inputs)},
				};

				list.emplace_back(std::move(j));
			}

			j["gates"] = std::move(list);
		}
		friend void from_json(const nlohmann::ordered_json& j, Gates& gates) {
			gates.gates.clear();

			if (!j.contains("gates")) return;
			json list = j.at("gates");

			gates.gates.resize(list.size());
			for (size_t i=0; i<list.size(); ++i)
				gates.gates[i] = std::make_unique<Gate>();
			
			for (size_t i=0; i<list.size(); ++i) {
				json& gj = list[i];
				auto& gp = gates.gates[i];

				gj.at("type").get_to(gp->type);
				gj.at("pos").get_to(gp->pos);
				gp->init_inputs();

				if (gj.contains("inputs")) {
					json inputsj = gj.at("inputs");
					
					assert(gp->inputs.size() == inputsj.size());
					
					for (int i=0; i<gp->inputs.size(); ++i) {
						int gate_idx = inputsj[i].at("g").get<int>();
						int outp_idx = inputsj[i].at("i").get<int>();

						if (gate_idx >= 0 && gate_idx < (int)gates.gates.size()) {
							gp->inputs[i].gate = gates.gates[gate_idx].get();
							gp->inputs[i].io_idx = outp_idx;
						}
					}
				}
			}
		}
		
		std::vector<std::unique_ptr<Gate>> gates;
		
		Gate& operator[] (int idx) {
			assert(idx >= 0 && idx < (int)gates.size());
			return *gates[idx];
		}
		int size () { return (int)gates.size(); }

		void add_gate (Gate&& gate) {
			ZoneScoped;

			assert(gate.type > GT_NULL);

			gate.init_inputs();

			auto new_gate = std::make_unique<Gate>();
			*new_gate = std::move(gate);
			gates.emplace_back(std::move(new_gate));
		}
		
		void _remove_wires (Gate* src) {
			for (auto& g : gates) {
				for (auto& inp : g->inputs) {
					if (inp.gate == src) inp = {};
				}
			}
		}
		void remove_gate (Gate* gate) {
			ZoneScoped;

			int idx = indexof(gates, gate, [] (std::unique_ptr<Gate> const& ptr, Gate* r) { return ptr.get() == r; });
			assert(idx >= 0);
			
			// inputs to this gate simply disappear
			// output connections have to be manually removes to avoid ptr use after free
			_remove_wires(gate);
			
			// replace gate to be removed with last gate and shrink vector by one to not leave holes
			gates[idx] = nullptr; // delete gate
			gates[idx] = std::move(gates.back()); // swap last vector element into this one
			gates.pop_back(); // shrink vector
		}

		void replace_wire (WireConnection src, WireConnection dst) {
			assert(dst.gate);
			assert(dst.io_idx < gate_info[dst.gate->type].inputs);
			if (src.gate) assert(src.io_idx < gate_info[src.gate->type].outputs);
			dst.gate->inputs[dst.io_idx] = src; // if src.gate == null, this effectively removes the connection
		}
	};

	Gates gates;

//// Editor logic
	Gate gate_preview = { GT_NULL };

	struct WirePreview {
		WireConnection dst = { nullptr, 0 }; // if gate=null then to unconnected_pos
		WireConnection src = { nullptr, 0 }; // if gate=null then from unconnected_pos

		float2         unconnected_pos; // = cursor pos

		bool was_dst;
	};
	WirePreview wire_preview = {};

	struct Selection {
		enum Type {
			NONE=0,
			TO_PLACE,
			GATE,
			INPUT,
			OUTPUT,
		};
		Type type;
		Gate* gate;
		int io_idx;

		operator bool () {
			return type != NONE;
		}
	};
	Selection sel = {};

	float snapping_size = 0.25f;
	bool snapping = true;
	
	bool _cursor_valid;
	float3 _snapped_cursor_pos;
	float3 _cursor_pos;
	
	bool dragging = false;
	float2 drag_offs;

	void imgui (Input& I) {
		if (imgui_Header("LogicSim", true)) {

			ImGui::Checkbox("snapping", &snapping);
			ImGui::SameLine();
			ImGui::InputFloat("###snapping_size", &snapping_size);

			if (ImGui::BeginTable("Gates", 2, ImGuiTableFlags_Borders)) {
				
				ImGui::TableNextColumn();
				if (ImGui::Selectable("AND" , gate_preview.type == GT_AND )) gate_preview.type = GT_AND;
				ImGui::TableNextColumn();
				if (ImGui::Selectable("NAND", gate_preview.type == GT_NAND)) gate_preview.type = GT_NAND;
				
				ImGui::TableNextColumn();
				if (ImGui::Selectable("OR"  , gate_preview.type == GT_OR  )) gate_preview.type = GT_OR;
				ImGui::TableNextColumn();
				if (ImGui::Selectable("NOR" , gate_preview.type == GT_NOR )) gate_preview.type = GT_NOR;
				
				ImGui::TableNextColumn();
				if (ImGui::Selectable("NOT" , gate_preview.type == GT_NOT )) gate_preview.type = GT_NOT;
				ImGui::TableNextColumn();
				if (ImGui::Selectable("XOR" , gate_preview.type == GT_XOR )) gate_preview.type = GT_XOR;

				ImGui::EndTable();
			}

			ImGui::Text("Gates: %d", (int)gates.size());

			ImGui::PopID();
		}
	}

	void update (Input& I, View3D& view) {
		ZoneScoped;

		// unselect gate if imgui to-be-placed is selected
		if (gate_preview.type > GT_NULL)
			sel = { Selection::TO_PLACE };

		// deselect gate via right click ie. get rid of to-be-placed gate preview
		if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
			sel = {};
			gate_preview.type = GT_NULL;
			wire_preview = {};
		}

		_cursor_valid = view.cursor_ray(I, &_cursor_pos);

		// snap cursor
		_snapped_cursor_pos = _cursor_pos;
		if (snapping)
			_snapped_cursor_pos = round(_cursor_pos / snapping_size) * snapping_size;
		
		if (_cursor_valid) {
			// move to-be-placed gate preview with cursor
			gate_preview.pos = _snapped_cursor_pos - 0.5f;

			// place gate on left click
			if (gate_preview.type > GT_NULL && I.buttons[MOUSE_BUTTON_LEFT].went_down) {
				gates.add_gate(std::move(gate_preview)); // preview becomes real gate
				// preview of gate still exists
			}
		}
	}
	
	bool gate_hitbox (Gate& gate, float2 point) {
		return point.x >= gate.pos.x && point.x < gate.pos.x + 1 &&
		       point.y >= gate.pos.y && point.y < gate.pos.y + 1;
	}
	int gate_input_hitbox (Gate& gate, float2 point) {
		for (int i=0; i<gate_info[gate.type].inputs; ++i) {
			float2 p = gate.get_input_pos(i) - IO_SIZE/2;
			
			if ( point.x >= p.x && point.x < p.x + IO_SIZE &&
			     point.y >= p.y && point.y < p.y + IO_SIZE)
				return i;
		}
		return -1;
	}
	int gate_output_hitbox (Gate& gate, float2 point) {
		for (int i=0; i<gate_info[gate.type].outputs; ++i) {
			float2 p = gate.get_output_pos(i) - IO_SIZE/2;

			if ( point.x >= p.x && point.x < p.x + IO_SIZE &&
				 point.y >= p.y && point.y < p.y + IO_SIZE)
				return i;
		}
		return -1;
	}

	Selection gate_hitboxes (Gate& gate, int gate_idx, float2 point) {
		Selection s = {};

		// do here for efficincy (avoid iterating twice)
		if (gate_hitbox(gate, _cursor_pos)) {
			s = { Selection::GATE, &gate };
		}

		int inp  = gate_input_hitbox(gate, _cursor_pos);
		if (inp >= 0) {
			s = { Selection::INPUT, &gate, inp };
		}
		int outp = gate_output_hitbox(gate, _cursor_pos);
		if (outp >= 0) {
			s = { Selection::OUTPUT, &gate, outp };
		}

		return s;
	}

	void highlight (Selection s, lrgba col, DebugDraw& dbgdraw) {
		if (s && s.type != Selection::TO_PLACE) {
			auto& gate = *s.gate;
			if (s.type == Selection::GATE) {
				dbgdraw.wire_quad(float3(gate.pos, 5.0f), 1, col);
			}
			else if (s.type == Selection::INPUT) {
				dbgdraw.wire_quad(float3(gate.get_input_pos(s.io_idx) - IO_SIZE/2, 5.0f), IO_SIZE, col);
			}
			else {
				dbgdraw.wire_quad(float3(gate.get_output_pos(s.io_idx) - IO_SIZE/2, 5.0f), IO_SIZE, col);
			}
		}
	}

	void simulate (Input& I, DebugDraw& dbgdraw) {
		ZoneScoped;

		bool try_select = sel.type != Selection::TO_PLACE
			&& _cursor_valid && I.buttons[MOUSE_BUTTON_LEFT].went_down;
		if (try_select)
			sel = {}; // unselect if click and nothing was hit

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

		// drag selected gate via left click
		if (sel.type == Selection::GATE && I.buttons[MOUSE_BUTTON_LEFT].is_down) {
			if (!dragging) {
				drag_offs = _snapped_cursor_pos - sel.gate->pos;
				dragging = true;
			}
			else {
				sel.gate->pos = _snapped_cursor_pos - drag_offs;
			}
		}

		// drag selected IO via left click
		if ((sel.type == Selection::INPUT || sel.type == Selection::OUTPUT) && I.buttons[MOUSE_BUTTON_LEFT].is_down) {
			if (!dragging) { // begin dragging
				// reset wire connection preview
				wire_preview.dst = {};
				wire_preview.src = {};
				// remember where dragging started

				wire_preview.was_dst = sel.type == Selection::INPUT;
				if (sel.type == Selection::INPUT) wire_preview.dst = { sel.gate, sel.io_idx };
				else                              wire_preview.src = { sel.gate, sel.io_idx };

				// begin dragging
				dragging = true;
			}
			if (dragging) { // while dragging
				// if other end is unconnected 'connect' it with cursor
				wire_preview.unconnected_pos = _cursor_pos;

				if (highlighted && (highlighted.type == Selection::INPUT || highlighted.type == Selection::OUTPUT)) {

					if (highlighted.type == Selection::OUTPUT && wire_preview.was_dst) {
						wire_preview.src.gate   = highlighted.gate;
						wire_preview.src.io_idx = highlighted.io_idx;
					}
					if (highlighted.type == Selection::INPUT && !wire_preview.was_dst) {
						wire_preview.dst.gate   = highlighted.gate;
						wire_preview.dst.io_idx = highlighted.io_idx;
					}
				}
				else {
					if (wire_preview.was_dst) wire_preview.src = {};
					else                      wire_preview.dst = {};
				}
			}
		}

		// stop dragging
		if (dragging && I.buttons[MOUSE_BUTTON_LEFT].went_up) {
			dragging = false;

			// if wire_preview was a connected on both ends, make it a real connection
			if ((wire_preview.src.gate && wire_preview.dst.gate) || wire_preview.was_dst) {
				gates.replace_wire(wire_preview.src, wire_preview.dst);
				if (wire_preview.was_dst) {
					// dragged from input, but did not connect
					// overwrite old connection with new one
					assert(wire_preview.dst.gate);
				}
				gates.replace_wire(wire_preview.src, wire_preview.dst);
			} else {
				// dragged from output, but did not connect
				// do nothing
			}
			wire_preview = {};
		}

		highlight(highlighted, lrgba(0.25f,0.25f,0.25f,1), dbgdraw);
		highlight(sel, lrgba(1,1,1,1), dbgdraw);

		// remove gates via DELETE key
		if (sel.type == Selection::GATE && I.buttons[KEY_DELETE].went_down) {
			gates.remove_gate(sel.gate);
			sel = {};
			wire_preview = {}; // just be to sure no stale pointers exist
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
