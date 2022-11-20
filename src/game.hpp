#pragma once
#include "common.hpp"
#include "camera.hpp"
#include "engine/dbgdraw.hpp"

struct GateInfo {
	const char* name;

	int   inputs;
	float input_x;

	int   outputs;
	float output_x;

	lrgba color;
};

enum GateType {
	GT_NULL  = -1,
	GT_BUF  = 0,
	GT_NOT  = 1,
	GT_AND  = 2,
	GT_NAND = 3,
	GT_OR   = 4,
	GT_NOR  = 5,
	GT_XOR  = 6,
};
constexpr GateInfo gate_info[] = {
	/* GT_BUF  */ { "Buffer", 1, 0.2f,  1, 0.78f, lrgba(0.5f, 0.5f,0.75f,1) },
	/* GT_NOT  */ { "Not"   , 1, 0.2f,  1, 0.88f, lrgba(   0,    0,    1,1) },
	/* GT_AND  */ { "And"   , 2, 0.2f,  1, 0.78f, lrgba(   1,    0,    0,1) },
	/* GT_NAND */ { "Nand"  , 2, 0.2f,  1, 0.88f, lrgba(0.5f,    1,    0,1) },
	/* GT_OR   */ { "Or"    , 2, 0.2f,  1, 0.78f, lrgba(   1, 0.5f,    0,1) },
	/* GT_NOR  */ { "Nor"   , 2, 0.2f,  1, 0.88f, lrgba(   0,    1, 0.5f,1) },
	/* GT_XOR  */ { "Xor"   , 2, 0.2f,  1, 0.78f, lrgba(   0,    1,    0,1) },
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
		float2   pos;

		struct Input {
			Gate* gate;
			int   io_idx;
			std::vector<float2> points;
		};
		std::vector<Input> inputs;

		uint8_t state; // double buffer
		
		void init () {
			inputs.assign(gate_info[type].inputs, Input{}); // no connections
			inputs.shrink_to_fit();
		}

		float2 get_io_pos (int i, int count, float x, float y, float h) {
			h -= 0.0f*2;
			y += 0.0f;
			
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
		
		bool hitbox (float2 point) {
			return point.x >= pos.x && point.x < pos.x + 1 &&
				   point.y >= pos.y && point.y < pos.y + 1;
		}
		int input_hitbox (float2 point) {
			for (int i=0; i<gate_info[type].inputs; ++i) {
				float2 p = get_input_pos(i) - IO_SIZE/2;
			
				if ( point.x >= p.x && point.x < p.x + IO_SIZE &&
					 point.y >= p.y && point.y < p.y + IO_SIZE)
					return i;
			}
			return -1;
		}
		int output_hitbox (float2 point) {
			for (int i=0; i<gate_info[type].outputs; ++i) {
				float2 p = get_output_pos(i) - IO_SIZE/2;

				if ( point.x >= p.x && point.x < p.x + IO_SIZE &&
					 point.y >= p.y && point.y < p.y + IO_SIZE)
					return i;
			}
			return -1;
		}

	};

	// Wrapper struct so I can manually write _just_ this part of the serialization
	struct Gates {

		int cur_buf = 0; // cur state double buffering index

		//SERIALIZE(Gate, type, pos)
		friend void to_json(nlohmann::ordered_json& j, const Gates& gates) {
			json list = {};
			
			uint8_t cur_smask  = 1u << gates.cur_buf;
			
			for (auto& gp : gates.gates) {
				auto& gate = *gp;

				assert(gate.type > GT_NULL);

				json j_inputs = {};
				for (auto& inp : gate.inputs) {
					int idx = inp.gate == nullptr ? -1 : indexof(gates.gates, inp.gate, [] (std::unique_ptr<Gate> const& ptr, Gate* r) { return ptr.get() == r; });
					json j = { {"g", idx}, {"i", inp.io_idx} };
					j["points"] = inp.points;
					
					j_inputs.emplace_back(std::move(j));
				}

				json j = {
					{"type", gate.type},
					{"pos",  gate.pos},
					{"state", (gate.state & cur_smask) != 0 },
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
				gj.at("state").get_to(gp->state);

				gp->init();

				if (gj.contains("inputs")) {
					json inputsj = gj.at("inputs");
					
					assert(gp->inputs.size() == inputsj.size());
					
					for (int i=0; i<gp->inputs.size(); ++i) {
						int gate_idx = inputsj[i].at("g").get<int>();
						int outp_idx = inputsj[i].at("i").get<int>();

						if (gate_idx >= 0 && gate_idx < (int)gates.gates.size()) {
							gp->inputs[i].gate = gates.gates[gate_idx].get();
							gp->inputs[i].io_idx = outp_idx;

							if (inputsj[i].contains("points")) inputsj[i].at("points").get_to(gp->inputs[i].points);
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

			gate.init();

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
			gates[idx] = std::move(gates.back()); // swap last vector element into this one (works even if this was the last element)
			gates.pop_back(); // shrink vector
		}
		
		void add_wire (WireConnection src, WireConnection dst, std::vector<float2>&& points) {
			assert(dst.gate);
			assert(src.gate);
			assert(dst.io_idx < gate_info[dst.gate->type].inputs);
			assert(src.io_idx < gate_info[src.gate->type].outputs);
			dst.gate->inputs[dst.io_idx] = { src.gate, src.io_idx, std::move(points) };
		}

	};

	Gates gates;

//// Editor logic
	
	enum EditMode {
		EM_VIEW,  // Can toggle inputs with LMB
		EM_EDIT,  // Can move/delete gates and connect wires
		EM_PLACE, // Can place down gates selected in imgui (or per gate picker to pick hovered type?)
	};
	EditMode mode = EM_VIEW;

	Gate gate_preview = { GT_NULL };

	struct WirePreview {
		WireConnection dst = { nullptr, 0 }; // if gate=null then to unconnected_pos
		WireConnection src = { nullptr, 0 }; // if gate=null then from unconnected_pos

		std::vector<float2> points;

		float2         unconnected_pos = 0; // = cursor pos

		bool was_dst = false;
	};
	WirePreview wire_preview = {};
	
	struct Selection {
		enum Type {
			NONE=0,
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
	Selection hover = {};

	float snapping_size = 0.125f;
	bool snapping = true;
	
	float2 snap (float2 pos) {
		return snapping ? round(pos / snapping_size) * snapping_size : pos;
	}

	bool _cursor_valid;
	float3 _cursor_pos;
	
	bool editing = false; // dragging gate or placing wire
	float2 drag_offs;
	
	Selection gate_hitboxes (Gate& gate, int gate_idx, float2 point) {
		Selection s = {};

		// do here for efficincy (avoid iterating twice)
		if (gate.hitbox(_cursor_pos)) {
			s = { Selection::GATE, &gate };
		}

		if (mode == EM_EDIT) {
			int inp  = gate.input_hitbox(_cursor_pos);
			if (inp >= 0) {
				s = { Selection::INPUT, &gate, inp };
			}
			int outp = gate.output_hitbox(_cursor_pos);
			if (outp >= 0) {
				s = { Selection::OUTPUT, &gate, outp };
			}
		}
		return s;
	}
	
	void imgui (Input& I) {
		if (imgui_Header("LogicSim", true)) {

			{ // wierd logic that ignores EM_PLACE
				bool em = mode != EM_VIEW;
				if (ImGui::Checkbox("Edit Mode [E]", &em))
					mode = em ? EM_EDIT : EM_VIEW;
			}

			ImGui::Checkbox("snapping", &snapping);
			ImGui::SameLine();
			ImGui::InputFloat("###snapping_size", &snapping_size);

			if (ImGui::BeginTable("Gates", 2, ImGuiTableFlags_Borders)) {
				
				ImGui::TableNextColumn();
				if (ImGui::Selectable("BUF" , gate_preview.type == GT_BUF )) gate_preview.type = GT_BUF;
				ImGui::TableNextColumn();
				if (ImGui::Selectable("NOT" , gate_preview.type == GT_NOT )) gate_preview.type = GT_NOT;

				ImGui::TableNextColumn();
				if (ImGui::Selectable("AND" , gate_preview.type == GT_AND )) gate_preview.type = GT_AND;
				ImGui::TableNextColumn();
				if (ImGui::Selectable("NAND", gate_preview.type == GT_NAND)) gate_preview.type = GT_NAND;
				
				ImGui::TableNextColumn();
				if (ImGui::Selectable("OR"  , gate_preview.type == GT_OR  )) gate_preview.type = GT_OR;
				ImGui::TableNextColumn();
				if (ImGui::Selectable("NOR" , gate_preview.type == GT_NOR )) gate_preview.type = GT_NOR;
				
				ImGui::TableNextColumn();
				if (ImGui::Selectable("XOR" , gate_preview.type == GT_XOR )) gate_preview.type = GT_XOR;
				ImGui::TableNextColumn();

				ImGui::EndTable();
			}

			ImGui::Text("Gates: %d", (int)gates.size());

			ImGui::PopID();
		}
	}

	void update (Input& I, View3D& view, DebugDraw& dbgdraw, Window& window) {
		ZoneScoped;
		
		_cursor_valid = view.cursor_ray(I, &_cursor_pos);

		if (mode != EM_PLACE && I.buttons['E'].went_down)
			mode = mode == EM_VIEW ? EM_EDIT : EM_VIEW;

		// unselect gate if imgui to-be-placed is selected
		if (gate_preview.type > GT_NULL) {
			mode = EM_PLACE;
			gate_preview.state = 3;
		}

		// deselect everthing when in view mode or on RMB in place mode
		// NOTE: keep selection in EDIT mode because RMB doubles as camera move button (click on empty space to unselect instead)
		if (mode == EM_VIEW || (mode == EM_PLACE && I.buttons[MOUSE_BUTTON_RIGHT].went_down)) {
			sel = {};
			gate_preview.type = GT_NULL;
			wire_preview = {};
			editing = false;
			// go from place mode into edit mode with right click
			if (mode == EM_PLACE) mode = EM_EDIT;
		}

		// placing gates
		if (_cursor_valid) {
			// move to-be-placed gate preview with cursor
			gate_preview.pos = snap(_cursor_pos - 0.5f);

			// place gate on left click
			if (mode == EM_PLACE && I.buttons[MOUSE_BUTTON_LEFT].went_down) {
				gates.add_gate(std::move(gate_preview)); // preview becomes real gate
				// preview of gate still exists
			}
		}

		
		bool try_select = mode == EM_EDIT && !editing && _cursor_valid && I.buttons[MOUSE_BUTTON_LEFT].went_down;
		if (try_select)
			sel = {}; // unselect if click and nothing was hit

		hover = {};

		for (int i=0; i<(int)gates.size(); ++i) {
			auto& g = gates[i];
			assert(g.type > GT_NULL);
			
			// do here for efficincy (avoid iterating twice)
			Selection hit = gate_hitboxes(g, i, _cursor_pos);
			if (hit) {
				if (try_select) { // gate was clicked
					sel = hit;
				}
				// gate was hovered
				hover = hit;
			}
		}

		if (sel.type == Selection::GATE) {
			if (!editing) {
				// begin dragging gate
				if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
					drag_offs = _cursor_pos - sel.gate->pos;
					editing = true;
				}
			}
			if (editing) {
				// drag gate
				if (editing && I.buttons[MOUSE_BUTTON_LEFT].is_down) {
					sel.gate->pos = snap(_cursor_pos - drag_offs);
				}
				// stop dragging gate
				if (editing && I.buttons[MOUSE_BUTTON_LEFT].went_up) {
					editing = false;
				}
			}

			// remove gates via DELETE key
			if (I.buttons[KEY_DELETE].went_down) {
				gates.remove_gate(sel.gate);
				if (hover == sel) hover = {};
				sel = {};
				wire_preview = {}; // just be to sure no stale pointers exist
			}
		}
		// drag selected IO via left click
		else if (sel.type == Selection::INPUT || sel.type == Selection::OUTPUT) {
			if (!editing) {
				// begin placing wire
				if (I.buttons[MOUSE_BUTTON_LEFT].went_down || !_cursor_valid) {
					// remember where dragging started
					
					if (sel.type == Selection::INPUT) {
						wire_preview.was_dst = true;
						sel.gate->inputs[sel.io_idx] = {}; // remove wire if new wire is begin being placed from input
						wire_preview.dst = { sel.gate, sel.io_idx };
					}
					else {                     
						wire_preview.src = { sel.gate, sel.io_idx };
					}

					wire_preview.unconnected_pos = _cursor_pos;

					editing = true;
				}
			}
			else {
				// if other end is unconnected 'connect' it with cursor
				wire_preview.unconnected_pos = snap(_cursor_pos);
				
				// connect to in/outputs where applicable each frame (does not stick)
				if (hover && (hover.type == Selection::INPUT || hover.type == Selection::OUTPUT)) {
				
					if (hover.type == Selection::OUTPUT && wire_preview.was_dst) {
						wire_preview.src.gate   = hover.gate;
						wire_preview.src.io_idx = hover.io_idx;
					}
					if (hover.type == Selection::INPUT && !wire_preview.was_dst) {
						wire_preview.dst.gate   = hover.gate;
						wire_preview.dst.io_idx = hover.io_idx;
					}
				}
				else {
					// unconnect if did hover over non-in/output
					if (wire_preview.was_dst) wire_preview.src = {};
					else                      wire_preview.dst = {};
				}
				
				//// stop dragging
				if (I.buttons[MOUSE_BUTTON_LEFT].went_down || !_cursor_valid) {
					if (hover && (hover.type == Selection::INPUT || hover.type == Selection::OUTPUT)) {
						// if wire_preview was a connected on both ends, make it a real connection
						if (wire_preview.src.gate && wire_preview.dst.gate) {
							gates.add_wire(wire_preview.src, wire_preview.dst, std::move(wire_preview.points));
						} else {
							// dragged from output, but did not connect, do nothing
						}

						wire_preview = {};
						sel = {};
						editing = false;
					}
					else {
						// add a wire point, at front or back depending on order that wire is built in
						if (wire_preview.was_dst) wire_preview.points.insert(wire_preview.points.begin(), wire_preview.unconnected_pos);
						else                      wire_preview.points.push_back(wire_preview.unconnected_pos);
					}
				}

				if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
					// undo one wire point or cancel wire
					if (wire_preview.points.empty()) {
						// cancel whole wire edit
						wire_preview = {};
						sel = {};
						editing = false;
					}
					else {
						// just remove last added point
						if (wire_preview.was_dst) wire_preview.points.erase(wire_preview.points.begin());
						else                      wire_preview.points.pop_back();
					}
				}

				if (!_cursor_valid) {
					wire_preview = {};
					sel = {};
					editing = false;
				}
			}
		}

		bool can_toggle = mode == EM_VIEW && hover.type == Selection::GATE;
		if (can_toggle && I.buttons[MOUSE_BUTTON_LEFT].went_down) {
			hover.gate->state ^= 1u <<  gates.cur_buf;
		}

		window.set_cursor(can_toggle ? Window::CURSOR_FINGER : Window::CURSOR_NORMAL);
	}
	
	void simulate (Input& I, DebugDraw& dbgdraw) {
		ZoneScoped;

		uint8_t next_smask = 1u << (gates.cur_buf^1);
		uint8_t cur_smask  = 1u <<  gates.cur_buf;

		for (int i=0; i<(int)gates.size(); ++i) {
			auto& g = gates[i];
			
			bool a_valid = gate_info[g.type].inputs >= 1 && g.inputs[0].gate;
			bool b_valid = gate_info[g.type].inputs >= 2 && g.inputs[1].gate;

			uint8_t new_state;

			if (!a_valid && !b_valid) {
				// keep prev state (needed to toggle gates via LMB)
				new_state = (g.state & cur_smask) != 0;
			}
			else {
				bool a = a_valid && (g.inputs[0].gate->state & cur_smask) != 0;
				bool b = b_valid && (g.inputs[1].gate->state & cur_smask) != 0;

				switch (g.type) {
					case GT_BUF  : new_state =  a;  break;
					case GT_NOT  : new_state = !a;  break;
				
					case GT_AND  : new_state =   a && b;	 break;
					case GT_NAND : new_state = !(a && b);	 break;
				
					case GT_OR   : new_state =   a || b;	 break;
					case GT_NOR  : new_state = !(a || b);	 break;
				
					case GT_XOR  : new_state =   a != b;	 break;

					default: assert(false);
				}
			}

			g.state &= ~next_smask;
			g.state |= new_state ? next_smask : 0;
		}

		gates.cur_buf ^= 1;
	}
};

struct Game {
	SERIALIZE(Game, cam, sim, sim_freq, pause)
	
	Camera2D cam = Camera2D();
	
	DebugDraw dbgdraw;
	
	LogicSim sim;

	float sim_freq = 10.0f;
	float sim_timer = 0;
	bool pause = false;
	bool manual_tick = false;

	float sim_anim_t = 0;

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
		
		sim.update(I, view, dbgdraw, window);

		if (!pause && sim_freq >= 0.1f) {

			sim_timer -= I.dt;
			
			float step = 1.0f / sim_freq;
			for (int i=0; i<4 && sim_timer < 0; ++i) {
				
				sim.simulate(I, dbgdraw);
				
				sim_timer += step;
			}

			sim_anim_t = clamp(1.0f - sim_timer/step, 0.0f, 1.0f);
		}
		else if (manual_tick) {
			sim.simulate(I, dbgdraw);
			sim_timer = 0;

			sim_anim_t = 1;
		}
		manual_tick = false;
	}
};
