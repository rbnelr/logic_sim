#include "common.hpp"
#include "logic_sim.hpp"
#include "game.hpp"

namespace logic_sim {

////
void simulate_chip (Chip& chip, int state_base, uint8_t* cur, uint8_t* next) {
		
	int state_offs = 0;
	
	for (auto& part : chip.outputs) {
		assert(part->chip == &gates[OUT_PIN]);
		
		assert(part->state_idx == state_offs);
		assert(part->chip->state_count == 1);
		
		Part* src_a = part->inputs[0].part;

		if (!src_a) {
			// keep prev state (needed to toggle gates via LMB)
			next[state_base + state_offs] = cur[state_base + state_offs];
		}
		else {
			bool new_state = src_a && cur[state_base + src_a->state_idx + part->inputs[0].pin_idx] != 0;
			
			next[state_base + state_offs] = new_state;
		}

		state_offs += 1;
	}
	
	state_offs += (int)chip.inputs.size();

	for (auto& part : chip.parts) {
		int input_count = (int)part->chip->inputs.size();

		if (!is_gate(part->chip)) {
			int output_count = (int)part->chip->outputs.size();

			for (int i=0; i<input_count; ++i) {
				Part* src = part->inputs[i].part;
					
				uint8_t new_state;

				if (!src) {
					// keep prev state (needed to toggle gates via LMB)
					new_state = cur[state_base + state_offs + output_count + i] != 0;
				}
				else {
					// read input connection
					new_state = cur[state_base + src->state_idx + part->inputs[i].pin_idx] != 0;
					// write input part state
				}

				next[state_base + state_offs + output_count + i] = new_state;
			}

			simulate_chip(*part->chip, state_base + state_offs, cur, next);
		}
		else {
			auto type = gate_type(part->chip);

			assert(type != INP_PIN && type != OUT_PIN);
			
			assert(part->chip->state_count); // state_count stale!
			
			assert(part->state_idx == state_offs);
			assert(part->chip->state_count == 1);
			
			// TODO: cache part_idx in input as well to avoid indirection
			Part* src_a = input_count >= 1 ? part->inputs[0].part : nullptr;
			Part* src_b = input_count >= 2 ? part->inputs[1].part : nullptr;
			Part* src_c = input_count >= 3 ? part->inputs[2].part : nullptr;

			if (!src_a && !src_b) {
				// keep prev state (needed to toggle gates via LMB)
				next[state_base + state_offs] = cur[state_base + state_offs];
			}
			else {
				bool a = src_a && cur[state_base + src_a->state_idx + part->inputs[0].pin_idx] != 0;
				bool b = src_b && cur[state_base + src_b->state_idx + part->inputs[1].pin_idx] != 0;
				bool c = src_c && cur[state_base + src_c->state_idx + part->inputs[2].pin_idx] != 0;
					
				uint8_t new_state;
				switch (type) {
					case BUF_GATE : new_state =  a;     break;
					case NOT_GATE : new_state = !a;     break;
					
					case AND_GATE : new_state =   a && b;    break;
					case NAND_GATE: new_state = !(a && b);   break;
					
					case OR_GATE  : new_state =   a || b;    break;
					case NOR_GATE : new_state = !(a || b);   break;
					
					case XOR_GATE : new_state =   a != b;    break;

					case AND3_GATE : new_state =   a && b && c;    break;
					case NAND3_GATE: new_state = !(a && b && c);   break;
					
					case OR3_GATE  : new_state =   a || b || c;    break;
					case NOR3_GATE : new_state = !(a || b || c);   break;

					default: assert(false);
				}

				next[state_base + state_offs] = new_state;
			}

			// don't recurse
		}

		state_offs += part->chip->state_count;
	}
	
	assert(chip.state_count >= 0); // state_count stale!
	assert(state_offs == chip.state_count); // state_count invalid!
}

void LogicSim::simulate (Input& I) {
	ZoneScoped;
		
	uint8_t* cur  = state[cur_state  ].data();
	uint8_t* next = state[cur_state^1].data();

	// keep prev state (needed to toggle gates via LMB)
	for (auto& part : viewed_chip->inputs) {
		next[part->state_idx] = cur[part->state_idx] != 0;
	}

	simulate_chip(*viewed_chip, 0, cur, next);

	cur_state ^= 1;
}

////
json part2json (LogicSim const& sim, Part& part, std::unordered_map<Part*, int>& part2idx) {
	json j;
	j["chip"] = is_gate(part.chip) ?
			gate_type(part.chip) :
			indexof_chip(sim.saved_chips, part.chip) + GATE_COUNT;
	
	if (!part.name.empty())
		j["name"] = part.name;

	j["pos"] = part.pos;
	
	auto& inputs = j["inputs"];

	for (int i=0; i<part.chip->inputs.size(); ++i) {
		json& ij = inputs.emplace_back();

		ij["part_idx"] = part.inputs[i].part ? part2idx[part.inputs[i].part] : -1;
		if (part.inputs[i].part) {
			ij["pin_idx"] = part.inputs[i].pin_idx;

			if (!part.inputs[i].wire_points.empty())
				ij["wire_points"] = part.inputs[i].wire_points;
		}
	}

	return j;
}
json chip2json (const Chip& chip, LogicSim const& sim) {
	std::unordered_map<Part*, int> part2idx;
	part2idx.reserve(chip.inputs.size() + chip.outputs.size() + chip.parts.size());
	
	int idx = 0;
	for (auto& part : chip.outputs) {
		part2idx[part.get()] = idx++;
	}
	for (auto& part : chip.inputs) {
		part2idx[part.get()] = idx++;
	}
	for (auto& part : chip.parts) {
		part2idx[part.get()] = idx++;
	}
	
	json jouts  = json::array();
	json jinps  = json::array();
	json jparts = json::array();

	for (auto& part : chip.outputs) {
		jouts.emplace_back( part2json(sim, *part, part2idx) );
	}

	for (auto& part : chip.inputs) {
		jinps.emplace_back( part2json(sim, *part, part2idx) );
	}

	for (auto& part : chip.parts) {
		jparts.emplace_back( part2json(sim, *part, part2idx) );
	}
	
	json j = {
		{"name",     chip.name},
		{"col",      chip.col},
		{"size",     chip.size},
		{"inputs",   std::move(jinps)},
		{"outputs",  std::move(jouts)},
		{"parts",    std::move(jparts)},
	};
	return j;
}
void to_json (json& j, LogicSim const& sim) {

	j["viewed_chip"] = indexof_chip(sim.saved_chips, sim.viewed_chip.get());

	json& jchips = j["chips"];
	for (auto& chip : sim.saved_chips) {
		jchips.emplace_back( chip2json(*chip, sim) );
	}
}

Part* json2part (const json& j, LogicSim const& sim) {
	
	int chip_id      = j.at("chip");
	std::string name = j.contains("name") ? j.at("name") : "";
	Placement   pos  = j.at("pos");
		
	assert(chip_id >= 0);
	Chip* part_chip = nullptr;
	if (chip_id >= 0) {
		if (chip_id >= 0 && chip_id < GATE_COUNT)
			part_chip = &gates[chip_id];
		else if (chip_id >= 0)
			part_chip = sim.saved_chips[chip_id - GATE_COUNT].get();
	}

	return new Part(part_chip, std::move(name), pos);
}
void json2links (const json& j, Part& part, std::vector<Part*>& idx2part) {
	if (j.contains("inputs")) {
		json inputsj = j.at("inputs");
		
		assert(part.chip->inputs.size() == inputsj.size());
		
		for (int i=0; i<part.chip->inputs.size(); ++i) {
			auto& inpj = inputsj.at(i);
			auto& inp = part.inputs[i];

			int part_idx = inpj.at("part_idx");
					
			if (part_idx >= 0 && part_idx < idx2part.size()) {
				inp.part = idx2part[part_idx];
				inp.pin_idx = inpj.at("pin_idx");
				
				if (inpj.contains("wire_points"))
					inpj.at("wire_points").get_to(inp.wire_points);
			}
		}
	}
}
void json2chip (const json& j, Chip& chip, LogicSim& sim) {
	
	auto& jouts = j.at("outputs");
	auto& jinps = j.at("inputs");
	auto& jparts = j.at("parts");
	size_t total = jouts.size() + jinps.size() + jparts.size();
	
	chip.parts.clear();
	chip.parts.reserve(jparts.size());
	
	// first pass to create part pointers
	std::vector<Part*> idx2part(total, nullptr);
	
	int idx = 0;
	int out_idx = 0, inp_idx = 0;
	for (auto& j : jouts) {
		auto* ptr = idx2part[idx++] = json2part(j, sim);
		chip.outputs[out_idx++] = std::unique_ptr<Part>(ptr);
	}
	for (auto& j : jinps) {
		auto* ptr = idx2part[idx++] = json2part(j, sim);
		chip.inputs[inp_idx++] = std::unique_ptr<Part>(ptr);
	}
	for (auto& j : jparts) {
		auto* ptr = idx2part[idx++] = json2part(j, sim);
		chip.parts.emplace(ptr);
	}

	// second pass to link parts via existing pointers
	idx = 0;
	for (auto& j : jouts) {
		json2links(j, *idx2part[idx++], idx2part);
	}
	for (auto& j : jinps) {
		json2links(j, *idx2part[idx++], idx2part);
	}
	for (auto& j : jparts) {
		json2links(j, *idx2part[idx++], idx2part);
	}
}
void from_json (const json& j, LogicSim& sim, Camera2D& cam) {
	// create chips without parts
	for (auto& jchip : j["chips"]) {
		auto& chip = sim.saved_chips.emplace_back(std::make_unique<Chip>());
		
		chip->name    = jchip.at("name");
		chip->col     = jchip.at("col");
		chip->size    = jchip.at("size");

		chip->outputs.resize(jchip.at("outputs").size());
		chip->inputs .resize(jchip.at("inputs" ).size());
	}
	// then convert chip ids references in parts to valid chips
	auto cur = sim.saved_chips.begin();
	for (auto& jchip : j["chips"]) {
		json2chip(jchip, *(*cur++), sim);
	}

	//int viewed_chip_idx = j["viewed_chip"];
	//if (viewed_chip_idx >= 0)
	//	sim.switch_to_chip_view( sim.saved_chips[viewed_chip_idx], cam);
}

////

// Test if to_place is used in place_inside
// if true need to disallow placing place_inside in to_place to avoid infinite recursion
bool check_recursion (Chip* to_place, Chip* place_inside) {
	if (to_place == place_inside)
		return true;

	for (auto& part : to_place->parts) {
		if (!is_gate(part->chip) && to_place->_recurs == 0) { // only check every chip once
			to_place->_recurs++;
			bool used_recursively = check_recursion(part->chip, place_inside);
			to_place->_recurs--;
			if (used_recursively)
				return true;
		}
	}
	return false;
}

void toggle_edit_mode (Editor& e) {
	if (e.in_mode<Editor::ViewMode>())
		e.mode = Editor::EditMode();
	else 
		e.mode = Editor::ViewMode();
}

void Editor::select_gate_imgui (LogicSim& sim, const char* name, Chip* type) {

	bool selected = in_mode<PlaceMode>() && std::get<PlaceMode>(mode).preview_part.chip == type;
	
	if (ImGui::Selectable(name, &selected)) {
		if (selected) {
			mode = PlaceMode{ PartPreview{ type, {} } };
		}
		else {
			mode = EditMode();
		}
	}
}

void Editor::saved_chips_imgui (LogicSim& sim, Camera2D& cam) {
	
	if (ImGui::Button("Clear View")) {
		// discard main_chip contents by resetting main_chip to empty
		sim.reset_chip_view(cam);
		reset();
	}
	ImGui::SameLine();
	if (ImGui::Button("Save as New")) {
		// save main_chip in saved_chips and reset main_chip to empty
		sim.saved_chips.emplace_back(sim.viewed_chip);

		sim.reset_chip_view(cam);
		reset();
	}
	
	//int delete_item = -1;

	if (ImGui::BeginTable("ChipsList", 1, ImGuiTableFlags_Borders)) {
		
		bool was_already_dragging = chips_reorder_src >= 0;
		int chips_reorder_targ = -1;
		
		const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();
		bool reorder_was_drawn = false;

		for (int i=0; i<(int)sim.saved_chips.size(); ++i) {
			auto& chip = sim.saved_chips[i];
			
			bool selected = in_mode<PlaceMode>() && std::get<PlaceMode>(mode).preview_part.chip == chip.get();
			bool can_place = !check_recursion(chip.get(), sim.viewed_chip.get());
			bool is_viewed = chip.get() == sim.viewed_chip.get();

			if (i == chips_reorder_src) {
				// skip item when reodering it
			}
			else {
				ImGui::TableNextColumn();
				
				float y = ImGui::TableGetCellBgRect(GImGui->CurrentTable, 0).Min.y;
				float my = ImGui::GetMousePos().y;
				if (was_already_dragging && !reorder_was_drawn && my < y + TEXT_BASE_HEIGHT) {
					chips_reorder_targ = i;
				
					// we are dragging a entry and are hovering an entry
					// insert dragged item before the hovered one
					ImGui::Selectable(sim.saved_chips[chips_reorder_src]->name.c_str(),
						false, ImGuiSelectableFlags_Disabled);

					ImGui::TableNextColumn();

					reorder_was_drawn = true;
				}

				if (is_viewed)
					ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::ColorConvertFloat4ToU32({0.8f, 0.8f, 0.4f, 0.3f}));
				else if (!can_place)
					ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::ColorConvertFloat4ToU32({1.0f, 0.2f, 0.2f, 0.05f}));
	
				if (ImGui::Selectable(chip->name.c_str(), &selected, ImGuiSelectableFlags_AllowDoubleClick)) {
					// Open chip as main chip on double click (Previous chips is saved
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						sim.switch_to_chip_view(chip, cam);
						reset(); // reset editor
					}
					else if (can_place) {
						if (selected) {
							mode = PlaceMode{ { chip.get(), {} } };
						}
						else {
							mode = EditMode();
						}
					}
				}

				//if (!was_already_dragging && ImGui::BeginPopupContextItem()) {
				//	if (ImGui::Button("Delete Chip")) {
				//		ImGui::OpenPopup("POPUP_delete_confirmation");
				//	}
				//	if (imgui_delete_confirmation(prints("Chip '%s'", chip->name.c_str()).c_str()) == _IM_CONF_DELETE) {
				//		ImGui::CloseCurrentPopup();
				//		delete_item = i;
				//	}
				//	ImGui::EndPopup();
				//}

				if (!can_place && ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Cannot place chip \"%s\" because it uses itself directly or inside a subchip.\n"
										"Doing so would create infinite recursion!", chip->name.c_str());
				}
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false) && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered()) {
					chips_reorder_src = i;
				}
			}
		}
		
		// add at end if out of range
		if (was_already_dragging && !reorder_was_drawn) {
			chips_reorder_targ = (int)sim.saved_chips.size();
			
			ImGui::TableNextColumn();
			// we are dragging a entry and are hovering an entry
			// insert dragged item before the hovered one
			ImGui::Selectable(sim.saved_chips[chips_reorder_src]->name.c_str(),
				false, ImGuiSelectableFlags_Disabled);
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && chips_reorder_src >= 0) {
			// stopped dragging
			if (chips_reorder_targ >= 0) {
				// apply change
				auto c = std::move(sim.saved_chips[chips_reorder_src]);
				sim.saved_chips.erase(sim.saved_chips.begin() + chips_reorder_src);
				sim.saved_chips.insert(sim.saved_chips.begin()
					+ (chips_reorder_targ - (chips_reorder_targ > chips_reorder_src ? 1 : 0))
					, std::move(c));
			}
			chips_reorder_src = -1;
		}

		ImGui::EndTable();
	}

	// TODO: can't delete chips that are used in other chips without first removing those with more code
	//if (delete_item >= 0) {
	//	sim.saved_chips.erase(sim.saved_chips.begin() + delete_item);
	//}
}
void Editor::viewed_chip_imgui (LogicSim& sim) {
	ImGui::InputText("name",  &sim.viewed_chip->name);
	ImGui::ColorEdit3("col",  &sim.viewed_chip->col.x);
	ImGui::DragFloat2("size", &sim.viewed_chip->size.x);

	if (ImGui::TreeNodeEx("Inputs")) {
		for (int i=0; i<(int)sim.viewed_chip->inputs.size(); ++i) {
			ImGui::PushID(i);
			ImGui::SetNextItemWidth(ImGui::CalcItemWidth() * 0.5f);
			ImGui::InputText(prints("Input Pin #%d###name", i).c_str(), &sim.viewed_chip->inputs[i]->name);
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Outputs")) {
		for (int i=0; i<(int)sim.viewed_chip->outputs.size(); ++i) {
			ImGui::PushID(i);
			ImGui::SetNextItemWidth(ImGui::CalcItemWidth() * 0.5f);
			ImGui::InputText(prints("Output Pin #%d###name", i).c_str(), &sim.viewed_chip->outputs[i]->name);
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
}

void Editor::selection_imgui (PartSelection& sel) {
	ImGui::Text("%d Item selected", (int)sel.items.size());

	auto& part = *sel.items[0].part;
	
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("First Selected: %s Instance", part.chip->name.c_str());

	ImGui::InputText("name",          &part.name);

	ImGui::Text("Placement in parent chip:");

	int rot = (int)part.pos.rot;
	ImGui::DragFloat2("pos",          &part.pos.pos.x, 0.1f);
	ImGui::SliderInt("rot [R]",       &rot, 0, 3);
	ImGui::Checkbox("mirror (X) [M]", &part.pos.mirror);
	ImGui::DragFloat("scale",         &part.pos.scale, 0.1f, 0.001f, 100.0f);
	part.pos.rot = (short)rot;
}

void Editor::imgui (LogicSim& sim, Camera2D& cam) {
	
	const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();
	
	if (imgui_Header("Editor", true)) {
		
		{
			bool m = !in_mode<Editor::ViewMode>();
			if (ImGui::Checkbox("Edit Mode [E]", &m))
				toggle_edit_mode(*this);
		}

		ImGui::Checkbox("snapping", &snapping);
		ImGui::SameLine();
		ImGui::InputFloat("###snapping_size", &snapping_size);
		
		if (imgui_Header("Selection", true)) {
			ImGui::Indent(10);
			
			// Child region so that window contents don't shift around when selection changes
			if (ImGui::BeginChild("SelectionRegion", ImVec2(0, TEXT_BASE_HEIGHT * 10))) {
				if (!in_mode<EditMode>()) {
					ImGui::Text("Not in Edit Mode");
				}
				else {
					auto& e = std::get<EditMode>(mode);
					
					if (!e.sel) {
						ImGui::Text("Nothing selected");
					}
					else {
						selection_imgui(e.sel);
					}
				}
			}
			ImGui::EndChild();
			
			ImGui::Unindent(10);
			ImGui::PopID();
		}

		if (imgui_Header("Viewed Chip", true)) {
			ImGui::Indent(10);
					
			viewed_chip_imgui(sim);
			
			ImGui::Unindent(10);
			ImGui::PopID();
		}

		if (imgui_Header("Parts", true)) {
			ImGui::Indent(10);
			
			struct Entry {
				const char* name = nullptr;
				Chip*       gate;
			};
			constexpr Entry entries[] = {
				{ "INP"    , &gates[INP_PIN   ] },
				{},
				{ "OUT"    , &gates[OUT_PIN   ] },
				{},
						     
				{ "BUF"    , &gates[BUF_GATE  ] },
				{},
				{ "NOT"    , &gates[NOT_GATE  ] },
				{},

				{ "AND"    , &gates[AND_GATE  ] },
				{ "AND-3"  , &gates[AND3_GATE ] },
				{ "NAND"   , &gates[NAND_GATE ] },
				{ "NAND-3" , &gates[NAND3_GATE] },

				{ "OR"     , &gates[OR_GATE   ] },
				{ "OR-3"   , &gates[OR3_GATE  ] },
				{ "NOR"    , &gates[NOR_GATE  ] },
				{ "NOR-3"  , &gates[NOR3_GATE ] },
					
				{ "XOR"    , &gates[XOR_GATE  ] },
				{},
				{},
				{},
			};

			if (ImGui::BeginTable("TableGates", 4, ImGuiTableFlags_Borders)) {
				
				for (auto& entry : entries) {
					ImGui::TableNextColumn();
					if (entry.name)
						select_gate_imgui(sim, entry.name, entry.gate);
				}

				ImGui::EndTable();
			}

			ImGui::Separator();

			saved_chips_imgui(sim, cam);
			
			ImGui::Unindent(10);
			ImGui::PopID();
		}
		
		ImGui::PopID();
	}
}

void Editor::add_part (LogicSim& sim, Chip& chip, PartPreview& part) {
	//assert(&chip == sim.viewed_chip.get());
	//
	//int idx;
	//if (part.chip == &gates[OUT_PIN]) {
	//	// insert output at end of outputs list
	//	idx = (int)chip.outputs.size();
	//	int old_outputs = (int)chip.outputs.size();
	//
	//	// add chip output at end
	//	chip.outputs.emplace_back("");
	//	
	//	// No pin_idx needs to be changed because we can only add at the end of the outputs list
	//}
	//else if (part.chip == &gates[INP_PIN]) {
	//	// insert input at end of inputs list
	//	idx = (int)chip.outputs.size() + (int)chip.inputs.size();
	//	int old_inputs = (int)chip.inputs.size();
	//
	//	// add chip input at end
	//	chip.inputs.emplace_back("");
	//			
	//	// resize input array of every part of this chip type
	//	for (auto& schip : sim.saved_chips) {
	//		for (auto& part : schip->parts) {
	//			if (part.chip == &chip) {
	//				part.inputs.insert(old_inputs, old_inputs, {});
	//			}
	//		}
	//	}
	//}
	//else {
	//	// insert normal part at end of parts list
	//	idx = (int)chip.parts.size();
	//}
	//
	//// shuffle input wire connection indices (part indices) where needed
	//for (auto& cpart : chip.parts) {
	//	for (int i=0; i<(int)cpart.chip->inputs.size(); ++i) {
	//		if (cpart.inputs[i].part_idx >= idx) {
	//			cpart.inputs[i].part_idx += part.chip->state_count;
	//		}
	//	}
	//}
	//		
	//// insert states at state_idx of part that we inserted in front of (or after all parts of chip)
	//int state_idx = idx >= (int)chip.parts.size() ? chip.state_count : chip.parts[idx].state_idx;
	//
	//// insert actual part into chip part list
	//chip.parts.emplace(chip.parts.begin() + idx, part.chip, part.pos);
	//		
	//// insert states for part  TODO: this is wrong if anything but the viewed chip (top level state) is edited (which I currently can't do)
	//// -> need to fold this state insertion into the update_all_chip_state_indices() function because it knows exactly where states need to be inserted or erased
	//assert(part.chip->state_count > 0);
	//for (int i=0; i<2; ++i)
	//	sim.state[i].insert(sim.state[i].begin() + state_idx, part.chip->state_count, 0);
	//
	//// update state indices
	//sim.update_all_chip_state_indices();
}
void Editor::remove_part (LogicSim& sim, Chip* chip, Part* part) {
	//assert(chip == sim.viewed_chip.get());
	//
	//int idx = indexof_part(chip->parts, part);
	//		
	//if (part->chip == &gates[OUT_PIN]) {
	//	assert(idx < (int)chip->outputs.size());
	//	
	//	int old_outputs = (int)chip->outputs.size();
	//
	//	// erase output at out_idx
	//	chip->outputs.erase(chip->outputs.begin() + idx);
	//	
	//	// remove wires to this output with horribly nested code
	//	for (auto& schip : sim.saved_chips) {
	//		for (auto& part : schip->parts) {
	//			for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
	//				if (part.inputs[i].part_idx >= 0) {
	//					auto& inpart = schip->parts[part.inputs[i].part_idx];
	//					if (inpart.chip == chip && part.inputs[i].pin_idx == idx) {
	//						part.inputs[i] = {};
	//					}
	//				}
	//			}
	//		}
	//	}
	//}
	//else if (part->chip == &gates[INP_PIN]) {
	//	int inp_idx = idx - (int)chip->outputs.size();
	//	assert(inp_idx >= 0 && inp_idx < (int)chip->inputs.size());
	//
	//	int old_inputs = (int)chip->inputs.size();
	//
	//	// erase input at idx
	//	chip->inputs.erase(chip->inputs.begin() + inp_idx);
	//	
	//	// resize input array of every part of this chip type
	//	for (auto& schip : sim.saved_chips) {
	//		for (auto& part : schip->parts) {
	//			if (part.chip == chip) {
	//				part.inputs.erase(old_inputs, inp_idx);
	//			}
	//		}
	//	}
	//}
	//
	//// erase part state  TODO: this is wrong if anything but the viewed chip (top level state) is edited (which I currently can't do)
	//int first = part->state_idx;
	//int end   = part->state_idx + part->chip->state_count;
	//
	//for (int i=0; i<2; ++i)
	//	sim.state[i].erase(sim.state[i].begin() + first, sim.state[i].begin() + end);
	//
	//// erase part from chip parts
	//chip->parts.erase(chip->parts.begin() + idx);
	//
	//// update input wire part indices
	//for (auto& cpart : chip->parts) {
	//	for (int i=0; i<(int)cpart.chip->inputs.size(); ++i) {
	//		if (cpart.inputs[i].part_idx == idx)
	//			cpart.inputs[i].part_idx = -1;
	//		else if (cpart.inputs[i].part_idx > idx)
	//			cpart.inputs[i].part_idx -= 1;
	//	}
	//}
	//
	//sim.update_all_chip_state_indices();
}

void Editor::add_wire (WireMode& w) {
	//auto& out = w.dir ? w.dst : w.src;
	//auto& inp = w.dir ? w.src : w.dst;
	//
	//assert(out.part && inp.part);
	//assert(inp.pin < (int)inp.part->chip->inputs.size());
	//assert(out.pin < (int)out.part->chip->outputs.size());
	//
	//int part_idx = indexof_part(w.chip->parts, out.part);
	//
	//inp.part->inputs[inp.pin] = { part_idx, out.pin, std::move(w.points) };
}

void edit_placement (Input& I, Placement& p) {
	if (I.buttons['R'].went_down) {
		int dir = I.buttons[KEY_LEFT_SHIFT].is_down ? -1 : +1;
		p.rot = wrap(p.rot + dir, 4);
	}
	if (I.buttons['M'].went_down) {
		p.mirror = !p.mirror;
	}
}

void Editor::edit_part (Input& I, LogicSim& sim, Chip& chip, Part& part, float2x3 const& world2chip, int state_idx) {
	
	auto world2part = part.pos.calc_inv_matrix() * world2chip;

	//if (I.buttons['K'].went_down) {
	//	printf("");
	//}

	if (!in_mode<PlaceMode>() && hitbox(part.chip->size, world2part))
		hover = { Hover::PART, &part, &chip, -1, state_idx, world2chip };

	if (in_mode<EditMode>() || in_mode<WireMode>()) {
		if (part.chip != &gates[OUT_PIN]) {
			for (int i=0; i<(int)part.chip->outputs.size(); ++i) {
				if (hitbox(PIN_SIZE, get_out_pos_invmat(*part.chip->outputs[i]) * world2part))
					hover = { Hover::PIN_OUT, &part, &chip, i, -1, world2chip };
			}
		}
		if (part.chip != &gates[INP_PIN]) {
			for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
				if (hitbox(PIN_SIZE, get_inp_pos_invmat(*part.chip->inputs[i]) * world2part))
					hover = { Hover::PIN_INP, &part, &chip, i, -1, world2chip };
			}
		}
	}

	if (!is_gate(part.chip)) {
			
		edit_chip(I, sim, *part.chip, world2part, state_idx);

		for (int i=0; i<(int)part.chip->outputs.size(); ++i) {
			if (hitbox(PIN_SIZE, get_out_pos_invmat(*part.chip->outputs[i]) * world2part))
				hover = { Hover::PIN_OUT, &part, &chip, i, -1, world2chip };
		}
		for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
			if (hitbox(PIN_SIZE, get_inp_pos_invmat(*part.chip->inputs[i]) * world2part))
				hover = { Hover::PIN_INP, &part, &chip, i, -1, world2chip };
		}
	}
}
void Editor::edit_chip (Input& I, LogicSim& sim, Chip& chip, float2x3 const& world2chip, int state_base) {
	
	int state_offs = 0;
	
	for (auto& part : chip.outputs) {
		edit_part(I, sim, chip, *part, world2chip, state_base + state_offs);
		state_offs += part->chip->state_count;
	}
	for (auto& part : chip.inputs) {
		edit_part(I, sim, chip, *part, world2chip, state_base + state_offs);
		state_offs += part->chip->state_count;
	}

	for (auto& part : chip.parts) {
		edit_part(I, sim, chip, *part, world2chip, state_base + state_offs);
		state_offs += part->chip->state_count;
	}
}

void Editor::update (Input& I, Game& g) {
	LogicSim& sim = g.sim;

	{ // Get cursor position
		float3 cur_pos = 0;
		_cursor_valid = !ImGui::GetIO().WantCaptureMouse && g.view.cursor_ray(I, &cur_pos);
		_cursor_pos = (float2)cur_pos;
	}

	// E toggles between edit and view mode (other modes are always exited)
	if (I.buttons['E'].went_down)
		toggle_edit_mode(*this);

	// compute hover
	hover = {};
	edit_chip(I, sim, *sim.viewed_chip, float2x3::identity(), 0);
	
	if (in_mode<PlaceMode>()) {
		auto& preview_part = std::get<PlaceMode>(mode).preview_part;
		
		edit_placement(I, preview_part.pos);
				
		if (_cursor_valid) {
			// move to-be-placed gate preview with cursor
			preview_part.pos.pos = snap(_cursor_pos);
					
			// place gate on left click
			if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
				add_part(sim, *sim.viewed_chip, preview_part); // preview becomes real gate, TODO: add a way to add parts to chips other than the viewed chip (keep selected chip during part placement?)
				// preview of gate still exists with same pos
			}
		}
	
		// exit edit mode with RMB
		if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
			preview_part = {}; // reset preview chip
			mode = EditMode();
		}
	}
	else if (in_mode<EditMode>()) {
		auto& e = std::get<EditMode>(mode);
		
		if (!_cursor_valid) {
			e.dragging = false;
		}
		
		bool shift = I.buttons[KEY_LEFT_SHIFT].is_down;

		if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
			// normal click
			// select part, unselect everything or select pin (go into wire mode)
			if (!shift || !e.sel) {
				if (hover.type == Hover::NONE) {
					e.sel = {};
				}
				else if (hover.type == Hover::PIN_INP || hover.type == Hover::PIN_OUT) {
					// begin wiring with hovered pin as start
					mode = WireMode{ hover.chip, hover.world2chip,
						hover.type == Hover::PIN_INP, { hover.part, hover.pin } };

					// unconnect previous connection on input pin when rewiring
					if (hover.type == Hover::PIN_INP)
						hover.part->inputs[hover.pin] = {};

					I.buttons[MOUSE_BUTTON_LEFT].went_down = false; // consume click to avoid wire mode also getting click?
				}
				else {
					assert(hover.type == Hover::PART);
					if (e.sel.has_part(hover.chip, hover.part)) {
						// keep multiselect if clicked on already selected item
					}
					else {
						e.sel = { hover.chip, {{ hover.part }}, hover.world2chip };
					}
				}
			}
			// shift click add/remove from selection
			else {
				assert(e.sel && !e.sel.items.empty());
				if (hover.type == Hover::PART && hover.chip == e.sel.chip) {
					e.sel.toggle_part(hover.part);
				}
				else {
					// ignore click
				}
			}
		}

		// edit parts
		if (in_mode<EditMode>() && e.sel) { // still in edit mode? else e becomes invalid

			float2 bounds_center;
			{
				e.sel.bounds = AABB::inf();

				for (auto& item : e.sel.items) {
					e.sel.bounds.add(item.part->get_aabb());
				}

				bounds_center = (e.sel.bounds.lo + e.sel.bounds.hi) * 0.5f;
					
				for (auto& item : e.sel.items) {
					item.bounds_offs = item.part->pos.pos - bounds_center;
				}
			}
			
			if (shift) {
				e.dragging = false;
			}
			else {

				// convert cursor position to chip space and _then_ snap that later
				// this makes it so that we always snap in the space of the chip
				float2 pos = snap(e.sel.world2chip * _cursor_pos);
			
				// Cursor is snapped, which is then used to move items as a group, thus unaligned items stay unaligned
				// snapping items individually is also possible

				if (!e.dragging && !shift) {
						// begin dragging gate
					if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
						e.drag_offset = pos - bounds_center;
						e.dragging = true;
					}
				}
				if (e.dragging) {
					// move selection to cursor
					bounds_center = pos - e.drag_offset;

					// drag gate
					if (I.buttons[MOUSE_BUTTON_LEFT].is_down) {
						for (auto& item : e.sel.items)
							item.part->pos.pos = item.bounds_offs + bounds_center;
					}
					// stop dragging gate
					if (I.buttons[MOUSE_BUTTON_LEFT].went_up) {
						e.dragging = false;
					}
				}
			}

			//edit_placement(I, sel.part->pos);
			
			
			//// Duplicate selected part with CTRL+C
			//// TODO: CTRL+D moves the camera to the right, change that?
			//if (I.buttons[KEY_LEFT_CONTROL].is_down && I.buttons['C'].went_down) {
			//	preview_part.chip = sel.part->chip;
			//	mode = PLACE_MODE;
			//}
			
			// remove gates via DELETE key
			if (e.sel.chip == sim.viewed_chip.get() && I.buttons[KEY_DELETE].went_down) {

				// Does not work
				for (auto& i : e.sel.items)
					remove_part(sim, e.sel.chip, i.part);

				// avoid stale pointer in hover
				if (e.sel.has_part(e.sel.chip, hover.part))
					hover = {};
				e.sel = {};
			}
			
			if (e.sel.items.size() > 1)
				g.dbgdraw.wire_quad(float3(e.sel.bounds.lo, 5.0f), e.sel.bounds.hi - e.sel.bounds.lo,
					lrgba(0.8f, 0.8f, 1.0f, 0.5f));
		}
	}
	
	// still execute wiremode after edit mode switched to it, so that wire shows instantly with correct unconn_pos
	if (in_mode<WireMode>()) {
		auto& w = std::get<WireMode>(mode);
		
		w.dst = {};

		// connect other end of wire to appropriate pin when hovered
		if (hover.chip == w.chip && (hover.type == Hover::PIN_INP || hover.type == Hover::PIN_OUT))
			if (w.dir == (hover.type == Hover::PIN_OUT))
				w.dst = { hover.part, hover.pin };
		
		// remember temprary end point for wire if not hovered over pin
		// convert cursor position to chip space and _then_ snap
		// this makes it so that we always snap in the space of the chip
		w.unconn_pos = snap(w.world2chip * _cursor_pos);
		
		// establish wire connection or add wire point
		if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
			if (w.src.part && w.dst.part) {
				// clicked correct pin, connect wire
				add_wire(w);
				
				mode = EditMode();
			}
			else {
				// add cur cursor position to list of wire points
				if (w.dir) w.points.insert(w.points.begin(), w.unconn_pos);
				else       w.points.push_back(w.unconn_pos);
			}
		}
		// remove wire point or cancel rewiring
		else if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
			// undo one wire point or cancel wire
			if (w.points.empty()) {
				// cancel whole wire edit
				mode = EditMode();
			}
			else {
				// just remove last added point
				if (w.dir) w.points.erase(w.points.begin());
				else       w.points.pop_back();
			}
		}
	}
}

void Editor::update_toggle_gate (Input& I, LogicSim& sim, Window& window) {
	
	bool can_toggle = in_mode<ViewMode>() &&
		hover.type == Hover::PART && is_gate(hover.part->chip);

	if (in_mode<ViewMode>()) {
		auto& v = std::get<ViewMode>(mode);
		
		if (!v.toggle_gate && can_toggle && I.buttons[MOUSE_BUTTON_LEFT].went_down) {
			v.toggle_gate = hover;
			v.state_toggle_value = !sim.state[sim.cur_state][v.toggle_gate.part_state_idx];
		}
		if (v.toggle_gate) {
			sim.state[sim.cur_state][v.toggle_gate.part_state_idx] = v.state_toggle_value;
	
			if (I.buttons[MOUSE_BUTTON_LEFT].went_up)
				v.toggle_gate = {};
		}
	}
	
	window.set_cursor(can_toggle ? Window::CURSOR_FINGER : Window::CURSOR_NORMAL);
}
	
} // namespace logic_sim
