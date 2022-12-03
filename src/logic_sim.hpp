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

template<typename T>
struct schmart_array {
	std::unique_ptr<T[]> ptr;

	schmart_array (int size): ptr{size > 0 ? std::make_unique<T[]>(size) : nullptr} {}

	T& operator[] (int i) {
		return ptr[i];
	}
	T const& operator[] (int i) const {
		return ptr[i];
	}

	void insert (int old_size, int idx, T&& val) {
		assert(idx >= 0 && idx <= old_size);

		auto old = std::move(ptr);
		ptr = std::make_unique<T[]>(old_size + 1);

		for (int i=0; i<idx; ++i)
			ptr[i] = std::move(old[i]);

		ptr[idx] = std::move(val);

		for (int i=idx; i<old_size; ++i)
			ptr[i+1] = std::move(old[i]);
	}
	void erase (int old_size, int idx) {
		assert(old_size > 0 && idx < old_size);
		
		auto old = std::move(ptr);
		ptr = old_size-1 > 0 ?
			std::make_unique<T[]>(old_size - 1)
			: nullptr;

		for (int i=0; i<idx; ++i)
			ptr[i] = std::move(old[i]);

		for (int i=idx+1; i<old_size; ++i)
			ptr[i-1] = std::move(old[i]);
	}
};

struct LogicSim {
	struct Chip;

	// An instance of a chip placed down in a chip
	// (Primitive gates are also implemented as chips)
	struct Part {
		Chip* chip = nullptr;

		Placement pos = {};

		// where the states of this parts outputs are stored for this chip
		// ie. any chip being placed itself is a part with a state_idx, it's subparts then each have a state_idx relative to it
		int state_idx = -1; // check parent chip for state state_count, then this is also stale

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
		schmart_array<InputWire> inputs = {0};
		
		Part () {}
		Part (Chip* chip, Placement pos={}): chip{chip}, pos{pos}, inputs{(int)chip->inputs.size()} {}
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
			SERIALIZE(IO_Pin, name)

			std::string name = "";
		};

		std::vector<IO_Pin> outputs = {};
		std::vector<IO_Pin> inputs = {};
		
		// first all outputs, than all inputs, then all other direct children of this chip
		std::vector<Part> parts = {};
		
