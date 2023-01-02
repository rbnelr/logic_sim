#include "common.hpp"
#include "logic_sim.hpp"
#include "game.hpp"
#include "opengl/renderer.hpp"

namespace logic_sim {
	
////
void simulate_chip (Chip& chip, int state_base, uint8_t* cur, uint8_t* next) {
		
	int sid = state_base;
	
	for (auto& part : chip.outputs) {
		assert(part->chip == &gates[OUT_PIN]);
		assert(part->chip->state_count == 1);
		
		Part* src_a = part->inputs[0].part;

		if (!src_a) {
			// keep prev state (needed to toggle gates via LMB)
			next[sid] = cur[sid];
		}
		else {
			bool new_state = src_a && cur[state_base + src_a->sid + part->inputs[0].pin] != 0;
			
			next[sid] = new_state;
		}

		sid += 1;
	}
	
	// skip inputs which were updated by caller
	sid += (int)chip.inputs.size();

	for (auto& part : chip.parts) {
		int input_count = (int)part->chip->inputs.size();

		if (!is_gate(part->chip)) {
			int output_count = (int)part->chip->outputs.size();

			for (int i=0; i<input_count; ++i) {
				Part* src = part->inputs[i].part;
					
				uint8_t new_state;

				if (!src) {
					// keep prev state (needed to toggle gates via LMB)
					new_state = cur[sid + output_count + i] != 0;
				}
				else {
					// read input connection
					new_state = cur[state_base + src->sid + part->inputs[i].pin] != 0;
					// write input part state
				}

				next[sid + output_count + i] = new_state;
			}

			simulate_chip(*part->chip, sid, cur, next);
		}
		else {
			auto type = gate_type(part->chip);

			assert(type != INP_PIN && type != OUT_PIN);
			
			assert(part->chip->state_count == 1);
			
			// TODO: cache part_idx in input as well to avoid indirection
			Part* src_a = input_count >= 1 ? part->inputs[0].part : nullptr;
			Part* src_b = input_count >= 2 ? part->inputs[1].part : nullptr;
			Part* src_c = input_count >= 3 ? part->inputs[2].part : nullptr;

			if (!src_a && !src_b) {
				// keep prev state (needed to toggle gates via LMB)
				next[sid] = cur[sid];
			}
			else {
				bool a = src_a && cur[state_base + src_a->sid + part->inputs[0].pin] != 0;
				bool b = src_b && cur[state_base + src_b->sid + part->inputs[1].pin] != 0;
				bool c = src_c && cur[state_base + src_c->sid + part->inputs[2].pin] != 0;
					
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

				next[sid] = new_state;
			}

			// don't recurse
		}

		sid += part->chip->state_count;
	}
	
	assert(chip.state_count >= 0); // state_count stale!
	assert(sid - state_base == chip.state_count); // state_count invalid!
}

void LogicSim::simulate (Input& I) {
	ZoneScoped;
	
	uint8_t* cur  = state[cur_state  ].data();
	uint8_t* next = state[cur_state^1].data();

	// keep prev state (needed to toggle gates via LMB)
	for (auto& part : viewed_chip->inputs) {
		next[part->sid] = cur[part->sid] != 0;
	}

	simulate_chip(*viewed_chip, 0, cur, next);

	cur_state ^= 1;
}

////
Chip Chip::deep_copy () const {
	Chip c;
	c.name = name;
	c.col  = col;
	c.size = size;
			
	// TODO: Is this really needed? How else to deep copy links correctly?
	// -> Good reason to use indices instead of pointers again?
	std::unordered_map<Part*, Part*> map; // old* -> new*
			
	auto create_part = [&] (Part* old) {
		auto new_ = std::make_unique<Part>(old->chip, std::string(old->name), old->pos);
		map.emplace(old, new_.get());
		return new_;
	};
	auto deep_copy = [&] (Part* old, Part* new_) {
		for (int i=0; i<(int)old->chip->inputs.size(); ++i) {
			new_->inputs[i].part        = map[old->inputs[i].part];
			new_->inputs[i].pin         = old->inputs[i].pin;
			new_->inputs[i].wire_points = old->inputs[i].wire_points;
		}
	};

	c.outputs.reserve(outputs.size());
	c.inputs .reserve(inputs .size());
	c.parts  .reserve(parts  .size());

	for (auto& p : outputs)
		c.outputs.emplace_back( create_part(p.get()) );
			
	for (auto& p : inputs)
		c.inputs .emplace_back( create_part(p.get()) );

	for (auto& p : parts)
		c.parts  .add( create_part(p.get()) );
			
	for (size_t i=0; i<outputs.size(); ++i) {
		deep_copy(c.outputs[i].get(), outputs[i].get());
	}
	for (size_t i=0; i<inputs.size(); ++i) {
		deep_copy(c.inputs[i].get(), inputs[i].get());
	}
	for (auto& old : parts) {
		deep_copy(old.get(), map[old.get()]);
	}

	return c;
}

void _add_user_to_chip_deps (Chip* chip, Chip* user) {
	for (auto& p : chip->parts) {
		if (!is_gate(p->chip)) {
			// iterate unique recursive dependencies
			if (p->chip->users.try_add(user)) {
				_add_user_to_chip_deps(p->chip, user);
			}
		}
	}
}
void LogicSim::recompute_chip_users () {
	for (auto& chip : saved_chips) {
		chip->users.clear();
	}
	for (auto& chip : saved_chips) {
		_add_user_to_chip_deps(chip.get(), chip.get()); // add chip to all its recursive dependencies
	}
}

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
			ij["pin_idx"] = part.inputs[i].pin;

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

