#pragma once

#if 0

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
		int      rot;

		uint8_t state; // double buffer

		struct Input {
			Gate* gate;
			int   io_idx;
			std::vector<float2> points;
		};
		std::vector<Input> inputs;
		
		void init () {
			inputs.assign(gate_info[type].inputs, Input{}); // no connections
			inputs.shrink_to_fit();
		}

		// relative to gate
		float2 get_io_pos (int i, int count, float x) {
			float h = 1.0f; // - 0.2f * 2
			float y = -0.5f;
			
			float step = h / (float)count;
			return ROT[rot] * float2(x, y + step * ((float)i + 0.5f));
		}
		// in world space
		float2 get_input_pos (int i) {
			assert(type >= GT_NULL);
			return pos + get_io_pos(i, gate_info[type].inputs, gate_info[type].input_x);
		}
		// in world space
		float2 get_output_pos (int i) {
			assert(type >= GT_NULL);
			return pos + get_io_pos(i, gate_info[type].outputs, gate_info[type].output_x);
		}
		
		bool hitbox (float2 point) {
			return point.x >= pos.x - 0.5f && point.x < pos.x + 0.5f &&
				   point.y >= pos.y - 0.5f && point.y < pos.y + 0.5f;
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
					{"rot",  gate.rot},
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
				gj.at("rot").get_to(gp->rot);
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

			gates.cur_buf = 0; // make sure we reset the current buffer to the one that we actually initialized
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
		if (gate.hitbox(point)) {
			s = { Selection::GATE, &gate };
		}

		if (mode == EM_EDIT) {
			int inp  = gate.input_hitbox(point);
			if (inp >= 0) {
				s = { Selection::INPUT, &gate, inp };
			}
			int outp = gate.output_hitbox(point);
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
	
	void rotate_button (Input& I, Gate& g) {
		if (I.buttons['R'].went_down) {
			int dir = I.buttons[KEY_LEFT_SHIFT].is_down ? +1 : -1;
			g.rot = wrap(g.rot + dir, 4);
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
		
		if (mode == EM_PLACE)
			rotate_button(I, gate_preview);

		// placing gates
		if (_cursor_valid) {
			// move to-be-placed gate preview with cursor
			gate_preview.pos = snap(_cursor_pos);

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

			rotate_button(I, *sel.gate);

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
		
		gates.cur_buf ^= 1;
		uint8_t prev_smask = 1u << (gates.cur_buf^1);
		uint8_t  cur_smask = 1u <<  gates.cur_buf;

		for (int i=0; i<(int)gates.size(); ++i) {
			auto& g = gates[i];
			
			bool a_valid = gate_info[g.type].inputs >= 1 && g.inputs[0].gate;
			bool b_valid = gate_info[g.type].inputs >= 2 && g.inputs[1].gate;

			uint8_t new_state;

			if (!a_valid && !b_valid) {
				// keep prev state (needed to toggle gates via LMB)
				new_state = (g.state & prev_smask) != 0;
			}
			else {
				bool a = a_valid && (g.inputs[0].gate->state & prev_smask) != 0;
				bool b = b_valid && (g.inputs[1].gate->state & prev_smask) != 0;

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

			g.state &= ~cur_smask;
			g.state |= new_state ? cur_smask : 0;
		}
	}
};

#endif

constexpr float2x2 ROT[] = {
	float2x2(  1, 0,  0, 1 ),
	float2x2(  0,-1,  1, 0 ),
	float2x2( -1, 0,  0,-1 ),
	float2x2(  0, 1, -1, 0 ),
};

struct Placement {
	SERIALIZE(Placement, pos, rot, scale)

	float2 pos = 0;
	int    rot = 0;
	float  scale = 1.0f;

	float2x3 calc_matrix () {
		return ::scale(float2(scale)) * translate(pos) * ROT[rot];
	}
};

struct LogicSim {
	struct Chip;

	// An instance of a chip placed down in a chip
	// (Primitive gates are also implemented as chips)
	struct Part {
		Chip* chip;

		Placement pos;

		// where the states of this parts outputs are stored for this chip
		// ie. any chip being placed itself is a part with a state_idx, it's subparts then each have a state_idx relative to it
		int state_idx; // check parent chip for state state_count, then this is also stale

		struct InputWire {
			// pointing to output of other part that is direct child of chip
			// can be normal part or chip input pin
			// or -1 if unconnected
			int subpart_idx = -1;
			// which output pin of the part is connected to
			int     pin_idx = 0;
			// recursively flattened state index relative to chip
			// ie. chip.state_idx + state_idx contains the state bits that need to be read for this input
			int   state_idx = 0;

			std::vector<float2> wire_points;
		};
		std::unique_ptr<InputWire[]> inputs = nullptr;

		Part (Chip* chip, Placement pos={}): chip{chip}, pos{pos},
			inputs{chip->inputs.size() > 0 ? std::make_unique<InputWire[]>(chip->inputs.size()) : nullptr} {}
		
		// clone a part (this does not clone the wiring in inputs[], it stays unconnected)
		// needed for primitives[] to be able to be initialized (c++ can't list init a vector of move-only types)
		// can also be used to implement a duplicate part button
		Part (Part const& p) {
			chip = p.chip;
			pos = p.pos;
			inputs = std::make_unique<InputWire[]>(chip->inputs.size());
		}
		Part (Part&&) = default;
		Part& operator= (Part const&) = delete;
		Part& operator= (Part&&) = default;
	};

	// A chip design that can be edited or simulated if viewed as the "global" chip
	// Uses other chips as parts, which are instanced into it's own editing or simulation
	// (but cannot use itself as part because this would cause infinite recursion)
	struct Chip {
		std::string name = "";
		lrgb        col = lrgb(0.8f);

		struct IO_Pin {
			SERIALIZE(IO_Pin, name)

			std::string name = "";
		};

		std::vector<IO_Pin> inputs = {};
		std::vector<IO_Pin> outputs = {};
		
		float2 size = 16;

		// how many total outputs are used (recursively)
		// and thus how many state vars need to be allocated
		// when this chip is placed in the simulation
		int state_count = -1; // -1 if stale

		// first all inputs, than all outputs, then all other direct children of this chip
		std::vector<Part> subparts = {};
		
		// to check against recursive self usage, which would cause a stack overflow
		// this check is needed to prevent the user from causes a recursive self usage
		bool _visited = false;
		
		Part& get_input_part (int i) {
			assert(i >= 0 && i < (int)inputs.size());
			assert(subparts.size() >= inputs.size());
			return subparts[i];
		}
		Part& get_output_part (int i) {
			assert(i >= 0 && i < (int)outputs.size());
			assert(subparts.size() >= inputs.size() + outputs.size());
			return subparts[(int)inputs.size() + i];
		}

		int update_state_indices () {
			// state count cached, early out
			if (state_count >= 0)
				return state_count;
			
			// state count stale, recompute
			state_count = 0;
			for (auto& part : subparts) {
				// states are placed flattened in order of depth first traversal of part (chip instance) tree
				part.state_idx = state_count;
				// allocate as many states as are needed recursively for this part
				state_count += part.chip->update_state_indices();
			}

			return state_count;
		}
	};

	
	enum PrimType {
		INP_PIN   =0,
		OUT_PIN   ,
		BUF_GATE  ,
		NOT_GATE  ,
		AND_GATE  ,
		NAND_GATE ,
		OR_GATE   ,
		NOR_GATE  ,
		XOR_GATE  ,
		PRIMITIVE_COUNT,
	};

	Chip primitives[PRIMITIVE_COUNT];
	
	bool is_primitive (Chip* chip) const {
		return chip >= primitives && chip < &primitives[PRIMITIVE_COUNT];
	}
	PrimType primitive_type (Chip* chip) const {
		assert(is_primitive(chip));
		return (PrimType)(chip - primitives);
	}

	LogicSim () {
		#define _INP(x,y) Part(&primitives[INP_PIN], {float2(x, y)})
		#define _OUT(x,y) Part(&primitives[OUT_PIN], {float2(x, y)})

		primitives[INP_PIN  ] = { "Input Pin"  , lrgb(0.8f, 0.1f, 0.1f), {{"In"}}, {},  0.2f, 0 };
		primitives[OUT_PIN  ] = { "Output Pin" , lrgb(0.1f, 0.1f, 0.8f), {}, {{"Out"}}, 0.2f, 0 };

		primitives[BUF_GATE ] = { "Buffer Gate", lrgb(0.5f, 0.5f,0.75f), {{"In"}}, {{"Out"}},      1, 1, {_INP(-0.3f, +0), _OUT(0.28f, 0)} };
		primitives[NOT_GATE ] = { "NOT Gate",    lrgb(   0,    0,    1), {{"In"}}, {{"Out"}},      1, 1, {_INP(-0.3f, +0), _OUT(0.36f, 0)} };
		primitives[AND_GATE ] = { "AND Gate",    lrgb(   1,    0,    0), {{"A"},{"B"}}, {{"Out"}}, 1, 1, {_INP(-0.3f, +0.25f), _INP(-0.3f, -0.25f), _OUT(0.28f, 0)} };
		primitives[NAND_GATE] = { "NAND Gate",   lrgb(0.5f,    1,    0), {{"A"},{"B"}}, {{"Out"}}, 1, 1, {_INP(-0.3f, +0.25f), _INP(-0.3f, -0.25f), _OUT(0.36f, 0)} };
		primitives[OR_GATE  ] = { "OR Gate",     lrgb(   1, 0.5f,    0), {{"A"},{"B"}}, {{"Out"}}, 1, 1, {_INP(-0.3f, +0.25f), _INP(-0.3f, -0.25f), _OUT(0.28f, 0)} };
		primitives[NOR_GATE ] = { "NOR Gate",    lrgb(   0,    1, 0.5f), {{"A"},{"B"}}, {{"Out"}}, 1, 1, {_INP(-0.3f, +0.25f), _INP(-0.3f, -0.25f), _OUT(0.36f, 0)} };
		primitives[XOR_GATE ] = { "XOR Gate",    lrgb(   0,    1,    0), {{"A"},{"B"}}, {{"Out"}}, 1, 1, {_INP(-0.3f, +0.25f), _INP(-0.3f, -0.25f), _OUT(0.28f, 0)} };
		
		#undef _INP
		#undef _OUT
	}
	
	friend void to_json (json& j, const Chip& chip, const LogicSim& sim) {
		
		j = {
			{"name", chip.name},
			{"col", chip.col},
			{"inputs", chip.inputs},
			{"outputs", chip.outputs},
			{"size", chip.size},
		};
		json& subparts_j = j["subparts"];

		for (auto& part : chip.subparts) {
			json& part_j = subparts_j.emplace_back();

			part_j = {
				{"chip", sim.is_primitive(part.chip) ? sim.primitive_type(part.chip) :
					-1 },
				{"pos", part.pos}
			};
			auto& inputs = part_j["inputs"];

			for (int i=0; i<part.chip->inputs.size(); ++i) {
				json& ij = inputs.emplace_back();
				ij["subpart_idx"] = part.inputs[i].subpart_idx;
				if (part.inputs[i].subpart_idx >= 0) {
					ij["pin_idx"] = part.inputs[i].pin_idx;
					if (!part.inputs[i].wire_points.empty())
						ij["wire_points"] = part.inputs[i].wire_points;
				}
			}
		}
	}
	friend void from_json (const json& j, Chip& chip, LogicSim& sim) {
		// TODO: temp code
		
		chip.name    = j.at("name");
		chip.col     = j.at("col");
		chip.inputs  = j.at("inputs");
		chip.outputs = j.at("outputs");
		chip.size    = j.at("size");
		
		json subparts = j.at("subparts");
		int part_count = (int)subparts.size();

		chip.subparts.clear();
		chip.subparts.reserve(part_count);
			
		for (int i=0; i<part_count; ++i) {
			json& partj = subparts[i];
			
			int chip_id   = partj.at("chip");
			Placement pos = partj.at("pos");

			Chip* part_chip;
			if (chip_id >= 0 && chip_id < sim.PRIMITIVE_COUNT) part_chip = &sim.primitives[chip_id];
			else assert(false); // TODO:
			
			auto& part = chip.subparts.emplace_back(part_chip, pos);

			if (partj.contains("inputs")) {
				json inputsj = partj.at("inputs");
					
				assert(part.chip->inputs.size() == inputsj.size());
				
				for (int i=0; i<part.chip->inputs.size(); ++i) {
					auto& inpj = inputsj.at(i);
					auto& inp = part.inputs[i];

					int subpart_idx = inpj.at("subpart_idx");
					
					if (subpart_idx >= 0 && subpart_idx < part_count) {
						inp.subpart_idx = subpart_idx;
						inp.pin_idx     = inpj.at("pin_idx");
				
						if (inpj.contains("wire_points"))
							inpj.at("wire_points").get_to(inp.wire_points);
					}
				}
			}
		}
	}

	friend void to_json (json& j, const LogicSim& t) {
		to_json(j["main_chip"], t.main_chip, t);
	}
	friend void from_json (const json& j, LogicSim& t) {
		from_json(j["main_chip"], t.main_chip, t);
	}
	
	// The chip you are viewing, ie editing and simulating
	// can be cleared or saved as a new chip in the list of chips
	Chip main_chip = {"<main>", lrgb(1), {}, {}, 100};


	std::vector<uint8_t> state[2];

	int cur_state = 0;

	void imgui (Input& I) {
		ZoneScoped;
		if (imgui_Header("LogicSim", true)) {


			ImGui::Text("Gates (# of states): %d", (int)state[0].size());

			ImGui::PopID();
		}
	}

	void update (Input& I, View3D& view, Window& window) {
		ZoneScoped;
		
		
		if (main_chip.state_count < 0) {
			ZoneScopedN("update_state_indices");
			main_chip.update_state_indices();

			// TODO: for now just zero and resize the state to fit all the states
			// later I want to be able to insert parts and have the states be moved correctly, which requires the recursive editor code to do this
			state[0].clear();
			state[0].resize(main_chip.state_count);
			state[1].clear();
			state[1].resize(main_chip.state_count);
		}
	}
	
	void simulate (Input& I, Chip& chip) {

		uint8_t* cur  = state[cur_state  ].data();
		uint8_t* next = state[cur_state^1].data();
		
		int state_count = 0;

		for (auto& part : chip.subparts) {
			
			int input_count = (int)part.chip->inputs.size();
			
			Part* src_a = input_count >= 1 && part.inputs[0].subpart_idx >= 0 ? &chip.subparts[part.inputs[0].subpart_idx] : nullptr;
			Part* src_b = input_count >= 2 && part.inputs[1].subpart_idx >= 0 ? &chip.subparts[part.inputs[1].subpart_idx] : nullptr;

			uint8_t new_state;

			assert(part.chip->state_count); // state_count stale!

			if (!src_a && !src_b) {
				// keep prev state (needed to toggle gates via LMB)
				new_state = cur[part.state_idx];
			}
			else {
				bool a = src_a && cur[src_a->state_idx] != 0;
				bool b = src_b && cur[src_b->state_idx] != 0;

				switch (primitive_type(part.chip)) {
					case BUF_GATE : new_state =  a;  break;
					case NOT_GATE : new_state = !a;  break;
					
					case AND_GATE : new_state =   a && b;	 break;
					case NAND_GATE: new_state = !(a && b);	 break;
					
					case OR_GATE  : new_state =   a || b;	 break;
					case NOR_GATE : new_state = !(a || b);	 break;
					
					case XOR_GATE : new_state =   a != b;	 break;

					default: assert(false);
				}
			}

			next[part.state_idx] = new_state;

			state_count += part.chip->state_count;
		}
		
		assert(chip.state_count > 0); // state_count stale!
		assert(state_count == chip.state_count); // state_count invalid!

		cur_state ^= 1;
	}

	void simulate (Input& I) {
		ZoneScoped;
		
		simulate(I, main_chip);
	}
};