		Part& get_input (int i) {
			int outs = (int)outputs.size();
			assert(i >= 0 && i < (int)inputs.size());
			assert(parts.size() >= outs + (int)inputs.size());
			return parts[outs + i];
		}
		Part& get_output (int i) {
			assert(i >= 0 && i < (int)outputs.size());
			assert(parts.size() >= (int)outputs.size());
			return parts[i];
		}

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
			//return indexof(parts, part, [] (Part const& l, Part* r) { return &l == r; });
			size_t idx = part - parts.data();
			assert(idx >= 0 && idx < parts.size());
			return (int)idx;
		}
	};

	int indexof_chip (Chip* chip) const {
		return indexof(saved_chips, chip, [] (std::shared_ptr<Chip> const& l, Chip* r) { return l.get() == r; });
	}
	
	enum GateType {
		//NULL_GATE =-1,
		INP_PIN   =0,
		OUT_PIN   ,

		BUF_GATE  ,
		NOT_GATE  ,
		AND_GATE  ,
		NAND_GATE ,
		OR_GATE   ,
		NOR_GATE  ,
		XOR_GATE  ,
		GATE_COUNT,
	};

	static constexpr float2 PIN_SIZE = 0.25f;

	struct _IO {
		const char* name;
		float2 pos;
	};
	Chip _GATE (const char* name, lrgb col, float2 size, std::initializer_list<_IO> inputs, std::initializer_list<_IO> outputs) {
		Chip c(name, col, 1, 1);
		c.outputs.resize(outputs.size());
		c.inputs .resize(inputs .size());
		c.parts  .resize(outputs.size() + inputs .size());
		for (int i=0; i<(int)outputs.size(); ++i) {
			c.outputs[i].name = (outputs.begin() + i)->name;
			c.parts[i].pos.pos = (outputs.begin() + i)->pos;
		}
		for (int i=0; i<(int)inputs.size(); ++i) {
			c.inputs[i].name = (inputs.begin() + i)->name;
			c.parts[(int)outputs.size() + i].pos.pos = (inputs.begin() + i)->pos;
		}
		return c;
	}
	Chip gates[GATE_COUNT] = {
		_GATE("<input>",  srgb(190,255,  0), PIN_SIZE, {{"In", float2(-0.3f, +0)}}, {{"Out", float2(0.28f, 0)}}),
		_GATE("<output>", srgb(255, 10, 10), PIN_SIZE, {{"In", float2(-0.3f, +0)}}, {{"Out", float2(0.28f, 0)}}),

		_GATE("Buffer Gate", lrgb(0.5f, 0.5f,0.75f), 1, {{"In", float2(-0.3f, +0)}}, {{"Out", float2(0.28f, 0)}}),
		_GATE("NOT Gate",    lrgb(   0,    0,    1), 1, {{"In", float2(-0.3f, +0)}}, {{"Out", float2(0.36f, 0)}}),
		_GATE("AND Gate",    lrgb(   1,    0,    0), 1, {{"A", float2(-0.3f, +0.25f)}, {"B", float2(-0.3f, -0.25f)}}, {{"Out", float2(0.28f, 0)}}),
		_GATE("NAND Gate",   lrgb(0.5f,    1,    0), 1, {{"A", float2(-0.3f, +0.25f)}, {"B", float2(-0.3f, -0.25f)}}, {{"Out", float2(0.36f, 0)}}),
		_GATE("OR Gate",     lrgb(   1, 0.5f,    0), 1, {{"A", float2(-0.3f, +0.25f)}, {"B", float2(-0.3f, -0.25f)}}, {{"Out", float2(0.28f, 0)}}),
		_GATE("NOR Gate",    lrgb(   0,    1, 0.5f), 1, {{"A", float2(-0.3f, +0.25f)}, {"B", float2(-0.3f, -0.25f)}}, {{"Out", float2(0.36f, 0)}}),
		_GATE("XOR Gate",    lrgb(   0,    1,    0), 1, {{"A", float2(-0.3f, +0.25f)}, {"B", float2(-0.3f, -0.25f)}}, {{"Out", float2(0.28f, 0)}}),
	};
	
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
		// TODO: delete chip warning if main_chip will be deleted by this?
		viewed_chip = std::move(chip); // move copy of shared ptr (ie original still exists)

		// not actually needed?
		update_all_chip_state_indices();

		for (int i=0; i<2; ++i) {
			state[i].clear();
			state[i].resize(viewed_chip->state_count);
			state[i].shrink_to_fit();
		}
		cur_state = 0;

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
			float2x3 chip2world = float2x3(0);
			
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
		void select_preview_chip_imgui (LogicSim& sim, const char* name, std::shared_ptr<Chip>& chip, bool can_place, bool is_viewed) {
			
			bool selected = preview_part.chip && preview_part.chip == chip.get();
			
			if (is_viewed)
				ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::ColorConvertFloat4ToU32({0.4f, 0.4f, 0.1f, 1}));

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
			//ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::ColorConvertFloat4ToU32({0.9f, 0.9f, 0.3f, 1}));
		}

		void saved_chips_imgui (LogicSim& sim) {
			
			if (ImGui::Button("Clear View")) {
				// discard main_chip contents by resetting main_chip to empty
				sim.clear_chip_view();
			}
			ImGui::SameLine();
			if (ImGui::Button("Save as New")) {
				// save main_chip in saved_chips and reset main_chip to empty
				sim.saved_chips.emplace_back(sim.viewed_chip);
				sim.clear_chip_view();
			}

			if (ImGui::BeginTable("ChipsList", 1, ImGuiTableFlags_Borders)) {

				// can only use chips eariler in the list to ensure no recursive dependencies via i < viewed_idx
				// if want to create a basic chip after a complex one has been created, need to use a feature to move chips up in the list
				int viewed_idx = sim.indexof_chip(sim.viewed_chip.get());
				if (viewed_idx < 0) viewed_idx = (int)sim.saved_chips.size();

				for (int i=0; i<viewed_idx; ++i) {
					ImGui::TableNextColumn();
					select_preview_chip_imgui(sim, sim.saved_chips[i]->name.c_str(), sim.saved_chips[i], true, false);
				}

				ImGui::TableNextColumn();
				ImGui::Spacing();

				for (int i=viewed_idx; i<(int)sim.saved_chips.size(); ++i) {
					ImGui::TableNextColumn();
					select_preview_chip_imgui(sim, sim.saved_chips[i]->name.c_str(), sim.saved_chips[i], false, i == viewed_idx);
				}

				ImGui::EndTable();
			}
		}
		void viewed_chip_imgui (LogicSim& sim) {
			ImGui::InputText("name",  &sim.viewed_chip->name);
			ImGui::ColorEdit3("col",  &sim.viewed_chip->col.x);
			ImGui::DragFloat2("size", &sim.viewed_chip->size.x);

			if (ImGui::TreeNodeEx("Inputs", ImGuiTreeNodeFlags_DefaultOpen)) {
				for (int i=0; i<(int)sim.viewed_chip->inputs.size(); ++i) {
					ImGui::PushID(i);
					ImGui::SetNextItemWidth(ImGui::CalcItemWidth() * 0.5f);
					ImGui::InputText(prints("Input Pin #%d###name", i).c_str(), &sim.viewed_chip->inputs[i].name);
					ImGui::PopID();
				}
				ImGui::TreePop();
			}
			if (ImGui::TreeNodeEx("Outputs", ImGuiTreeNodeFlags_DefaultOpen)) {
				for (int i=0; i<(int)sim.viewed_chip->outputs.size(); ++i) {
					ImGui::PushID(i);
					ImGui::SetNextItemWidth(ImGui::CalcItemWidth() * 0.5f);
					ImGui::InputText(prints("Output Pin #%d###name", i).c_str(), &sim.viewed_chip->outputs[i].name);
					ImGui::PopID();
				}
				ImGui::TreePop();
			}
		}
		void selection_imgui (Selection& sel) {
			if (!sel) {
				ImGui::Text("No Selection");
				return;
			}
			else if (sel.type == Selection::PIN_INP) {
				ImGui::Text("Input Pin#%d of %s", sel.pin, sel.part->chip->name.c_str());
				return;
			}
			else if (sel.type == Selection::PIN_OUT) {
				ImGui::Text("Output Pin#%d of %s", sel.pin, sel.part->chip->name.c_str());
				return;
			}

			ImGui::Text("%s Instance", sel.part->chip->name.c_str());
			
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::Text("Placement in containing chip:");
			ImGui::DragFloat2("pos",  &sel.part->pos.pos.x, 0.1f);
			ImGui::SliderInt("rot",   &sel.part->pos.rot, 0, 3);
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
						select_preview_gate_imgui(sim, "INP" , &sim.gates[INP_PIN  ] );
						ImGui::TableNextColumn();
						select_preview_gate_imgui(sim, "OUT" , &sim.gates[OUT_PIN  ] );

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

					saved_chips_imgui(sim);

					ImGui::PopID();
					ImGui::Unindent(10);
				}

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
				if (imgui_Header("Viewed Chip", true)) {
					ImGui::Indent(10);
					
					viewed_chip_imgui(sim);
					
					ImGui::PopID();
					ImGui::Unindent(10);
				}
				
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

			int idx;
			if (part.chip == &sim.gates[OUT_PIN]) {
				// insert output at end of outputs list
				idx = (int)chip.outputs.size();
				// add chip output at end
				chip.outputs.emplace_back("");

				// No pin_idx needs to be changed because we can only add at the end of the outputs list
			}
			else if (part.chip == &sim.gates[INP_PIN]) {
				// insert input at end of inputs list
				idx = (int)chip.outputs.size() + (int)chip.inputs.size();
				int old_inputs = (int)chip.inputs.size();

				// add chip input at end
				chip.inputs.emplace_back("");
				
				// resize input array of every part of this chip type
				for (auto& schip : sim.saved_chips) {
					for (auto& part : schip->parts) {
						if (part.chip == &chip) {
							part.inputs.insert(old_inputs, old_inputs, {});
						}
					}
				}
			}
			else {
				// insert normal part at end of parts list
				idx = (int)chip.parts.size();
			}

			// shuffle input wire connection indices (part indices) where needed
			for (auto& cpart : chip.parts) {
				for (int i=0; i<(int)cpart.chip->inputs.size(); ++i) {
					if (cpart.inputs[i].part_idx >= idx) {
						cpart.inputs[i].part_idx += part.chip->state_count;
					}
				}
			}
			
			// insert states at state_idx of part that we inserted in front of (or after all parts of chip)
			int state_idx = idx >= (int)chip.parts.size() ? chip.state_count : chip.parts[idx].state_idx;

			// insert actual part into chip part list
			chip.parts.emplace(chip.parts.begin() + idx, part.chip, part.pos);
			
			// insert states for part  TODO: this is wrong if anything but the viewed chip (top level state) is edited (which I currently can't do)
			// -> need to fold this state insertion into the update_all_chip_state_indices() function because it knows exactly where states need to be inserted or erased
			assert(part.chip->state_count > 0);
			for (int i=0; i<2; ++i)
				sim.state[i].insert(sim.state[i].begin() + state_idx, part.chip->state_count, 0);

			// update state indices
			sim.update_all_chip_state_indices();
		}
		void remove_part (LogicSim& sim, Selection& sel) {

			int idx = sel.chip->indexof_part(sel.part);
			
			if (sel.part->chip == &sim.gates[OUT_PIN]) {
				assert(idx < (int)sel.chip->outputs.size());

				// erase output at out_idx
				sel.chip->outputs.erase(sel.chip->outputs.begin() + idx);
				
				// remove wires to this output with horribly nested code
				for (auto& schip : sim.saved_chips) {
					for (auto& part : schip->parts) {
						for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
							if (part.inputs[i].part_idx >= 0) {
								auto& inpart = schip->parts[part.inputs[i].part_idx];
								if (inpart.chip == sel.chip && part.inputs[i].pin_idx == idx) {
									part.inputs[i] = {};
								}
							}
						}
					}
				}
			}
			else if (sel.part->chip == &sim.gates[INP_PIN]) {
				int inp_idx = idx - (int)sel.chip->outputs.size();
				assert(inp_idx >= 0 && inp_idx < (int)sel.chip->inputs.size());

				int old_inputs = (int)sel.chip->inputs.size();

				// erase input at idx
				sel.chip->inputs.erase(sel.chip->inputs.begin() + idx);
				
				// resize input array of every part of this chip type
				for (auto& schip : sim.saved_chips) {
					for (auto& part : schip->parts) {
						if (part.chip == sel.chip) {
							part.inputs.erase(old_inputs, idx);
						}
					}
				}
			}

			// erase part from chip parts
			sel.chip->parts.erase(sel.chip->parts.begin() + idx);

			// erase part state  TODO: this is wrong if anything but the viewed chip (top level state) is edited (which I currently can't do)
			int first = sel.part->state_idx;
			int end = sel.part->state_idx + sel.part->chip->state_count;
			for (int i=0; i<2; ++i)
				sim.state[i].erase(sim.state[i].begin() + first, sim.state[i].begin() + end);

			// update input wire part indices
			for (auto& cpart : sel.chip->parts) {
				for (int i=0; i<(int)cpart.chip->inputs.size(); ++i) {
					if (cpart.inputs[i].part_idx == idx)
						cpart.inputs[i].part_idx = -1;
					else if (cpart.inputs[i].part_idx > idx)
						cpart.inputs[i].part_idx -= 1;
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
		void edit_chip (Input& I, LogicSim& sim, Chip& chip,
				float2x3 const& world2chip, float2x3 const& chip2world, int state_base) {
			
			int state_offs = 0;
			
			for (auto& part : chip.parts) {
				auto world2part = part.pos.calc_inv_matrix() * world2chip;
				auto part2world = chip2world * part.pos.calc_matrix();

				int part_state_idx = state_base + state_offs;
				
				if (I.buttons['K'].went_down) {
					printf("");
				}

				if (mode != PLACE_MODE && hitbox(part.chip->size, world2part))
					hover = { Selection::PART, &part, 0, &chip, world2chip, part2world, part_state_idx };

				if (mode == EDIT_MODE) {
					if (part.chip != &sim.gates[OUT_PIN]) {
						for (int i=0; i<(int)part.chip->outputs.size(); ++i) {
							if (hitbox(PIN_SIZE, part.chip->get_output(i).pos.calc_inv_matrix() * world2part))
								hover = { Selection::PIN_OUT, &part, i, &chip, world2chip, part2world * part.chip->get_output(i).pos.calc_matrix() };
						}
					}
					if (part.chip != &sim.gates[INP_PIN]) {
						for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
							if (hitbox(PIN_SIZE, part.chip->get_input(i).pos.calc_inv_matrix() * world2part))
								hover = { Selection::PIN_INP, &part, i, &chip, world2chip, part2world * part.chip->get_input(i).pos.calc_matrix() };
						}
					}
				}

				if (!sim.is_gate(part.chip)) {
					
					edit_chip(I, sim, *part.chip, world2part, part2world, part_state_idx);
					
					if (mode == EDIT_MODE) {
						for (int i=0; i<(int)part.chip->outputs.size(); ++i) {
							auto& out_gate = part.chip->get_output(i);
							assert(out_gate.chip == &sim.gates[OUT_PIN]);
							if (hitbox(PIN_SIZE, out_gate.chip->parts[0].pos.calc_inv_matrix() * out_gate.pos.calc_inv_matrix() * world2part))
								hover = { Selection::PIN_OUT, &part, i, &chip, world2chip, part2world * out_gate.pos.calc_matrix() };
						}
						for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
							auto& inp_gate = part.chip->get_input(i);
							assert(inp_gate.chip == &sim.gates[INP_PIN]);
							if (hitbox(PIN_SIZE, inp_gate.chip->parts[1].pos.calc_inv_matrix() * inp_gate.pos.calc_inv_matrix() * world2part))
								hover = { Selection::PIN_INP, &part, i, &chip, world2chip, part2world * inp_gate.pos.calc_matrix() };
						}
					}
				}

				state_offs += part.chip->state_count;
			}
		}

		void update (Input& I, View3D& view, Window& window, LogicSim& sim) {
			{
				float3 cur_pos;
				_cursor_valid = view.cursor_ray(I, &cur_pos);
				_cursor_pos = (float2)cur_pos;
			}

			if (mode != PLACE_MODE && I.buttons['E'].went_down) {
				mode = mode == VIEW_MODE ? EDIT_MODE : VIEW_MODE;
			}

			// reset hover but keep imgui hover
			if (!imgui_hovered)
				hover = {};
			imgui_hovered = false;

			edit_chip(I, sim, *sim.viewed_chip, float2x3::identity(), float2x3::identity(), 0);
			
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
					// remove wire point or cancel rewiring
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
			int input_count = (int)part.chip->inputs.size();

			if (!is_gate(part.chip)) {
				int output_count = (int)part.chip->outputs.size();

				for (int i=0; i<input_count; ++i) {
					Part* src = part.inputs[i].part_idx >= 0 ? &chip.parts[part.inputs[i].part_idx] : nullptr;
					
					uint8_t new_state;

					if (!src) {
						// keep prev state (needed to toggle gates via LMB)
						new_state = cur[state_base + state_offs + output_count + i] != 0;
					}
					else {
						// read input connection
						new_state = cur[state_base + src->state_idx + part.inputs[i].pin_idx] != 0;
						// write input part state
					}

					next[state_base + state_offs + output_count + i] = new_state;
				}

				simulate(*part.chip, state_base + state_offs);
			}
			else {
				auto type = gate_type(part.chip);
				
				assert(part.chip->state_count); // state_count stale!
				
				assert(part.state_idx == state_offs);
				assert(part.chip->state_count == 1);
				
				// TODO: cache part_idx in input as well to avoid indirection
				Part* src_a = input_count >= 1 && part.inputs[0].part_idx >= 0 ? &chip.parts[part.inputs[0].part_idx] : nullptr;
				Part* src_b = input_count >= 2 && part.inputs[1].part_idx >= 0 ? &chip.parts[part.inputs[1].part_idx] : nullptr;

				if (type == INP_PIN) {
					// input pins already updated outside of chip, inside of chip these do not have any inputs
				}
				else if (!src_a && !src_b) {
					// keep prev state (needed to toggle gates via LMB)
					next[state_base + state_offs] = cur[state_base + state_offs];
				}
				else {
					bool a = src_a && cur[state_base + src_a->state_idx + part.inputs[0].pin_idx] != 0;
					bool b = src_b && cur[state_base + src_b->state_idx + part.inputs[1].pin_idx] != 0;
					
					uint8_t new_state;
					switch (type) {
						case OUT_PIN  : new_state =  a;     break;

						case BUF_GATE : new_state =  a;     break;
						case NOT_GATE : new_state = !a;     break;
					
						case AND_GATE : new_state =   a && b;	 break;
						case NAND_GATE: new_state = !(a && b);	 break;
					
						case OR_GATE  : new_state =   a || b;	 break;
						case NOR_GATE : new_state = !(a || b);	 break;
					
						case XOR_GATE : new_state =   a != b;	 break;

						default: assert(false);
					}

					next[state_base + state_offs] = new_state;
				}

				// don't recurse
			}

			state_offs += part.chip->state_count;
		}
		
		assert(chip.state_count >= 0); // state_count stale!
		assert(state_offs == chip.state_count); // state_count invalid!
	}

	void simulate (Input& I) {
		ZoneScoped;
		
		uint8_t* cur  = state[cur_state  ].data();
		uint8_t* next = state[cur_state^1].data();

		int output_count = (int)viewed_chip->outputs.size();
		int input_count = (int)viewed_chip->inputs.size();

		for (int i=0; i<input_count; ++i) {
			// keep prev state (needed to toggle gates via LMB)
			next[output_count + i] = cur[output_count + i] != 0;
		}

		simulate(*viewed_chip, 0);

		cur_state ^= 1;
	}
};
