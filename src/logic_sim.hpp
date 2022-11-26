#pragma once
#include "common.hpp"

constexpr float2x2 ROT[] = {
	float2x2(  1, 0,  0, 1 ),
	float2x2(  0, 1, -1, 0 ),
	float2x2( -1, 0,  0,-1 ),
	float2x2(  0,-1,  1, 0 ),
};
constexpr float2x2 INV_ROT[] = {
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
		return translate(pos) * ::scale(float2(scale)) * ROT[rot];
	}
	float2x3 calc_inv_matrix () {
		return INV_ROT[rot] * ::scale(float2(1.0f/scale)) * translate(-pos);
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
			int part_idx = -1;
			// which output pin of the part is connected to
			int pin_idx = 0;
			
			//int state_idx = 0;

			std::vector<float2> wire_points;
		};
		std::unique_ptr<InputWire[]> inputs = nullptr;

		Part (Chip* chip, Placement pos={}): chip{chip}, pos{pos},
			inputs{chip->inputs.size() > 0 ? std::make_unique<InputWire[]>(chip->inputs.size()) : nullptr} {}
		
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
		
		float2 size = 16;
		
		// how many total outputs are used (recursively)
		// and thus how many state vars need to be allocated
		// when this chip is placed in the simulation
		int state_count = -1; // -1 if stale

		struct IO_Pin {
			SERIALIZE(IO_Pin, name, pos)

			std::string name = "";
			Placement   pos;
		};

		std::vector<IO_Pin> inputs = {};
		std::vector<IO_Pin> outputs = {};
		
		// first all inputs, than all outputs, then all other direct children of this chip
		std::vector<Part> parts = {};
		
		//// to check against recursive self usage, which would cause a stack overflow
		//// this check is needed to prevent the user from causes a recursive self usage
		//bool _visited = false;
		
		int update_state_indices () {
			// state count cached, early out
			if (state_count >= 0)
				return state_count;
			
			// state count stale, recompute
			state_count = 0;
			for (auto& part : parts) {
				// states are placed flattened in order of depth first traversal of part (chip instance) tree
				part.state_idx = state_count;
				// allocate as many states as are needed recursively for this part
				state_count += part.chip->update_state_indices();
			}

			return state_count;
		}

		// TODO: eliminiate these with cached indices?
		int indexof_part (Part* part) {
			return indexof(parts, part, [] (Part const& l, Part* r) { return &l == r; });
		}
	};

	int indexof_chip (Chip* chip) const {
		return indexof(saved_chips, chip, [] (std::shared_ptr<Chip> const& l, Chip* r) { return l.get() == r; });
	}
	
	enum GateType {
		//NULL_GATE =-1,
		BUF_GATE  =0,
		NOT_GATE  ,
		AND_GATE  ,
		NAND_GATE ,
		OR_GATE   ,
		NOR_GATE  ,
		XOR_GATE  ,
		GATE_COUNT,
	};

	Chip gates[GATE_COUNT] = {
		{ "Buffer Gate", lrgb(0.5f, 0.5f,0.75f), 1, 1, {{"In", float2(-0.3f, +0)}}, {{"Out", float2(0.28f, 0)}} },
		{ "NOT Gate",    lrgb(   0,    0,    1), 1, 1, {{"In", float2(-0.3f, +0)}}, {{"Out", float2(0.36f, 0)}} },
		{ "AND Gate",    lrgb(   1,    0,    0), 1, 1, {{"A", float2(-0.3f, +0.25f)},{"B", float2(-0.3f, -0.25f)}}, {{"Out", float2(0.28f, 0)}} },
		{ "NAND Gate",   lrgb(0.5f,    1,    0), 1, 1, {{"A", float2(-0.3f, +0.25f)},{"B", float2(-0.3f, -0.25f)}}, {{"Out", float2(0.36f, 0)}} },
		{ "OR Gate",     lrgb(   1, 0.5f,    0), 1, 1, {{"A", float2(-0.3f, +0.25f)},{"B", float2(-0.3f, -0.25f)}}, {{"Out", float2(0.28f, 0)}} },
		{ "NOR Gate",    lrgb(   0,    1, 0.5f), 1, 1, {{"A", float2(-0.3f, +0.25f)},{"B", float2(-0.3f, -0.25f)}}, {{"Out", float2(0.36f, 0)}} },
		{ "XOR Gate",    lrgb(   0,    1,    0), 1, 1, {{"A", float2(-0.3f, +0.25f)},{"B", float2(-0.3f, -0.25f)}}, {{"Out", float2(0.28f, 0)}} },
	};

	static constexpr float2 PIN_SIZE = 0.25f;
	
	bool is_gate (Chip* chip) const {
		return chip >= gates && chip < &gates[GATE_COUNT];
	}
	GateType gate_type (Chip* chip) const {
		assert(is_gate(chip));
		return (GateType)(chip - gates);
	}

	friend void to_json (json& j, const Chip& chip, const LogicSim& sim) {
		
		j = {
			{"name", chip.name},
			{"col", chip.col},
			{"inputs", chip.inputs},
			{"outputs", chip.outputs},
			{"size", chip.size},
		};
		json& parts_j = j["parts"];

		for (auto& part : chip.parts) {
			json& part_j = parts_j.emplace_back();

			part_j = {
				{"chip", sim.is_gate(part.chip) ?
					sim.gate_type(part.chip) :
					sim.indexof_chip(part.chip) + sim.GATE_COUNT },
				{"pos", part.pos}
			};
			auto& inputs = part_j["inputs"];

			for (int i=0; i<part.chip->inputs.size(); ++i) {
				json& ij = inputs.emplace_back();
				ij["part_idx"] = part.inputs[i].part_idx;
				if (part.inputs[i].part_idx >= 0) {
					ij["pin_idx"] = part.inputs[i].pin_idx;
					if (!part.inputs[i].wire_points.empty())
						ij["wire_points"] = part.inputs[i].wire_points;
				}
			}
		}
	}
	friend void from_json (const json& j, Chip& chip, LogicSim& sim) {
		
		chip.name    = j.at("name");
		chip.col     = j.at("col");
		chip.inputs  = j.at("inputs");
		chip.outputs = j.at("outputs");
		chip.size    = j.at("size");
		
		json parts = j.at("parts");
		int part_count = (int)parts.size();

		chip.parts.clear();
		chip.parts.reserve(part_count);
			
		for (int i=0; i<part_count; ++i) {
			json& partj = parts[i];
			
			int chip_id   = partj.at("chip");
			Placement pos = partj.at("pos");

			assert(chip_id >= 0);
			Chip* part_chip = nullptr;
			if (chip_id >= 0) {
				if (chip_id >= 0 && chip_id < sim.GATE_COUNT)
					part_chip = &sim.gates[chip_id];
				else if (chip_id >= 0)
					part_chip = sim.saved_chips[chip_id - sim.GATE_COUNT].get();
			}

			auto& part = chip.parts.emplace_back(part_chip, pos);

			if (partj.contains("inputs")) {
				json inputsj = partj.at("inputs");
					
				assert(part.chip->inputs.size() == inputsj.size());
				
				for (int i=0; i<part.chip->inputs.size(); ++i) {
					auto& inpj = inputsj.at(i);
					auto& inp = part.inputs[i];

					int part_idx = inpj.at("part_idx");
					
					if (part_idx >= 0 && part_idx < part_count) {
						inp.part_idx = part_idx;
						inp.pin_idx = inpj.at("pin_idx");
				
						if (inpj.contains("wire_points"))
							inpj.at("wire_points").get_to(inp.wire_points);
					}
				}
			}
		}
	}

	friend void to_json (json& j, const LogicSim& sim) {
		//to_json(j["main_chip"], sim.main_chip, sim);

		json& jchips = j["chips"];
		for (auto& chip : sim.saved_chips) {
			json& jchip = jchips.emplace_back();
			to_json(jchip, *chip, sim);
		}
	}
	friend void from_json (const json& j, LogicSim& sim) {
		sim.saved_chips.clear();

		//from_json(j["main_chip"], sim.main_chip, sim);

		for (auto& jchip : j["chips"]) {
			auto& chip = sim.saved_chips.emplace_back(std::make_unique<Chip>());
			from_json(jchip, *chip, sim);
		}
		
		sim.clear_chip_view();
	}

	std::vector<std::shared_ptr<Chip>> saved_chips;

	std::shared_ptr<Chip> viewed_chip;

	std::vector<uint8_t> state[2];

	int cur_state = 0;

	void switch_to_chip_view (std::shared_ptr<Chip> chip) {
		state[0].clear();
		state[1].clear();
		cur_state = 0;

		// TODO: delete chip warning if main_chip will be deleted by this?
		viewed_chip = std::move(chip); // move copy of shared ptr (ie original still exists)

		// not actually needed?
		update_all_chip_state_indices();

		state[0].resize(viewed_chip->state_count);
		state[1].resize(viewed_chip->state_count);

		editor.reset();
	}
	void clear_chip_view () {
		switch_to_chip_view(std::make_shared<Chip>("", lrgb(1), float2(10, 6)));
	}

	// TODO: Is this needed?
	void update_all_chip_state_indices () {

		// Only one chip (except on json reload) was invalidated
		// (chips not depending on this one are not, but how do I know this?
		//  -> if I keep chips in saved_chips strictly in dependency order I can know this easily)

		// invalidate all chips and recompute state_counts
		for (auto& c : saved_chips)
			c->state_count = -1;
		viewed_chip->state_count = -1;

		for (auto& c : saved_chips)
			c->update_state_indices();
		viewed_chip->update_state_indices();
	}

	struct ChipEditor {

		enum Mode {
			VIEW_MODE=0,
			EDIT_MODE,
			PLACE_MODE,
		};
		Mode mode = VIEW_MODE;

		float snapping_size = 0.125f;
		bool snapping = true;
	
		float2 snap (float2 pos) {
			return snapping ? round(pos / snapping_size) * snapping_size : pos;
		}

		struct PartPreview {
			Chip* chip = nullptr;
			Placement pos = {};
		};
		PartPreview preview_part = {};
		
		struct Selection {
			enum Type {
				NONE = 0,
				PART,
				PIN_INP,
				PIN_OUT,
			};
			Type type = NONE;

			// part in chip
			Part* part = nullptr;
			int pin = 0;

			Chip* chip = nullptr;
			
			float2x3 world2chip = float2x3(0); // world2chip during hitbox test
			
			int part_state_idx = -1; // needed to toggle gates even when they are part of a chip placed in the viewed chip

			operator bool () {
				return type != NONE;
			}
		};
		
		void select_preview_gate_imgui (LogicSim& sim, const char* name, Chip* type) {

			bool selected = preview_part.chip && preview_part.chip == type;

			if (ImGui::Selectable(name, &selected)) {
				if (selected) {
					mode = PLACE_MODE;
					preview_part.chip = type;
				}
				else {
					preview_part.chip = nullptr;
					mode = EDIT_MODE;
				}
			}
		}
		void select_preview_chip_imgui (LogicSim& sim, const char* name, std::shared_ptr<Chip>& chip, bool can_place) {
			
			bool selected = preview_part.chip && preview_part.chip == chip.get();
			
			if (ImGui::Selectable(name, &selected, ImGuiSelectableFlags_AllowDoubleClick)) {

				// Open chip as main chip on double click (Previous chips is saved
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					sim.switch_to_chip_view(chip);
				}
				else if (can_place) {
					if (selected) {
						mode = PLACE_MODE;
						preview_part.chip = chip.get();
					}
					else {
						preview_part.chip = nullptr;
						mode = EDIT_MODE;
					}
				}
			}
		}

		void selection_imgui (Selection& sel) {
			if (!sel) {
				ImGui::Text("No Selection");
				return;
			}
			else if (sel.type == Selection::PIN_INP) {
				ImGui::Text("Input Pin#%d of %s", sel.pin, sel.part->chip->name.c_str());
			}
			else if (sel.type == Selection::PIN_OUT) {
				ImGui::Text("Output Pin#%d of %s", sel.pin, sel.part->chip->name.c_str());
			}

			ImGui::Text("%s Instance", sel.part->chip->name.c_str());
			
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::Text("Placement in containing chip:");
			ImGui::DragFloat2("pos", &sel.part->pos.pos.x, 0.1f);
			ImGui::SliderInt("rot", &sel.part->pos.rot, 0, 3);
			ImGui::DragFloat("scale", &sel.part->pos.scale, 0.1f, 0.001f, 100.0f);
			
			//ImGui::Spacing();
			//
			//ImGui::Text("Input Wires:");
			//
			//for (int i=0; i<(int)sel.part->chip->inputs.size(); ++i) {
			//	ImGui::Text("Input Pin#%d [%s]", i, sel.part->chip->inputs[i].name.c_str());
			//}
		}

		void parts_hierarchy_imgui (LogicSim& sim, Chip& chip) {
			if (ImGui::BeginTable("PartsList", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
				
				for (auto& part : chip.parts) {
					ImGui::TableNextColumn();
					ImGui::PushID(&part);
					
					bool selected = sel.type == Selection::PART && sel.part == &part;
					if (selected && just_selected)
						ImGui::SetScrollHereY();

					if (ImGui::Selectable(part.chip->name.c_str(), selected)) {
						if (!selected) {
							sel = { Selection::PART, &part };
							mode = EDIT_MODE;
						}
						else
							sel = {};
					}
					if (ImGui::IsItemHovered()) {
						hover = { Selection::PART, &part };
						imgui_hovered = true;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		void imgui (LogicSim& sim) {
			
			if (imgui_Header("Editor", true)) {
				ImGui::Indent(10);

				{
					bool m = mode != VIEW_MODE;
					if (ImGui::Checkbox("Edit Mode [E]", &m))
						mode = m ? EDIT_MODE : VIEW_MODE;
				}

				ImGui::Checkbox("snapping", &snapping);
				ImGui::SameLine();
				ImGui::InputFloat("###snapping_size", &snapping_size);

				ImGui::Spacing();
				if (imgui_Header("Gates", true)) {
					ImGui::Indent(10);

					if (ImGui::BeginTable("TableGates", 2, ImGuiTableFlags_Borders)) {
				
						ImGui::TableNextColumn();
						select_preview_gate_imgui(sim, "BUF" , &sim.gates[BUF_GATE ] );
						ImGui::TableNextColumn();
						select_preview_gate_imgui(sim, "NOT" , &sim.gates[NOT_GATE ] );
				
						ImGui::TableNextColumn();
						select_preview_gate_imgui(sim, "AND" , &sim.gates[AND_GATE ] );
						ImGui::TableNextColumn();
						select_preview_gate_imgui(sim, "NAND", &sim.gates[NAND_GATE] );
				
						ImGui::TableNextColumn();
						select_preview_gate_imgui(sim, "OR"  , &sim.gates[OR_GATE  ] );
						ImGui::TableNextColumn();
						select_preview_gate_imgui(sim, "NOR" , &sim.gates[NOR_GATE ] );
				
						ImGui::TableNextColumn();
						select_preview_gate_imgui(sim, "XOR" , &sim.gates[XOR_GATE ] );
						ImGui::TableNextColumn();

						ImGui::EndTable();
					}

					ImGui::PopID();
					ImGui::Unindent(10);
				}
			
				ImGui::Spacing();
				if (imgui_Header("User-defined Chips", true)) {
					ImGui::Indent(10);

					ImGui::InputText("name",  &sim.viewed_chip->name);
					ImGui::ColorEdit3("col",  &sim.viewed_chip->col.x);
					ImGui::DragFloat2("size", &sim.viewed_chip->size.x);
					
					if (ImGui::Button("Clear View")) {
						// discard main_chip contents by resetting main_chip to empty
						sim.clear_chip_view();
					}
					if (ImGui::Button("Save as New")) {
						// save main_chip in saved_chips and reset main_chip to empty
						sim.saved_chips.emplace_back(sim.viewed_chip);
						sim.clear_chip_view();
					}


					if (ImGui::BeginTable("ChipsList", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {

						// can only use chips eariler in the list to ensure no recursive dependencies via i < viewed_idx
						// if want to create a basic chip after a complex one has been created, need to use a feature to move chips up in the list
						int viewed_idx = sim.indexof_chip(sim.viewed_chip.get());
						if (viewed_idx < 0) viewed_idx = (int)sim.saved_chips.size();

						for (int i=0; i<viewed_idx; ++i) {
							ImGui::TableNextColumn();
							select_preview_chip_imgui(sim, sim.saved_chips[i]->name.c_str(), sim.saved_chips[i], true);
						}

						ImGui::TableNextColumn();
						ImGui::Spacing();

						for (int i=viewed_idx; i<(int)sim.saved_chips.size(); ++i) {
							ImGui::TableNextColumn();
							select_preview_chip_imgui(sim, sim.saved_chips[i]->name.c_str(), sim.saved_chips[i], false);
						}

						ImGui::EndTable();
					}

					ImGui::PopID();
					ImGui::Unindent(10);
				}
				
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
				if (imgui_Header("Selected Part", true)) {
					ImGui::Indent(10);
					
					selection_imgui(sel);
					
					ImGui::PopID();
					ImGui::Unindent(10);
				}

				//ImGui::Spacing();
				//if (imgui_Header("Parts Hierarchy", true)) {
				//	ImGui::Indent(10);
				//
				//	parts_hierarchy_imgui(sim, sim.main_chip);
				//
				//	ImGui::PopID();
				//	ImGui::Unindent(10);
				//}
				
				ImGui::Unindent(10);
				ImGui::PopID();
			}
			
			just_selected = false;
		}

		bool _cursor_valid;
		float2 _cursor_pos;
		
		// check mouse cursor against chip hitbox
		bool hitbox (float2 box_size, float2x3 const& world2chip) {
			if (!_cursor_valid) return false;
			
			// cursor in space of chip normalized to size
			float2 p = world2chip * _cursor_pos;
			p /= box_size;
			// chip is a [-0.5, 0.5] box in this space
			return p.x >= -0.5f && p.x < 0.5f &&
			       p.y >= -0.5f && p.y < 0.5f;
		}
		
		bool imgui_hovered = false;
		bool just_selected = false;

		Selection sel   = {};
		Selection hover = {};
		
		bool dragging = false; // dragging gate
		float2 drag_offs;

		struct WirePreview {
			struct Connection {
				Part* part = nullptr;
				int   pin = 0;
			};
			Connection src = {};
			Connection dst = {};

			Chip* chip = nullptr;

			float2 unconn_pos;

			std::vector<float2> points;
		};

		WirePreview wire_preview = {};

		void reset () {
			mode = EDIT_MODE;

			hover = {};
			sel = {};

			dragging = false;

			wire_preview = {};
			preview_part.chip = nullptr;
		}
		
		void edit_placement (Input& I, Placement& p) {
			if (I.buttons['R'].went_down) {
				int dir = I.buttons[KEY_LEFT_SHIFT].is_down ? -1 : +1;
				p.rot = wrap(p.rot + dir, 4);
			}
		}
		
		void add_part (LogicSim& sim, Chip& chip, PartPreview& part) {
			chip.parts.emplace_back(part.chip, part.pos);
			
			assert(part.chip->state_count > 0);
			for (int i=0; i<2; ++i)
				sim.state[i].insert(sim.state[i].begin() + chip.state_count, part.chip->state_count, 0);

			sim.update_all_chip_state_indices();
		}
		void remove_part (LogicSim& sim, Selection& sel) {
			int idx = sel.chip->indexof_part(sel.part);

			sel.chip->parts.erase(sel.chip->parts.begin() + idx);

			int first = sel.part->state_idx;
			int end = sel.part->state_idx + sel.part->chip->state_count;
			for (int i=0; i<2; ++i)
				sim.state[i].erase(sim.state[i].begin() + first, sim.state[i].begin() + end);

			// update input wire part indices
			for (auto& part : sel.chip->parts) {
				for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
					if (part.inputs[i].part_idx == idx)
						part.inputs[i].part_idx = -1;
					else if (part.inputs[i].part_idx > idx)
						part.inputs[i].part_idx -= 1;
				}
			}

			sim.update_all_chip_state_indices();
		}

		void add_wire (WirePreview& wire) {
			assert(wire.dst.part);
			assert(wire.dst.pin < (int)wire.dst.part->chip->inputs.size());
			if (wire.dst.part) assert(wire.src.pin < (int)wire.src.part->chip->outputs.size());
			int part_idx = wire.chip->indexof_part(wire.src.part);

			wire.dst.part->inputs[wire.dst.pin] = { part_idx, wire.src.pin, std::move(wire.points) };
		}

		// Currently just recursively handles hitbox testing
		// TODO: proper chip instancing requires moving more editing code in here?
		Selection edit_chip (Input& I, LogicSim& sim, Chip& chip, float2x3 const& world2chip, int state_base) {
			
			int state_offs = 0;
			
			Selection chip_hover = {};
			Selection sub_hover = {};
			
			for (auto& part : chip.parts) {
				auto world2part = part.pos.calc_inv_matrix() * world2chip;
				int part_state_idx = state_base + state_offs;
				
				if (mode != PLACE_MODE && hitbox(part.chip->size, world2part))
					chip_hover = { Selection::PART, &part, 0, &chip, world2chip, part_state_idx };

				if (mode == EDIT_MODE) {
					for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
						if (hitbox(PIN_SIZE, part.chip->inputs[i].pos.calc_inv_matrix() * world2part))
							chip_hover = { Selection::PIN_INP, &part, i, &chip, world2chip };
					}
					for (int i=0; i<(int)part.chip->outputs.size(); ++i) {
						if (hitbox(PIN_SIZE, part.chip->outputs[i].pos.calc_inv_matrix() * world2part))
							chip_hover = { Selection::PIN_OUT, &part, i, &chip, world2chip };
					}
				}

				Selection hov = edit_chip(I, sim, *part.chip, world2part, part_state_idx);
				if (hov) sub_hover = hov;

				state_offs += part.chip->state_count;
			}

			return sub_hover ? sub_hover : chip_hover;
		}

		void update (Input& I, View3D& view, Window& window, LogicSim& sim) {
			{
				float3 cur;
				_cursor_valid = view.cursor_ray(I, &cur);
				_cursor_pos = (float2)cur;
			}

			if (mode != PLACE_MODE && I.buttons['E'].went_down) {
				mode = mode == VIEW_MODE ? EDIT_MODE : VIEW_MODE;
			}

			// reset hover but keep imgui hover
			if (!imgui_hovered)
				hover = {};
			imgui_hovered = false;

			hover = edit_chip(I, sim, *sim.viewed_chip, float2x3::identity(), 0);
			
			// unselect all when needed
			if (!_cursor_valid || mode != EDIT_MODE) {
				sel = {};
				wire_preview = {};
				dragging = false;
			}
			if (!PLACE_MODE) {
				preview_part.chip = nullptr;
			}

			if (mode == PLACE_MODE) {

				edit_placement(I, preview_part.pos);
				
				if (_cursor_valid) {
					// move to-be-placed gate preview with cursor
					preview_part.pos.pos = snap(_cursor_pos);
					
					// place gate on left click
					if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
						add_part(sim, *sim.viewed_chip, preview_part); // preview becomes real gate, TODO: add a way to add parts to chips other than the viewed chip (keep selected chip during part placement?)
						// preview of gate still exists
					}
				}

				// exit edit mode with RMB
				if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
					preview_part.chip = nullptr;
					mode = EDIT_MODE;
				}
			}
			else if (mode == EDIT_MODE) {

				// rewire pin when pin is selected
				if (sel.type == Selection::PIN_INP || sel.type == Selection::PIN_OUT) {
				
					bool from_dst = sel.type == Selection::PIN_INP;

					// start wire at selected pin
					wire_preview.src = {};
					wire_preview.dst = {};

					if (from_dst) wire_preview.dst = { sel.part, sel.pin };
					else          wire_preview.src = { sel.part, sel.pin  };
					wire_preview.chip = sel.chip;

					// unconnect previous connection on input pin when rewiring
					if (from_dst)
						sel.part->inputs[sel.pin] = {};
				
					// remember temprary end point for wire if not hovered over pin
					// convert cursor position to chip space and _then_ snap
					// this makes it so that we always snap in the space of the chip
					wire_preview.unconn_pos = snap(sel.world2chip * _cursor_pos);
				 
					// connect other end of wire to appropriate pin when hovered
					if (  (hover.type == Selection::PIN_INP || hover.type == Selection::PIN_OUT)
						  && hover.chip == wire_preview.chip) {
						if (from_dst) {
							if (hover.type == Selection::PIN_OUT) wire_preview.src = { hover.part, hover.pin };
						} else {
							if (hover.type == Selection::PIN_INP) wire_preview.dst = { hover.part, hover.pin };
						}
					}

					// establish wire connection or add wire point
					if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
						if (wire_preview.src.part && wire_preview.dst.part) {
							// clicked correct pin, connect wire
							add_wire(wire_preview);

							sel = {};
							wire_preview = {};
						}
						else {
							// add cur cursor position to list of wire points
							if (from_dst) wire_preview.points.insert(wire_preview.points.begin(), wire_preview.unconn_pos);
							else          wire_preview.points.push_back(wire_preview.unconn_pos);
						}
					}
					// remove wire point or stop cancel rewiring
					else if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
						// undo one wire point or cancel wire
						if (wire_preview.points.empty()) {
							// cancel whole wire edit
							sel = {};
							wire_preview = {};
						}
						else {
							// just remove last added point
							if (from_dst) wire_preview.points.erase(wire_preview.points.begin());
							else          wire_preview.points.pop_back();
						}
					}
				}
				// select thing when hovered and clicked
				else {
					// only select hovered things when not in wire placement mode
					if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
						just_selected = true;
						sel = hover;
					}

					// edit parts
					if (sel.type == Selection::PART) {
						
						// convert cursor position to chip space and _then_ snap that later
						// this makes it so that we always snap in the space of the chip
						float2 pos = sel.world2chip * _cursor_pos;

						edit_placement(I, sel.part->pos);

						if (!dragging) {
							// begin dragging gate
							if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
								drag_offs = pos - sel.part->pos.pos;
								dragging = true;
							}
						}
						if (dragging) {
							// drag gate
							if (I.buttons[MOUSE_BUTTON_LEFT].is_down) {
								sel.part->pos.pos = snap(pos - drag_offs);
							}
							// stop dragging gate
							if (I.buttons[MOUSE_BUTTON_LEFT].went_up) {
								dragging = false;
							}
						}

						// Duplicate selected part with CTRL+C
						// TODO: CTRL+D moves the camera to the right, change that?
						if (I.buttons[KEY_LEFT_CONTROL].is_down && I.buttons['C'].went_down) {
							preview_part.chip = sel.part->chip;
							mode = PLACE_MODE;
						}

						// remove gates via DELETE key
						if (I.buttons[KEY_DELETE].went_down) {
							remove_part(sim, sel);

							if (hover == sel) hover = {};
							sel = {};
							wire_preview = {}; // just be to sure no stale pointers exist
						}
					}
				}

			}

			// Gate toggle per LMB
			bool can_toggle = mode == VIEW_MODE && hover.type == Selection::PART && sim.is_gate(hover.part->chip);
			if (can_toggle && I.buttons[MOUSE_BUTTON_LEFT].went_down) {
				sim.state[sim.cur_state][hover.part_state_idx] ^= 1u;
			}

			window.set_cursor(can_toggle ? Window::CURSOR_FINGER : Window::CURSOR_NORMAL);
		}
	};
	ChipEditor editor;
	
	void imgui (Input& I) {
		ImGui::Text("Gates (# of states): %d", (int)state[0].size());

		editor.imgui(*this);
	}

	void update (Input& I, View3D& view, Window& window) {
		ZoneScoped;
		
		editor.update(I, view, window, *this);
	}
	
	void simulate (Chip& chip, int state_base) {

		uint8_t* cur  = state[cur_state  ].data();
		uint8_t* next = state[cur_state^1].data();
		
		int state_offs = 0;

		for (auto& part : chip.parts) {
			if (!is_gate(part.chip)) {
				simulate(*part.chip, state_base + state_offs);
			}
			else {
				int input_count = (int)part.chip->inputs.size();
			
				// TODO: cache part_idx in input as well to avoid indirection
				Part* src_a = input_count >= 1 && part.inputs[0].part_idx >= 0 ? &chip.parts[part.inputs[0].part_idx] : nullptr;
				Part* src_b = input_count >= 2 && part.inputs[1].part_idx >= 0 ? &chip.parts[part.inputs[1].part_idx] : nullptr;

				uint8_t new_state;

				assert(part.chip->state_count); // state_count stale!

				if (!src_a && !src_b) {
					// keep prev state (needed to toggle gates via LMB)
					new_state = cur[state_base + part.state_idx];
				}
				else {
					bool a = src_a && cur[state_base + src_a->state_idx] != 0;
					bool b = src_b && cur[state_base + src_b->state_idx] != 0;

					switch (gate_type(part.chip)) {
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

				next[state_base + part.state_idx] = new_state;

				assert(part.chip->state_count == 1);
			}

			state_offs += part.chip->state_count;
		}
		
		assert(chip.state_count >= 0); // state_count stale!
		assert(state_offs == chip.state_count); // state_count invalid!
	}

	void simulate (Input& I) {
		ZoneScoped;
		
		simulate(*viewed_chip, 0);

		cur_state ^= 1;
	}
};