	// HACK: const-cast, only way around this would be not using const in json lib
	//  or using global bool, which I want to avoid
	((LogicSim&)sim).unsaved_changes = false;
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
				inp.pin = inpj.at("pin_idx");
				
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
	
	chip.parts = {};
	chip.parts.reserve((int)jparts.size());
	
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
		chip.parts.add(ptr);
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
void from_json (const json& j, LogicSim& sim) {
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

	sim.recompute_chip_users();

	int viewed_chip_idx = j["viewed_chip"];
	if (viewed_chip_idx >= 0) {
		sim.switch_to_chip_view( sim.saved_chips[viewed_chip_idx]);
	}
}

////

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

void Editor::viewed_chip_imgui (LogicSim& sim, Camera2D& cam) {

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

	ImGui::Separator();
	int users = sim.viewed_chip->users.size();

	ImGui::PushStyleColor(ImGuiCol_Button,        (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.0f, 0.7f, 0.7f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,  (ImVec4)ImColor::HSV(0.0f, 0.8f, 0.8f));

	if (users == 0) {
		bool clicked = ImGui::ButtonEx("Delete###Del");
		
		ImGui::PopStyleColor(3);
		
		if (imgui_delete_confirmation(sim.viewed_chip->name.c_str(), clicked) == GuiConfirm::YES) {
			sim.delete_chip(sim.viewed_chip.get(), cam);
		}
	}
	else {
		ImGui::BeginDisabled();
		ImGui::ButtonEx(prints("Delete (Still %d Users)###Del", users).c_str());
		ImGui::EndDisabled();
		
		ImGui::PopStyleColor(3);
	}
}

void Editor::saved_chips_imgui (LogicSim& sim, Camera2D& cam) {
	
	if (ImGui::Button("Clear View")) {
		// discard main_chip contents by resetting main_chip to empty
		sim.reset_chip_view(cam);
		reset();
	}
	ImGui::SameLine();
	
	int idx = indexof_chip(sim.saved_chips, sim.viewed_chip.get());
	bool dupl = idx >= 0;

	if (ImGui::Button(dupl ? "Duplicate###_Save" : "Save as New###_Save")) {
		if (!sim.viewed_chip->name.empty()) {
			// save chip as new, duplicate it if it already was a saved chip
			
			std::shared_ptr<Chip> saved_chip;
			if (dupl) {
				// save copy after current entry
				saved_chip = std::make_shared<Chip>( sim.viewed_chip->deep_copy() );
				sim.saved_chips.emplace(sim.saved_chips.begin() + idx + 1, saved_chip);
			}
			else {
				// save new chip at end of list
				saved_chip = sim.viewed_chip;
				sim.saved_chips.emplace_back(saved_chip);
			}

			sim.switch_to_chip_view(saved_chip);
		}
		else {
			ImGui::OpenPopup("POPUP_SaveAsNewEnterName");
		}
	}
	if (ImGui::BeginPopup("POPUP_SaveAsNewEnterName")) {
		ImGui::Text("Please enter a name for the Chip before saving it.");
		ImGui::EndPopup();
	}
	
	//int delete_item = -1;

	if (ImGui::BeginTable("ChipsList", 1, ImGuiTableFlags_Borders)) {
		
		bool was_already_dragging = chips_reorder_src >= 0;
		int chips_reorder_targ = -1;
		
		const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();
		bool reorder_was_drawn = false;

		bool want_drag = chips_reorder_src < 0 &&
			ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left, 10); // TODO: how to now drag on double click

		for (int i=0; i<(int)sim.saved_chips.size(); ++i) {
			auto& chip = sim.saved_chips[i];
			
			bool selected = in_mode<PlaceMode>() && std::get<PlaceMode>(mode).preview_part.chip == chip.get();
			bool can_place = !sim.viewed_chip->users.contains(chip.get());
			bool is_viewed = chip.get() == sim.viewed_chip.get();

			if (i == chips_reorder_src) {
				// skip item when reodering it
			}
			else {
				ImGui::TableNextColumn();
				ImGui::PushID(i);
				
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
						sim.switch_to_chip_view(chip);
						sim.adjust_camera_for_viewed_chip(cam);

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
				if (want_drag && ImGui::IsItemActive()) {
					chips_reorder_src = i;
				}

				ImGui::PopID();
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
					
			viewed_chip_imgui(sim, cam);
			
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

void delete_subpart_popup (bool open) {
	if (open) {
		ImGui::OpenPopup("POPUP_DeleteSubpart");
	}
	if (ImGui::BeginPopup("POPUP_DeleteSubpart")) {
		ImGui::Text("Removal of parts only supported when viewing containing chip.");
		ImGui::EndPopup();
	}
}

void Editor::add_part (LogicSim& sim, Chip& chip, PartPreview& part) {
	assert(&chip == sim.viewed_chip.get());

	auto* ptr = new Part(part.chip, "", part.pos);

	if (part.chip == &gates[OUT_PIN]) {
		// insert output at end of outputs list

		chip.outputs.emplace_back(ptr);
	}
	else if (part.chip == &gates[INP_PIN]) {
		// insert input at end of inputs list
		
		// resize input array of every part of this chip type
		int count = (int)chip.inputs.size();
		for (auto& schip : sim.saved_chips) {
			for (auto& part : schip->parts) {
				if (part->chip == &chip) {
					part->inputs = insert(part->inputs, count, count, {});
				}
			}
		}

		chip.inputs.emplace_back(ptr);
	}
	else {
		// insert part at end of parts list
		chip.parts.add(ptr);
	}
	
	// insert states for part  TODO: this is wrong if anything but the viewed chip (top level state) is edited (which I currently can't do)
	// -> need to fold this state insertion into the update_all_chip_state_indices() function because it knows exactly where states need to be inserted or erased
	//assert(part.chip->state_count > 0);
	//for (int i=0; i<2; ++i)
	//	sim.state[i].insert(sim.state[i].begin() + state_idx, part.chip->state_count, 0);
	
	// update state indices
	sim.update_all_chip_state_indices();

	// TODO
	for (int i=0; i<2; ++i)
		sim.state[i].assign(sim.viewed_chip->state_count, 0);
	
	sim.recompute_chip_users();
	
	sim.unsaved_changes = true;
}
void Editor::remove_part (LogicSim& sim, Chip* chip, Part* part) {
	assert(chip == sim.viewed_chip.get());

	// remove wire connections to this part
	for (auto& p : chip->outputs) {
		for (int i=0; i<(int)p->chip->inputs.size(); ++i) {
			if (p->inputs[i].part && p->inputs[i].part == part) {
				remove_wire(sim, chip, { p.get(), i });
			}
		}
	}
	for (auto& p : chip->parts) {
		for (int i=0; i<(int)p->chip->inputs.size(); ++i) {
			if (p->inputs[i].part && p->inputs[i].part == part) {
				remove_wire(sim, chip, { p.get(), i });
			}
		}
	}

	if (part->chip == &gates[OUT_PIN]) {
		int idx = indexof(chip->outputs, part, Partptr_equal());
		assert(idx >= 0);
		
		// remove wires to this chip output pin in all chips using this chip as a part
		for (auto& schip : sim.saved_chips) {
			for (auto& p : schip->parts) {
				for (int i=0; i<(int)p->chip->inputs.size(); ++i) {
					if (p->inputs[i].part && p->inputs[i].part->chip == chip && p->inputs[i].pin == idx) {
						remove_wire(sim, chip, { p.get(), i });
					}
				}
			}
		}

		chip->outputs.erase(chip->outputs.begin() + idx);
	}
	else if (part->chip == &gates[INP_PIN]) {
		int idx = indexof(chip->inputs, part, Partptr_equal());
		assert(idx >= 0);
	
		// resize input array of every part of this chip type
		int count = (int)chip->inputs.size();
		for (auto& schip : sim.saved_chips) {
			for (auto& part : schip->parts) {
				if (part->chip == chip) {
					part->inputs = erase(part->inputs, count, idx);
				}
			}
		}

		chip->inputs.erase(chip->inputs.begin() + idx);
	}
	else {
		chip->parts.try_remove(part);
	}

	sim.update_all_chip_state_indices();

	// TODO
	for (int i=0; i<2; ++i)
		sim.state[i].assign(sim.viewed_chip->state_count, 0);

	sim.recompute_chip_users();

	sim.unsaved_changes = true;
}

void Editor::add_wire (LogicSim& sim, Chip* chip, WireConn src, WireConn dst, std::vector<float2>&& wire_points) {
	assert(src.part && dst.part);
	assert(src.pin < (int)src.part->chip->outputs.size());
	assert(dst.pin < (int)dst.part->chip->inputs.size());
	assert(chip->contains_part(src.part));
	assert(chip->contains_part(dst.part));

	dst.part->inputs[dst.pin] = { src.part, src.pin, std::move(wire_points) };

	sim.unsaved_changes = true;
}
void Editor::remove_wire (LogicSim& sim, Chip* chip, WireConn dst) {

	assert(dst.part && chip->contains_part(dst.part));
	assert(dst.pin < (int)dst.part->chip->inputs.size());
	auto& src = dst.part->inputs[dst.pin];

	assert(src.part && chip->contains_part(src.part));
	assert(src.pin < (int)dst.part->chip->outputs.size());;

	assert(dst.part->inputs[dst.pin].part == src.part && dst.part->inputs[dst.pin].pin == src.pin);

	dst.part->inputs[dst.pin] = {};

	sim.unsaved_changes = true;
}

void edit_placement (LogicSim& sim, Input& I, Placement& p, float2 center=0) {
	if (I.buttons['R'].went_down) {
		int dir = I.buttons[KEY_LEFT_SHIFT].is_down ? -1 : +1;
		p.rotate_around(center, dir);
		sim.unsaved_changes = true;
	}
	if (I.buttons['M'].went_down) {
		p.mirror_around(center);
		sim.unsaved_changes = true;
	}
}

constexpr float part_text_sz = 20;
constexpr float pin_text_sz = 14;

constexpr lrgba preview_box_col = lrgba(1, 1, 1, 0.5f);
constexpr lrgba pin_col         = lrgba(1, 1, 0, 1);
constexpr lrgba named_part_col  = lrgba(0.3f, 0.2f, 1, 1);
constexpr lrgba hover_col       = lrgba(0.3f, 0.2f, 1, 1);
constexpr lrgba sel_col         = lrgba(0, 1, 1, 1);
constexpr lrgba multisel_col    = lrgba(0, 1, 1, 0.5f);

std::string_view part_name (Part& part, std::string& buf) {
	if (part.chip == &gates[OUT_PIN] || part.chip == &gates[INP_PIN])
		return part.name;
	if (part.name.empty())
		return part.chip->name;
	buf = prints("%s (%s)", part.chip->name.c_str(), part.name.c_str());
	return buf;
}

void highlight_pin (ogl::Renderer& r, Part* part, int pin_idx, bool is_inp, float2x3 const& part2world, lrgba col) {
	auto& pin = is_inp ? *part->chip->inputs [pin_idx] :
	                     *part->chip->outputs[pin_idx];
	
	auto pos = is_inp ? get_inp_pos(pin) : get_out_pos(pin);

	auto mat = part2world * translate(pos);
		
	r.draw_highlight_box(PIN_SIZE, mat, col * pin_col);
	r.draw_highlight_text(PIN_SIZE, mat, pin.name, part_text_sz, pin_col);
}
void highlight_chip_names (ogl::Renderer& r, Chip& chip, float2x3 const& chip2world) {
	if (&chip != &gates[OUT_PIN]) {
		for (int i=0; i<(int)chip.outputs.size(); ++i) {
			auto& pin = *chip.outputs[i];
			r.draw_text(pin.name, chip2world * get_out_pos(pin), pin_text_sz, pin_col, 0.5f, 0.4f);
		}
	}
	if (&chip != &gates[INP_PIN]) {
		for (int i=0; i<(int)chip.inputs.size(); ++i) {
			auto& pin = *chip.inputs[i];
			r.draw_text(pin.name, chip2world * get_inp_pos(pin), pin_text_sz, pin_col, 0.5f, 0.4f);
		}
	}
			
	for (auto& part : chip.parts) {
		if (!part->name.empty())
			r.draw_text(part->name, chip2world * part->pos.pos, part_text_sz, named_part_col, 0.5f, 0.4f);
	}
}

// find last (depth first search) hovered part or pin, where pins have priority over parts
void Editor::find_hover (Chip& chip, SelectInput& I,
		float2x3 const& chip2world, float2x3 const& world2chip, int state_base) {

	// state index
	int sid = state_base;

	auto chip_id = ChipInstanceID{ &chip, state_base };
	
	auto edit_part = [&] (Part& part) {
		auto part2world = chip2world * part.pos.calc_matrix();
		auto world2part = part.pos.calc_inv_matrix() * world2chip;
		
		// only hover parts that are part of the desired chip instance
		if (!I.only_chip || I.only_chip == chip_id) {
			// check part pins for hover
			if (I.allow_pins) {
				if (part.chip != &gates[OUT_PIN]) {
					for (int i=0; i<(int)part.chip->outputs.size(); ++i) {
						if (hitbox(PIN_SIZE, get_out_pos_invmat(*part.chip->outputs[i]) * world2part))
							hover = { Hover::PIN_OUT, chip_id, &part, i, chip2world, world2chip };
					}
				}
				if (part.chip != &gates[INP_PIN]) {
					for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
						if (hitbox(PIN_SIZE, get_inp_pos_invmat(*part.chip->inputs[i]) * world2part))
							hover = { Hover::PIN_INP, chip_id, &part, i, chip2world, world2chip };
					}
				}
			}
		
			// check part hitbox
			//bool hit = hitbox(part.chip->size, world2part);

			// hover part if hitbox hit and _not_ yet hovering a pin (pins act as a prioritized layer for hovering)
			bool hovering_pin = hover.type == Hover::PIN_OUT || hover.type == Hover::PIN_INP;
			if (I.allow_parts && !hovering_pin && hitbox(part.chip->size, world2part))
				hover = { Hover::PART, chip_id, &part, -1, chip2world, world2chip };
		}

		// recursivly check custom chips _only if_ their bounds were hit
		// I.only_chip != &chip: if only allow this part we don't need to recurse further
		//if (hit && !is_gate(part.chip)) { // optimization
		if (!is_gate(part.chip)) {
			find_hover(*part.chip, I, part2world, world2part, sid);
		}

		sid += part.chip->state_count;
	};

	for (auto& part : chip.outputs) {
		edit_part(*part);
	}
	for (auto& part : chip.inputs) {
		edit_part(*part);
	}

	for (auto& part : chip.parts) {
		edit_part(*part);
	}
}

void Editor::find_boxsel (Chip& chip, bool remove, AABB box,
		float2x3 const& chip2world, float2x3 const& world2chip, int state_base,
		PartSelection& sel) {
	
	// state index
	int sid = state_base;

	auto chip_id = ChipInstanceID{ &chip, state_base };
	
	auto edit_part = [&] (Part& part) {
		//auto part2world = chip2world * part.pos.calc_matrix();
		//auto world2part = part.pos.calc_inv_matrix() * world2chip;
		//
		//if (I.allow_pins) {
		//	if (part.chip != &gates[OUT_PIN]) {
		//		for (int i=0; i<(int)part.chip->outputs.size(); ++i) {
		//			if (box.is_inside(get_out_pos(*part.chip->inputs[i])))
		//				sel = { Hover::PIN_OUT, chip_id, &part, i, chip2world, world2chip };
		//		}
		//	}
		//	if (part.chip != &gates[INP_PIN]) {
		//		for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
		//			if (box.is_inside(get_inp_pos(*part.chip->inputs[i])))
		//				hover = { Hover::PIN_INP, chip_id, &part, i, chip2world, world2chip };
		//		}
		//	}
		//}


		if (box.is_inside(part.pos.pos)) {
			if (remove)
				sel.items.try_remove(&part);
			else
				sel.items.try_add(PartSelection::Item{ &part });
		}

		//if (!is_gate(part.chip)) {
		//	find_hover(*part.chip, I, part2world, world2part, sid);
		//}

		sid += part.chip->state_count;
	};

	for (auto& part : chip.outputs) {
		edit_part(*part);
	}
	for (auto& part : chip.inputs) {
		edit_part(*part);
	}

	for (auto& part : chip.parts) {
		edit_part(*part);
	}
}

void Editor::update (Input& I, LogicSim& sim, ogl::Renderer& r) {
	// E toggles between edit and view mode (other modes are always exited)
	if (I.buttons['E'].went_down)
		toggle_edit_mode(*this);
	
	if (I.buttons['K'].went_down)
		printf(""); // for debugging

	{ // Get cursor position
		float3 cur_pos = 0;
		_cursor_valid = !ImGui::GetIO().WantCaptureMouse && r.view.cursor_ray(I, &cur_pos);
		_cursor_pos = (float2)cur_pos;
	}

	// compute hover
	hover = {};

	SelectInput in;
	in.only_chip = {};
	in.allow_pins  = in_mode<EditMode>() || in_mode<WireMode>();
	in.allow_parts = !in_mode<PlaceMode>();

	if (in_mode<EditMode>() && I.buttons[KEY_LEFT_SHIFT].is_down) {
		auto& e = std::get<EditMode>(mode);
		if (e.sel) {
			// only allow parts of this chip instance to be added/removed on this multiselect with shift-click
			in.only_chip  = e.sel.chip;
			in.allow_pins = false;
		}
	}
	else if (in_mode<WireMode>()) {
		auto& w = std::get<WireMode>(mode);

		// only allow parts of this chip instance to be added/removed on this multiselect with shift-click
		in.only_chip  = w.chip;
		in.allow_pins = true;
	}

	if (_cursor_valid) {
		find_hover(*sim.viewed_chip, in, float2x3::identity(), float2x3::identity(), 0);
	}
	
	PartSelection boxsel = {};

	if (in_mode<PlaceMode>()) {
		auto& preview_part = std::get<PlaceMode>(mode).preview_part;
		
		edit_placement(sim, I, preview_part.pos);
		
		if (_cursor_valid) {
			
			// move to-be-placed gate preview with cursor
			preview_part.pos.pos = snap(_cursor_pos);
			
			r.draw_highlight_box(preview_part.chip->size, preview_part.pos.calc_matrix(), preview_box_col);
			
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
			e.box_selecting = false;
		}
		
		bool ctrl  = I.buttons[KEY_LEFT_CONTROL].is_down;
		bool shift = I.buttons[KEY_LEFT_SHIFT].is_down;
		auto& lmb  = I.buttons[MOUSE_BUTTON_LEFT];

		if (lmb.went_down) {
			if (!hover) {
				
				if (!shift && !ctrl) {
					e.sel = {};
				}
				
				e.box_selecting = true;
				e.box_sel_start = _cursor_pos;
			}
			else if (hover.type == Hover::PIN_INP || hover.type == Hover::PIN_OUT) {
				if (!shift && !ctrl) {

					mode = WireMode{ hover.chip, hover.world2chip,
						hover.type == Hover::PIN_INP, { hover.part, hover.pin } };
			
					if (hover.type == Hover::PIN_INP && hover.part->inputs[hover.pin].part)
						remove_wire(sim, hover.chip.ptr, { hover.part, hover.pin });
					
					I.buttons[MOUSE_BUTTON_LEFT].went_down = false; // consume click to avoid wire mode also getting click?
				}
			}
			else {
				assert(hover.type == Hover::PART);

				if (shift && e.sel) {
					assert(hover.chip == e.sel.chip); // enforced in find_hover() TODO?
					e.sel.toggle_part(hover.part);
				}
				else if (ctrl && e.sel) {
					
				}
				else {
					// non-shift click on already selected part (only on same instance or else dragging might use wrong matrix)
					if (e.sel.inst_contains_inst(hover)) {
						// keep multiselect if clicked on already selected item
					}
					// non-shift click on non-selected part, replace selection
					else {
						e.sel = { hover.chip, {{ hover.part }}, hover.chip2world, hover.world2chip };
					}
				}
			}

			//// normal click or shift-click with no current selection
			//if (!shift || !e.sel) {
			//	// unselect via click on background
			//	if (!hover) {
			//		e.sel = {};
			//		
			//		e.box_selecting = true;
			//		e.box_sel_start = _cursor_pos;
			//	}
			//	// begin wiring with hovered pin as start
			//	else if (hover.type == Hover::PIN_INP || hover.type == Hover::PIN_OUT) {
			//		mode = WireMode{ hover.chip, hover.world2chip,
			//			hover.type == Hover::PIN_INP, { hover.part, hover.pin } };
			//
			//		if (hover.type == Hover::PIN_INP && hover.part->inputs[hover.pin].part)
			//			remove_wire(sim, hover.chip.ptr, { hover.part, hover.pin });
			//		
			//		I.buttons[MOUSE_BUTTON_LEFT].went_down = false; // consume click to avoid wire mode also getting click?
			//	}
			//	// clicked part
			//	else {
			//		assert(hover.type == Hover::PART);
			//		// non-shift click on already selected part (only on same instance or else dragging might use wrong matrix)
			//		if (e.sel.inst_contains_inst(hover)) {
			//			// keep multiselect if clicked on already selected item
			//		}
			//		// non-shift click on non-selected part, replace selection
			//		else {
			//			e.sel = { hover.chip, {{ hover.part }}, hover.chip2world, hover.world2chip };
			//		}
			//	}
			//}
			//// shift clicked to add/remove from selection
			//else {
			//	assert(e.sel && !e.sel.items.empty());
			//	// shift clicked part
			//	if (hover.type == Hover::PART) {
			//		assert(hover.chip == e.sel.chip); // enforced in find_hover()
			//		e.sel.toggle_part(hover.part);
			//	}
			//	else {
			//		// ignore click for pins or on background
			//	}
			//}
		}
		
		if (in_mode<EditMode>()) { // still in edit mode? else e becomes invalid
			if (e.box_selecting) {

				bool remove = false;

				// shift lmb: add to selection
				// ctrl  lmb: remove from selection
				if (shift || ctrl) {
					remove = ctrl;
				}
				// just lmb: replace selection
				else {
					e.sel = {};
				}
				
				boxsel = e.sel;
				if (!boxsel) 
					boxsel = { {sim.viewed_chip.get(), 0}, {}, float2x3::identity(), float2x3::identity() };

				float2 lo = min(e.box_sel_start, _cursor_pos);
				float2 hi = max(e.box_sel_start, _cursor_pos);
				float2 size = hi - lo;
				
				find_boxsel(*boxsel.chip.ptr, remove, AABB{lo,hi}, boxsel.chip2world, boxsel.world2chip, 0, boxsel);
				
				r.dbgdraw.wire_quad(float3(lo, 0), size, multisel_col);

				if (lmb.went_up) {
					e.sel = boxsel;

					e.box_selecting = false;
				}
			}
		}

		// edit parts
		if (in_mode<EditMode>() && e.sel) { // still in edit mode? else e becomes invalid

			float2 bounds_center;
			{
				e.sel.bounds = AABB::inf();

				for (auto& item : e.sel.items) {
					e.sel.bounds.add(item.part->get_aabb(0.05f));
				}

				bounds_center = (e.sel.bounds.lo + e.sel.bounds.hi) * 0.5f;
				
				for (auto& item : e.sel.items) {
					item.bounds_offs = item.part->pos.pos - bounds_center;
				}
			}
			
			if (shift || ctrl) {
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
						if (length_sqr(e.drag_offset) > 0) {
							for (auto& item : e.sel.items)
								item.part->pos.pos = item.bounds_offs + bounds_center;

							sim.unsaved_changes = true;
						}
					}
					// stop dragging gate
					else {
						e.dragging = false;
					}
				}
			}

			// TODO: rotate around mouse cursor when dragging?
			for (auto& i : e.sel.items) {
				edit_placement(sim, I, i.part->pos, bounds_center);
			}

			// Duplicate selected part with CTRL+C
			// TODO: CTRL+D moves the camera to the right, change that?
			//if (I.buttons[KEY_LEFT_CONTROL].is_down && I.buttons['C'].went_down) {
			//	preview_part.chip = sel.part->chip;
			//	mode = PLACE_MODE;
			//}
			
			{ // remove gates via DELETE key
				bool can_delete = e.sel.chip.ptr == sim.viewed_chip.get();

				delete_subpart_popup(I.buttons[KEY_DELETE].went_down && !can_delete);

				if (I.buttons[KEY_DELETE].went_down && can_delete) {
					// avoid stale pointer in hover
					if (e.sel.has_part(e.sel.chip.ptr, hover.part))
						hover = {};
				
					for (auto& i : e.sel.items)
						remove_part(sim, e.sel.chip.ptr, i.part);
				
					e.sel = {};
				}
			}
		}
	}
	
	// still execute wiremode after edit mode switched to it, so that wire shows instantly with correct unconn_pos
	if (in_mode<WireMode>()) {
		auto& w = std::get<WireMode>(mode);
		
		if (hover) assert(hover.chip == w.chip); // enforced by find_hover()

		// connect other end of wire to appropriate pin when hovered
		w.dst = {};
		if (hover.type == Hover::PIN_INP || hover.type == Hover::PIN_OUT)
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
				WireConn src=w.src, dst=w.dst;
				if (w.dir) std::swap(src, dst);
		
				add_wire(sim, w.chip.ptr,
					w.dir ? w.dst : w.src,
					w.dir ? w.src : w.dst,
					std::move(w.points));
				
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
	
//// Highlights
	highlight_chip_names(r, *sim.viewed_chip, float2x3::identity());
	
	if (in_mode<EditMode>()) {
		auto& e = std::get<EditMode>(mode);
		
		PartSelection& sel = e.box_selecting ? boxsel : e.sel;

		if (sel) {
			std::string buf;

			// only show text for single-part selections as to not spam too much text
			if (sel.items.size() == 1) {
				auto& item = sel.items[0];

				auto part2world = sel.chip2world * item.part->pos.calc_matrix();

				r.draw_highlight_box(item.part->chip->size, part2world, sel_col);
				r.draw_highlight_text(item.part->chip->size, part2world, part_name(*item.part, buf), part_text_sz, sel_col);
			}
			else {
				for (auto& item : sel.items) {
					auto part2world = sel.chip2world * item.part->pos.calc_matrix();

					r.draw_highlight_box(item.part->chip->size, part2world, sel_col);
				}
				
				//if (!e.box_selecting) {
				//	float2 sz     =  sel.bounds.hi - sel.bounds.lo;
				//	float2 center = (sel.bounds.hi + sel.bounds.lo) * 0.5f;
				//
				//	auto part2world = sel.chip2world * translate(center);
				//
				//	r.draw_highlight_box(sz, part2world, multisel_col);
				//}
			}
		}

	}

	if (hover) {
		auto part2world = hover.chip2world * hover.part->pos.calc_matrix();
		
		if (hover.type == Editor::Hover::PART) {
			highlight_chip_names(r, *hover.part->chip, part2world);
		}
		else {
			highlight_pin(r, hover.part, hover.pin, hover.type == Hover::PIN_INP, part2world, lrgba(1));
		}

		{
			std::string buf;

			r.draw_highlight_box(hover.part->chip->size, part2world, lrgba(1));
			r.draw_highlight_text(hover.part->chip->size, part2world, part_name(*hover.part, buf), part_text_sz);
		}
	}
}

void Editor::update_toggle_gate (Input& I, LogicSim& sim, Window& window) {
	
	bool can_toggle = in_mode<ViewMode>() &&
		hover.type == Hover::PART && is_gate(hover.part->chip);
	
	if (in_mode<ViewMode>()) {
		auto& v = std::get<ViewMode>(mode);
		
		if (v.toggle_sid < 0 && can_toggle && I.buttons[MOUSE_BUTTON_LEFT].went_down) {
			v.toggle_sid = hover.chip.sid + hover.part->sid;
			v.state_toggle_value = !sim.state[sim.cur_state][v.toggle_sid];
		}
		if (v.toggle_sid >= 0) {
			sim.state[sim.cur_state][v.toggle_sid] = v.state_toggle_value;
	
			if (I.buttons[MOUSE_BUTTON_LEFT].went_up)
				v.toggle_sid = -1;
		}
	}
	
	window.set_cursor(can_toggle ? Window::CURSOR_FINGER : Window::CURSOR_NORMAL);
}
	
} // namespace logic_sim
