#include "common.hpp"
#include "logic_sim.hpp"

namespace logic_sim {

////
void simulate_chip (Chip& chip, int state_base, uint8_t* cur, uint8_t* next) {
		
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

			simulate_chip(*part.chip, state_base + state_offs, cur, next);
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

void LogicSim::simulate (Input& I) {
	ZoneScoped;
		
	uint8_t* cur  = state[cur_state  ].data();
	uint8_t* next = state[cur_state^1].data();

	int output_count = (int)viewed_chip->outputs.size();
	int input_count = (int)viewed_chip->inputs.size();

	for (int i=0; i<input_count; ++i) {
		// keep prev state (needed to toggle gates via LMB)
		next[output_count + i] = cur[output_count + i] != 0;
	}

	simulate_chip(*viewed_chip, 0, cur, next);

	cur_state ^= 1;
}

////
void to_json (json& j, const Chip& chip, const LogicSim& sim) {
		
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
			{"chip", is_gate(part.chip) ?
				gate_type(part.chip) :
				indexof_chip(sim.saved_chips, part.chip) + GATE_COUNT },
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
void from_json (const json& j, Chip& chip, LogicSim& sim) {
		
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
			if (chip_id >= 0 && chip_id < GATE_COUNT)
				part_chip = &gates[chip_id];
			else if (chip_id >= 0)
				part_chip = sim.saved_chips[chip_id - GATE_COUNT].get();
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

void to_json (json& j, const LogicSim& sim) {
	json& jchips = j["chips"];
	for (auto& chip : sim.saved_chips) {
		json& jchip = jchips.emplace_back();
		to_json(jchip, *chip, sim);
	}
}
void from_json (const json& j, LogicSim& sim) {
	for (auto& jchip : j["chips"]) {
		auto& chip = sim.saved_chips.emplace_back(std::make_unique<Chip>());
		from_json(jchip, *chip, sim);
	}

	sim.reset_chip_view();
}

////
void Editor::select_preview_gate_imgui (LogicSim& sim, const char* name, Chip* type) {

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
void Editor::select_preview_chip_imgui (LogicSim& sim, const char* name, std::shared_ptr<Chip>& chip, bool can_place, bool is_viewed) {
			
	bool selected = preview_part.chip && preview_part.chip == chip.get();
			
	if (is_viewed)
		ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::ColorConvertFloat4ToU32({0.4f, 0.4f, 0.1f, 1}));

	if (ImGui::Selectable(name, &selected, ImGuiSelectableFlags_AllowDoubleClick)) {

		// Open chip as main chip on double click (Previous chips is saved
		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			sim.switch_to_chip_view(chip);
			reset(); // reset editor
		}
		else if (can_place) {
			if (selected) {
				preview_part.chip = chip.get();
				mode = PLACE_MODE;
			}
			else {
				preview_part.chip = nullptr;
				mode = EDIT_MODE;
			}
		}
	}
	//ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::ColorConvertFloat4ToU32({0.9f, 0.9f, 0.3f, 1}));
}

void Editor::saved_chips_imgui (LogicSim& sim) {
			
	if (ImGui::Button("Clear View")) {
		// discard main_chip contents by resetting main_chip to empty
		sim.reset_chip_view();
		reset(); // reset editor
	}
	ImGui::SameLine();
	if (ImGui::Button("Save as New")) {
		// save main_chip in saved_chips and reset main_chip to empty
		sim.saved_chips.emplace_back(sim.viewed_chip);

		sim.reset_chip_view();
		reset(); // reset editor
	}

	if (ImGui::BeginTable("ChipsList", 1, ImGuiTableFlags_Borders)) {

		// can only use chips eariler in the list to ensure no recursive dependencies via i < viewed_idx
		// if want to create a basic chip after a complex one has been created, need to use a feature to move chips up in the list
		int viewed_idx = indexof_chip(sim.saved_chips, sim.viewed_chip.get());
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
void Editor::viewed_chip_imgui (LogicSim& sim) {
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
void Editor::selection_imgui (Selection& sel) {
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

void Editor::parts_hierarchy_imgui (LogicSim& sim, Chip& chip) {
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

void Editor::imgui (LogicSim& sim) {
			
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
				select_preview_gate_imgui(sim, "INP" , &gates[INP_PIN  ] );
				ImGui::TableNextColumn();
				select_preview_gate_imgui(sim, "OUT" , &gates[OUT_PIN  ] );

				ImGui::TableNextColumn();
				select_preview_gate_imgui(sim, "BUF" , &gates[BUF_GATE ] );
				ImGui::TableNextColumn();
				select_preview_gate_imgui(sim, "NOT" , &gates[NOT_GATE ] );
				
				ImGui::TableNextColumn();
				select_preview_gate_imgui(sim, "AND" , &gates[AND_GATE ] );
				ImGui::TableNextColumn();
				select_preview_gate_imgui(sim, "NAND", &gates[NAND_GATE] );
				
				ImGui::TableNextColumn();
				select_preview_gate_imgui(sim, "OR"  , &gates[OR_GATE  ] );
				ImGui::TableNextColumn();
				select_preview_gate_imgui(sim, "NOR" , &gates[NOR_GATE ] );
				
				ImGui::TableNextColumn();
				select_preview_gate_imgui(sim, "XOR" , &gates[XOR_GATE ] );
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

void Editor::add_part (LogicSim& sim, Chip& chip, PartPreview& part) {

	int idx;
	if (part.chip == &gates[OUT_PIN]) {
		// insert output at end of outputs list
		idx = (int)chip.outputs.size();
		// add chip output at end
		chip.outputs.emplace_back("");

		// No pin_idx needs to be changed because we can only add at the end of the outputs list
	}
	else if (part.chip == &gates[INP_PIN]) {
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
void Editor::remove_part (LogicSim& sim, Selection& sel) {

	int idx = indexof_part(sel.chip->parts, sel.part);
			
	if (sel.part->chip == &gates[OUT_PIN]) {
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
	else if (sel.part->chip == &gates[INP_PIN]) {
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

void Editor::add_wire (WirePreview& wire) {
	assert(wire.dst.part);
	assert(wire.dst.pin < (int)wire.dst.part->chip->inputs.size());
	if (wire.dst.part) assert(wire.src.pin < (int)wire.src.part->chip->outputs.size());
	int part_idx = indexof_part(wire.chip->parts, wire.src.part);

	wire.dst.part->inputs[wire.dst.pin] = { part_idx, wire.src.pin, std::move(wire.points) };
}

void edit_placement (Input& I, Placement& p) {
	if (I.buttons['R'].went_down) {
		int dir = I.buttons[KEY_LEFT_SHIFT].is_down ? -1 : +1;
		p.rot = wrap(p.rot + dir, 4);
	}
}

void Editor::edit_chip (Input& I, LogicSim& sim, Chip& chip, float2x3 const& world2chip, int state_base) {
	
	int state_offs = 0;
	
	for (auto& part : chip.parts) {
		auto world2part = part.pos.calc_inv_matrix() * world2chip;

		int part_state_idx = state_base + state_offs;
		
		//if (I.buttons['K'].went_down) {
		//	printf("");
		//}

		if (mode != PLACE_MODE && hitbox(part.chip->size, world2part))
			hover = { Selection::PART, &part, 0, &chip, world2chip, part_state_idx };

		if (mode == EDIT_MODE) {
			if (part.chip != &gates[OUT_PIN]) {
				for (int i=0; i<(int)part.chip->outputs.size(); ++i) {
					if (hitbox(PIN_SIZE, get_out_pos_invmat(part.chip->get_output(i)) * world2part))
						hover = { Selection::PIN_OUT, &part, i, &chip, world2chip };
				}
			}
			if (part.chip != &gates[INP_PIN]) {
				for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
					if (hitbox(PIN_SIZE, get_inp_pos_invmat(part.chip->get_input(i)) * world2part))
						hover = { Selection::PIN_INP, &part, i, &chip, world2chip };
				}
			}
		}

		if (!is_gate(part.chip)) {
			
			edit_chip(I, sim, *part.chip, world2part, part_state_idx);

			for (int i=0; i<(int)part.chip->outputs.size(); ++i) {
				if (hitbox(PIN_SIZE, get_out_pos_invmat(part.chip->get_output(i)) * world2part))
					hover = { Selection::PIN_OUT, &part, i, &chip, world2chip };
			}
			for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
				if (hitbox(PIN_SIZE, get_inp_pos_invmat(part.chip->get_input(i)) * world2part))
					hover = { Selection::PIN_INP, &part, i, &chip, world2chip };
			}
		}

		state_offs += part.chip->state_count;
	}
}

void Editor::update (Input& I, LogicSim& sim, View3D& view) {
	{
		float3 cur_pos = 0;
		_cursor_valid = !ImGui::GetIO().WantCaptureMouse && view.cursor_ray(I, &cur_pos);
		_cursor_pos = (float2)cur_pos;
	}

	if (mode != PLACE_MODE && I.buttons['E'].went_down) {
		mode = mode == VIEW_MODE ? EDIT_MODE : VIEW_MODE;
	}

	// reset hover but keep imgui hover
	if (!imgui_hovered)
		hover = {};
	imgui_hovered = false;

	edit_chip(I, sim, *sim.viewed_chip, float2x3::identity(), 0);
			
	// unselect all when needed
	if (!_cursor_valid || mode != EDIT_MODE) {
		sel = {};
		preview_wire = {};
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
			preview_wire.src = {};
			preview_wire.dst = {};

			if (from_dst) preview_wire.dst = { sel.part, sel.pin };
			else          preview_wire.src = { sel.part, sel.pin  };
			preview_wire.chip = sel.chip;

			// unconnect previous connection on input pin when rewiring
			if (from_dst)
				sel.part->inputs[sel.pin] = {};
				
			// remember temprary end point for wire if not hovered over pin
			// convert cursor position to chip space and _then_ snap
			// this makes it so that we always snap in the space of the chip
			preview_wire.unconn_pos = snap(sel.world2chip * _cursor_pos);
				 
			// connect other end of wire to appropriate pin when hovered
			if (  (hover.type == Selection::PIN_INP || hover.type == Selection::PIN_OUT)
					&& hover.chip == preview_wire.chip) {
				if (from_dst) {
					if (hover.type == Selection::PIN_OUT) preview_wire.src = { hover.part, hover.pin };
				} else {
					if (hover.type == Selection::PIN_INP) preview_wire.dst = { hover.part, hover.pin };
				}
			}

			// establish wire connection or add wire point
			if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
				if (preview_wire.src.part && preview_wire.dst.part) {
					// clicked correct pin, connect wire
					add_wire(preview_wire);

					sel = {};
					preview_wire = {};
				}
				else {
					// add cur cursor position to list of wire points
					if (from_dst) preview_wire.points.insert(preview_wire.points.begin(), preview_wire.unconn_pos);
					else          preview_wire.points.push_back(preview_wire.unconn_pos);
				}
			}
			// remove wire point or cancel rewiring
			else if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
				// undo one wire point or cancel wire
				if (preview_wire.points.empty()) {
					// cancel whole wire edit
					sel = {};
					preview_wire = {};
				}
				else {
					// just remove last added point
					if (from_dst) preview_wire.points.erase(preview_wire.points.begin());
					else          preview_wire.points.pop_back();
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
					// just be to sure no stale pointers exist
					preview_wire = {};
					toggle_gate = {};
				}
			}
		}

	}
	
}

void Editor::update_toggle_gate (Input& I, LogicSim& sim, Window& window) {
	
	bool can_toggle = mode == VIEW_MODE && hover.type == Selection::PART && is_gate(hover.part->chip);
	
	if (!toggle_gate && can_toggle && I.buttons[MOUSE_BUTTON_LEFT].went_down) {
		toggle_gate = hover;
		state_toggle_value = !sim.state[sim.cur_state][toggle_gate.part_state_idx];
	}
	if (toggle_gate) {
		sim.state[sim.cur_state][toggle_gate.part_state_idx] = state_toggle_value;

		if (I.buttons[MOUSE_BUTTON_LEFT].went_up)
			toggle_gate = {};
	}

	window.set_cursor(can_toggle ? Window::CURSOR_FINGER : Window::CURSOR_NORMAL);
}
	
} // namespace logic_sim
