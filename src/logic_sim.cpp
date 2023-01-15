#include "common.hpp"
#include "logic_sim.hpp"
#include "game.hpp"
#include "opengl/renderer.hpp"

namespace logic_sim {
	
////
void simulate_chip (Chip& chip, int state_base, uint8_t* cur, uint8_t* next) {
		
	//int sid = state_base;
	//
	//for (auto& part : chip.outputs) {
	//	assert(part->chip == &gates[OUT_PIN]);
	//	assert(part->chip->state_count == 1);
	//	
	//	Part* src_a = part->inputs[0].part;
	//
	//	if (!src_a) {
	//		// keep prev state (needed to toggle gates via LMB)
	//		next[sid] = cur[sid];
	//	}
	//	else {
	//		bool new_state = src_a && cur[state_base + src_a->sid + part->inputs[0].pin] != 0;
	//		
	//		next[sid] = new_state;
	//	}
	//
	//	sid += 1;
	//}
	//
	//// skip inputs which were updated by caller
	//sid += (int)chip.inputs.size();
	//
	//for (auto& part : chip.parts) {
	//	int input_count = (int)part->chip->inputs.size();
	//
	//	if (!is_gate(part->chip)) {
	//		int output_count = (int)part->chip->outputs.size();
	//
	//		for (int i=0; i<input_count; ++i) {
	//			Part* src = part->inputs[i].part;
	//				
	//			uint8_t new_state;
	//
	//			if (!src) {
	//				// keep prev state (needed to toggle gates via LMB)
	//				new_state = cur[sid + output_count + i] != 0;
	//			}
	//			else {
	//				// read input connection
	//				new_state = cur[state_base + src->sid + part->inputs[i].pin] != 0;
	//				// write input part state
	//			}
	//
	//			next[sid + output_count + i] = new_state;
	//		}
	//
	//		simulate_chip(*part->chip, sid, cur, next);
	//	}
	//	else {
	//		auto type = gate_type(part->chip);
	//
	//		assert(type != INP_PIN && type != OUT_PIN);
	//		
	//		assert(part->chip->state_count == 1);
	//		
	//		// TODO: cache part_idx in input as well to avoid indirection
	//		Part* src_a = input_count >= 1 ? part->inputs[0].part : nullptr;
	//		Part* src_b = input_count >= 2 ? part->inputs[1].part : nullptr;
	//		Part* src_c = input_count >= 3 ? part->inputs[2].part : nullptr;
	//
	//		if (!src_a && !src_b) {
	//			// keep prev state (needed to toggle gates via LMB)
	//			next[sid] = cur[sid];
	//		}
	//		else {
	//			bool a = src_a && cur[state_base + src_a->sid + part->inputs[0].pin] != 0;
	//			bool b = src_b && cur[state_base + src_b->sid + part->inputs[1].pin] != 0;
	//			bool c = src_c && cur[state_base + src_c->sid + part->inputs[2].pin] != 0;
	//				
	//			uint8_t new_state;
	//			switch (type) {
	//				case BUF_GATE : new_state =  a;     break;
	//				case NOT_GATE : new_state = !a;     break;
	//				
	//				case AND_GATE : new_state =   a && b;    break;
	//				case NAND_GATE: new_state = !(a && b);   break;
	//				
	//				case OR_GATE  : new_state =   a || b;    break;
	//				case NOR_GATE : new_state = !(a || b);   break;
	//				
	//				case XOR_GATE : new_state =   a != b;    break;
	//
	//				case AND3_GATE : new_state =   a && b && c;    break;
	//				case NAND3_GATE: new_state = !(a && b && c);   break;
	//				
	//				case OR3_GATE  : new_state =   a || b || c;    break;
	//				case NOR3_GATE : new_state = !(a || b || c);   break;
	//
	//				default: assert(false);
	//			}
	//
	//			next[sid] = new_state;
	//		}
	//
	//		// don't recurse
	//	}
	//
	//	sid += part->chip->state_count;
	//}
	//
	//assert(chip.state_count >= 0); // state_count stale!
	//assert(sid - state_base == chip.state_count); // state_count invalid!
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
		for (int i=0; i<(int)old->pins.size(); ++i) {
			new_->pins[i].node = std::make_unique<WireNode>(*old->pins[i].node);
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

void LogicSim::add_part (Chip& chip, Chip* part_chip, Placement part_pos) {
	assert(&chip == viewed_chip.get());

	auto* ptr = new Part(part_chip, "", part_pos);

	if (part_chip == &gates[INP_PIN]) {
		
		int idx = (int)chip.inputs.size();
		chip.inputs.emplace_back(ptr);

		for (auto& schip : saved_chips) {
			for (auto& part : schip->parts) {
				if (part->chip == &chip) {
					part->pins.emplace(part->pins.begin() + idx,
						std::make_unique<WireNode>(part->pos.calc_matrix() * get_inp_pos(*ptr), part.get()));
				}
			}
		}
	}
	else if (part_chip == &gates[OUT_PIN]) {
		
		chip.outputs.emplace_back(ptr);

		for (auto& schip : saved_chips) {
			for (auto& part : schip->parts) {
				if (part->chip == &chip) {
					part->pins.emplace_back(
						std::make_unique<WireNode>(part->pos.calc_matrix() * get_out_pos(*ptr), part.get()));
				}
			}
		}
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
	update_all_chip_state_indices();
	
	// TODO
	for (int i=0; i<2; ++i)
		state[i].assign(viewed_chip->state_count, 0);

	ptr->update_pins_pos();

	recompute_chip_users();

	unsaved_changes = true;
}
void LogicSim::remove_part (Chip& chip, Part* part) {
	assert(&chip == viewed_chip.get());

	for (auto& pin : part->pins)
		disconnect_wire_node(chip, pin.node.get());

	if (part->chip == &gates[INP_PIN]) {
		int idx = indexof(chip.inputs, part, _partptr_equal());
		assert(idx >= 0);

		chip.inputs.erase(chip.inputs.begin() + idx);
	
		int i = idx;
		for (auto& schip : saved_chips) {
			for (auto& part : schip->parts) {
				if (part->chip == &chip) {
					disconnect_wire_node(*schip, part->pins[i].node.get());
					part->pins.erase(part->pins.begin() + i);
				}
			}
		}
	}
	else if (part->chip == &gates[OUT_PIN]) {
		int idx = indexof(chip.outputs, part, _partptr_equal());
		assert(idx >= 0);

		chip.outputs.erase(chip.outputs.begin() + idx);
		
		int i = idx + (int)chip.inputs.size();
		for (auto& schip : saved_chips) {
			for (auto& part : schip->parts) {
				if (part->chip == &chip) {
					disconnect_wire_node(*schip, part->pins[i].node.get());
					part->pins.erase(part->pins.begin() + i);
				}
			}
		}
	}
	else {
		chip.parts.try_remove(part);
	}

	update_all_chip_state_indices();
	
	// TODO
	for (int i=0; i<2; ++i)
		state[i].assign(viewed_chip->state_count, 0);

	recompute_chip_users();

	unsaved_changes = true;
}

json part2json (LogicSim const& sim, Part& part, std::unordered_map<Part*, int>& part2idx) {
	json j;
	j["chip"] = is_gate(part.chip) ?
			gate_type(part.chip) :
			indexof_chip(sim.saved_chips, part.chip) + GATE_COUNT;
	
	if (!part.name.empty())
		j["name"] = part.name;

	j["pos"] = part.pos;

	return j;
}
json chip2json (const Chip& chip, LogicSim const& sim) {
	std::unordered_map<Part*, int> part2idx;
	part2idx.reserve(chip.inputs.size() + chip.outputs.size() + chip.parts.size());
	
	std::unordered_map<WireNode*, int> node2idx;
	node2idx.reserve(chip.wire_nodes.size() + chip.parts.size()*4); // estimate

	int idx = 0;
	int node_idx = 0;

	for (auto& wire : chip.wire_nodes) {
		node2idx[wire.get()] = node_idx++;
	}

	for (auto& part : chip.outputs) {
		part2idx[part.get()] = idx++;

		for (auto& pin : part->pins) {
			node2idx[pin.node.get()] = node_idx++;
		}
	}
	for (auto& part : chip.inputs) {
		part2idx[part.get()] = idx++;

		for (auto& pin : part->pins) {
			node2idx[pin.node.get()] = node_idx++;
		}
	}
	for (auto& part : chip.parts) {
		part2idx[part.get()] = idx++;

		for (auto& pin : part->pins) {
			node2idx[pin.node.get()] = node_idx++;
		}
	}

	json jouts  = json::array();
	json jinps  = json::array();
	json jparts = json::array();
	json jnodes = json::array();
	json jedges = json::array();
	
	// just serialize the free node positions
	for (auto& node : chip.wire_nodes) {
		jnodes.emplace_back(json{
			{"pos", node->pos}
		});
	}
	// instead of serializing the nodes with their connections
	// serialize just the edges, because it's simpler and avoids the doubly linked graph
	for (auto& edge : chip.wire_edges) {
		jedges.emplace_back(json{
			{"a", node2idx[edge->a]},
			{"b", node2idx[edge->b]},
		});
	}

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
		{"name",       chip.name},
		{"col",        chip.col},
		{"size",       chip.size},
		{"inputs",     std::move(jinps)},
		{"outputs",    std::move(jouts)},
		{"parts",      std::move(jparts)},
		{"wire_nodes", std::move(jnodes)},
		{"wire_edges", std::move(jedges)},
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

Part* json2part (const json& j, LogicSim const& sim, std::vector<WireNode*>& idx2node) {
	
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

	auto* ptr = new Part(part_chip, std::move(name), pos);
	for (auto& pin : ptr->pins) {
		idx2node.emplace_back(pin.node.get());
	}
	return ptr;
}
void json2chip (const json& j, Chip& chip, LogicSim& sim) {
	
	auto& jouts = j.at("outputs");
	auto& jinps = j.at("inputs");
	auto& jparts = j.at("parts");
	auto& jnodes = j.at("wire_nodes");
	auto& jedges = j.at("wire_edges");
	size_t total = jouts.size() + jinps.size() + jparts.size();
	
	chip.parts.reserve((int)jparts.size());

	chip.wire_nodes.reserve((int)jnodes.size());
	chip.wire_edges.reserve((int)jedges.size());

	// first pass to create pointers
	std::vector<Part*> idx2part(total, nullptr);
	
	std::vector<WireNode*> idx2node;
	idx2node.reserve(chip.wire_nodes.size() + chip.parts.size()*4);

	for (auto& j : jnodes) {
		float2 pos = j.at("pos");
		auto node = std::make_unique<WireNode>(pos, nullptr);
		idx2node.emplace_back(node.get());
		chip.wire_nodes.add(std::move(node));
	}

	int idx = 0;
	int out_idx = 0, inp_idx = 0;
	for (auto& j : jouts) {
		auto* ptr = idx2part[idx++] = json2part(j, sim, idx2node);
		chip.outputs[out_idx++] = std::unique_ptr<Part>(ptr);
	}
	for (auto& j : jinps) {
		auto* ptr = idx2part[idx++] = json2part(j, sim, idx2node);
		chip.inputs[inp_idx++] = std::unique_ptr<Part>(ptr);
	}
	for (auto& j : jparts) {
		auto* ptr = idx2part[idx++] = json2part(j, sim, idx2node);
		chip.parts.add(ptr);
	}

	for (auto& j : jedges) {
		auto get = [&] (int idx) -> WireNode* {
			assert(idx >= 0 && idx < (int)idx2node.size());
			return idx2node[idx];
		};
		auto* a = get(j.at("a"));
		auto* b = get(j.at("b"));
		auto edge = std::make_unique<WireEdge>(a, b);
	
		a->edges.add(b);
		b->edges.add(a);
		
		chip.wire_edges.add(std::move(edge));
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
	
	for (auto& chip : sim.saved_chips) {
		for (auto& part : chip->inputs) {
			part->update_pins_pos();
		}
		for (auto& part : chip->outputs) {
			part->update_pins_pos();
		}
		for (auto& part : chip->parts) {
			part->update_pins_pos();
		}
	}

	sim.recompute_chip_users();

	int viewed_chip_idx = j["viewed_chip"];
	if (viewed_chip_idx >= 0) {
		sim.switch_to_chip_view( sim.saved_chips[viewed_chip_idx]);
	}
}

////

void Editor::select_gate_imgui (LogicSim& sim, const char* name, Chip* type) {

	bool selected = in_mode<PlaceMode>() && std::get<PlaceMode>(mode).place_chip == type;
	
	if (ImGui::Selectable(name, &selected)) {
		if (selected) {
			mode = PlaceMode{ type, Placement{} };
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
		bool clicked = ImGui::ButtonEx("Delete Chip###Del");
		
		ImGui::PopStyleColor(3);
		
		if (imgui_delete_confirmation(sim.viewed_chip->name.c_str(), clicked) == GuiConfirm::YES) {
			sim.delete_chip(sim.viewed_chip.get(), cam);
		}
	}
	else {
		ImGui::BeginDisabled();
		ImGui::ButtonEx(prints("Delete Chip (Still %d Users)###Del", users).c_str());
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
			
			sim.unsaved_changes = true;

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
			
			bool selected = in_mode<PlaceMode>() && std::get<PlaceMode>(mode).place_chip == chip.get();
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
							mode = PlaceMode{ chip.get(), Placement{} };
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

				sim.unsaved_changes = true;
			}
			chips_reorder_src = -1;
		}

		ImGui::EndTable();
	}
}

void Editor::selection_imgui (PartSelection& sel) {
	if (sel.items.size() == 0) {
		ImGui::Text("No parts selected");
	}
	else if (sel.items.size() > 1) {
		ImGui::Text("%d Items selected", sel.items.size());
	}
	else if (sel.items[0].type == T_NODE) {
		ImGui::Text("Wire Node selected");
	}
	else if (sel.items[0].type == T_PART) {
		auto& part = *sel.items[0].part;
		
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
}

void Editor::imgui (LogicSim& sim, Camera2D& cam) {
	
	const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();
	
	if (imgui_Header("Editor", true)) {
		
		ImGui::Text("Edit Mode: %s Mode", mode_names[mode.index()]);
		{
			bool m = in_mode<EditMode>();
			if (ImGui::Checkbox("Edit [E]", &m)) {
				if (m) mode = EditMode();
				else   mode = ViewMode();
			}
			ImGui::SameLine();
			m = in_mode<WireMode>();
			if (ImGui::Checkbox("Wire [C]", &m)) {
				if (m) mode = WireMode();
				else   mode = EditMode();
			}
		}
		//{
		//	bool m = !in_mode<Editor::ViewMode>();
		//	if (ImGui::Checkbox("Edit Mode [E]", &m))
		//		toggle_edit_mode(*this);
		//}

		ImGui::Checkbox("snapping", &snapping);
		ImGui::SameLine();
		ImGui::InputFloat("###snapping_size", &snapping_size);
		
		if (imgui_Header("Selection", true)) {
			ImGui::Indent(10);
			
			// Child region so that window contents don't shift around when selection changes
			if (ImGui::BeginChild("SelectionRegion", ImVec2(0, TEXT_BASE_HEIGHT * 9))) {
				if (!in_mode<EditMode>()) {
					ImGui::Text("Not in Edit Mode");
				}
				else {
					auto& e = std::get<EditMode>(mode);
					
					selection_imgui(e.sel);
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
			//ImGui::Indent(10);
			
			if (ImGui::TreeNodeEx("Primitive Parts", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::TextDisabled("Click to Place");
				if (ImGui::BeginTable("TableGates", 4, ImGuiTableFlags_Borders)) {
					
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

					for (auto& entry : entries) {
						ImGui::TableNextColumn();
						if (entry.name)
							select_gate_imgui(sim, entry.name, entry.gate);
					}

					ImGui::EndTable();
				}
				ImGui::TreePop();
			}

			ImGui::Separator();
			
			if (ImGui::TreeNodeEx("Custom Chips", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::TextDisabled("Click to Place, Double Click to View, Drag to reorder");
				saved_chips_imgui(sim, cam);
				ImGui::TreePop();
			}

			//ImGui::Unindent(10);
			ImGui::PopID();
		}
		
		ImGui::PopID();
	}
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
// Edit placement wrapper that works on just position (for WireNode)
// uses Dummy Placement struct
void edit_placement (LogicSim& sim, Input& I, float2& p, float2 center=0) {
	Placement dummy;
	dummy.pos = p;
	edit_placement(sim, I, dummy, center);
	p = dummy.pos;
}

constexpr float part_text_sz = 20;
constexpr float pin_text_sz  = 14;

constexpr lrgba boxsel_box_col  = lrgba(1,1,1, 1);

constexpr lrgba hover_col       = lrgba(1,1,1, 1);
constexpr lrgba sel_col         = lrgba(0.3f, 0.9f, 0.3f, 1);

constexpr lrgba preview_box_col = hover_col * lrgba(1,1,1, 0.5f);
constexpr lrgba pin_col         = lrgba(1, 1, 0, 1);
constexpr lrgba wire_col        = lrgba(1, 1, 0.5f, 1);
constexpr lrgba named_part_col  = lrgba(.3f, .2f, 1, 1);

std::string_view part_name (Part& part, std::string& buf) {
	if (part.chip == &gates[OUT_PIN] || part.chip == &gates[INP_PIN])
		return part.name;
	if (part.name.empty())
		return part.chip->name;
	buf = prints("%s (%s)", part.chip->name.c_str(), part.name.c_str());
	return buf;
}

bool hitbox (float2 cursor_pos, float2 box_size, float2x3 const& world2chip) {
	// cursor in space of chip normalized to size
	float2 p = world2chip * cursor_pos;
	p /= box_size;
	// chip is a [-0.5, 0.5] box in this space
	return p.x >= -0.5f && p.x < 0.5f &&
		p.y >= -0.5f && p.y < 0.5f;
}

void highlight (ogl::Renderer& r, float2 size, float2x3 mat, lrgba col, std::string_view text={}) {
	r.draw_highlight_box(size, mat, col);
	if (!text.empty())
		r.draw_highlight_text(size, mat, text, part_text_sz, col);
}
void highlight_part (ogl::Renderer& r, Part& part, float2x3 const& part2world, lrgba col, bool show_text=true, float shrink=0.0f) {
	std::string buf;
	highlight(r, part.chip->size - shrink*2.0f, part2world, col,
		show_text ? part_name(part, buf) : std::string_view{});
}
void highlight_chip_names (ogl::Renderer& r, Chip& chip, float2x3 const& chip2world) {
	if (&chip != &gates[INP_PIN]) {
		for (int i=0; i<(int)chip.inputs.size(); ++i) {
			auto& pin = *chip.inputs[i];
			r.draw_text(pin.name, chip2world * get_inp_pos(pin), pin_text_sz, pin_col, 0.5f, 0.4f);
		}
	}
	if (&chip != &gates[OUT_PIN]) {
		for (int i=0; i<(int)chip.outputs.size(); ++i) {
			auto& pin = *chip.outputs[i];
			r.draw_text(pin.name, chip2world * get_out_pos(pin), pin_text_sz, pin_col, 0.5f, 0.4f);
		}
	}
			
	for (auto& part : chip.parts) {
		if (!part->name.empty())
			r.draw_text(part->name, chip2world * part->pos.pos, part_text_sz, named_part_col, 0.5f, 0.4f);
	}
}

bool wire_hitbox (float2 cursor_pos, WireEdge& wire) {
	float2 center = (wire.a->pos + wire.b->pos) * 0.5f;
	float2 dir    = wire.b->pos - wire.a->pos;
	float2 size   = float2(length(dir), wire_radius*2);

	float ang = atan2f(dir.y, dir.x);

	float2x3 mat = rotate2(-ang) * translate(-center);

	return hitbox(cursor_pos, size, mat);
}
void highlight_wire (ogl::Renderer& r, WireEdge& wire, lrgba col, float shrink=0.0f) {
	float2 center = (wire.a->pos + wire.b->pos) * 0.5f;
	float2 dir    = wire.b->pos - wire.a->pos;
	float2 size   = float2(length(dir), wire_radius*2);

	float ang = atan2f(dir.y, dir.x);

	float2x3 mat = translate(center) * rotate2(ang);

	r.draw_highlight_box(size, mat, wire_col * col);
}

void highlight (ogl::Renderer& r, ThingPtr ptr, lrgba col, bool show_text, float shrink=0.0f) {
	float pin_size = wire_radius*wire_node_radius_fac*2;

	switch (ptr.type) {
		case T_PART: {
			auto part2world = ptr.part->pos.calc_matrix();
			
			highlight_part(r, *ptr.part, part2world, col, show_text, shrink);
		} break;
		case T_NODE: {
			if (ptr.node->parent_part) {
				highlight_part(r, *ptr.node->parent_part, ptr.node->parent_part->pos.calc_matrix(), col, false, shrink);
			}

			std::string_view name;
			if (show_text) {
				// TODO: pin name?
				name = "<wire_node>";
			}

			highlight(r, pin_size - shrink*2.0f, translate(ptr.node->pos), col * pin_col, name);
		} break;

		case T_WIRE: {
			highlight_wire(r, *ptr.wire, wire_col * col, shrink);
		} break;
	}
}

AABB ThingPtr::get_aabb () const {
	switch (type) {
		case T_PART: return part->get_aabb(); break;
		case T_NODE: return node->get_aabb(); break;
		INVALID_DEFAULT;
	}
}
float2& ThingPtr::get_pos () {
	switch (type) {
		case T_PART:    return part->pos.pos; break;
		case T_NODE:    return node->pos;     break;
		INVALID_DEFAULT;
	}
}

void Editor::ViewMode::find_hover (float2 cursor_pos, Chip& chip,
		float2x3 chip2world, float2x3 world2chip, int sid) {
	
	for_each_part(chip, [&] (Part* part) {
		auto part2world = chip2world * part->pos.calc_matrix();
		auto world2part = part->pos.calc_inv_matrix() * world2chip;
		
		if (hitbox(cursor_pos, part->chip->size, world2part)) {
			hover_part = { part, sid, part2world };

			if (!is_gate(part->chip)) {
				find_hover(cursor_pos, *part->chip, part2world, world2part, sid);
			}
		}

		sid += part->chip->state_count;
	});
}

void find_edit_hover (float2 cursor_pos, Chip& chip, bool allow_parts, bool allow_wires, ThingPtr& hover) {
	float pin_size = wire_radius*wire_node_radius_fac*2;

	for_each_part(chip, [&] (Part* part) {
		auto part2world = part->pos.calc_matrix();
		auto world2part = part->pos.calc_inv_matrix();
		
		if (allow_parts && hitbox(cursor_pos, part->chip->size, world2part))
			hover = part;

		if (part->chip != &gates[INP_PIN]) {
			for (int i=0; i<(int)part->chip->inputs.size(); ++i) {
				if (hitbox(cursor_pos, pin_size, get_inp_pos_invmat(*part->chip->inputs[i]) * world2part)) {
					hover = { part->pins[i].node.get() };
				}
			}
		}
		if (part->chip != &gates[OUT_PIN]) {
			for (int i=0; i<(int)part->chip->outputs.size(); ++i) {
				if (hitbox(cursor_pos, pin_size, get_out_pos_invmat(*part->chip->outputs[i]) * world2part)) {
					hover = { part->pins[(int)part->chip->inputs.size() + i].node.get() };
				}
			}
		}
	});

	if (allow_wires) {
		for (auto& wire : chip.wire_edges) {
			if (wire_hitbox(cursor_pos, *wire)) {
				hover = wire.get();
			}
		}
	}
	
	for (auto& node : chip.wire_nodes) {
		if (hitbox(cursor_pos, pin_size, translate(-node->pos))) {
			hover = node.get();
		}
	}
}
void find_boxsel (Chip& chip, bool remove, AABB box, Editor::PartSelection& sel) {
	
	auto for_item = [&] (ThingPtr ptr) {
		if (box.is_inside(ptr.get_pos())) {
			if (remove)
				sel.items.try_remove(ptr);
			else
				sel.items.try_add(ptr);
		}
	};

	for_each_part(chip, for_item);

	for (auto& node : chip.wire_nodes) {
		for_item(node.get());
	}
}

void Editor::update (Input& I, LogicSim& sim, ogl::Renderer& r) {
	
	if (I.buttons['E'].went_down) {
		if (in_mode<EditMode>()) mode = ViewMode();
		else                     mode = EditMode();
	}
	if (I.buttons['C'].went_down) {
		if (in_mode<WireMode>()) mode = EditMode();
		else                     mode = WireMode();
	}
	
	//if (I.buttons['K'].went_down)
	//	printf(""); // for debugging

	{ // Get cursor position
		float3 cur_pos = 0;
		_cursor_valid = !ImGui::GetIO().WantCaptureMouse && r.view.cursor_ray(I, &cur_pos);
		_cursor_pos = (float2)cur_pos;
	}

	if (in_mode<ViewMode>()) {
		auto& v = std::get<ViewMode>(mode);

		v.hover_part = {};
		v.find_hover(_cursor_pos, *sim.viewed_chip, float2x3::identity(), float2x3::identity(), 0);

		if (v.hover_part.part) {
			highlight_part(r, *v.hover_part.part, v.hover_part.part2world, hover_col);
		}
	}
	else if (in_mode<PlaceMode>()) {
		auto& pl = std::get<PlaceMode>(mode);
		auto& pos  = pl.place_pos;
		
		edit_placement(sim, I, pos);
		
		if (_cursor_valid) {
			
			// move to-be-placed gate preview with cursor
			pos.pos = snap(_cursor_pos);
			
			highlight(r, pl.place_chip->size, pos.calc_matrix(), preview_box_col);
			
			// place gate on left click
			if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
				sim.add_part(*sim.viewed_chip, pl.place_chip, pos); // preview becomes real gate, TODO: add a way to add parts to chips other than the viewed chip (keep selected chip during part placement?)
				// preview of gate still exists with same pos
			}
		}
	
		// exit edit mode with RMB
		if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
			mode = EditMode();
		}
	}
	else if (in_mode<EditMode>()) {
		auto& e = std::get<EditMode>(mode);
		
	//// Find hovered item
		ThingPtr hover = {};
		find_edit_hover(_cursor_pos, *sim.viewed_chip, true, false, hover);

	//// Item selection logic + box selection start
		
		// LMB selection logic
		if (!_cursor_valid) {
			e.dragging = false;
			e.box_selecting = false;
		}
		
		bool ctrl  = I.buttons[KEY_LEFT_CONTROL].is_down;
		bool shift = I.buttons[KEY_LEFT_SHIFT].is_down;
		auto& lmb  = I.buttons[MOUSE_BUTTON_LEFT];
		
		if (lmb.went_down) {
			if (hover.type == T_PART || (hover.type == T_NODE && !hover.node->parent_part)) {
				if (shift && e.sel) {
					// shift click toggles selection
					e.sel.items.toggle(hover);
				}
				else if (ctrl && e.sel) {
					// ignore
				}
				else {
					if (e.sel.items.contains(hover)) {
						// keep multiselect if clicked on already selected item
					}
					// non-shift click on non-selected part, replace selection
					else {
						e.sel = {{ hover }};
					}
				}
			}
			else {
				// replace selection if boxselect without shift or ctrl
				if (!shift && !ctrl) {
					e.sel = {};
				}
				
				// begin boxselect
				e.box_selecting = true;
				e.box_sel_start = _cursor_pos;
			}
		}
		
	//// Find box selection
		PartSelection boxsel = {};

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
				boxsel = {};
		
			float2 lo = min(e.box_sel_start, _cursor_pos);
			float2 hi = max(e.box_sel_start, _cursor_pos);
			float2 size = hi - lo;
			
			find_boxsel(*sim.viewed_chip, remove, AABB{lo,hi}, boxsel);
			
			r.dbgdraw.wire_quad(float3(lo, 0), size, boxsel_box_col);
		
			if (lmb.went_up) {
				e.sel = boxsel;
		
				e.box_selecting = false;
			}
		}

	//// Move selected parts and wire nodes
		if (e.sel) { // still in edit mode? else e becomes invalid
		
			float2 snapped_cursor = snap(_cursor_pos);
			
			float2 bounds_center;
			{
				AABB bounds = AABB::inf();
		
				for (auto& item : e.sel.items) {
					bounds.add(item.get_aabb());
				}
				
				bounds_center = (bounds.lo + bounds.hi) * 0.5f;
				// need to snap this right here (before item.ptr.get_pos() - bounds_center)
				// so that all computations are in snapped space, or when dragging and or rotation selections they can end up out of alignment
				bounds_center = snap(bounds_center);
			}
			
			// Rotate and mirror selection via keys
			// TODO: rotate around mouse cursor when dragging?
			// TODO: add some way of scaling? Scale as box or would you more commonly want to scale indivdual items?
			{
				float2 edit_center = e.dragging ? snapped_cursor : bounds_center;

				if (e.dragging) {
					// do confusing computation to correctly rotate/mirror around mouse cursor while dragging
					float2 orig_snapped_cursor = bounds_center + e.drag_offset;
					edit_placement(sim, I, bounds_center, snapped_cursor); // rotate/mirror bounds center around snapped_cursor
					e.drag_offset = orig_snapped_cursor - bounds_center;
				}

				for (auto& i : e.sel.items) {
					switch (i.type) {
						case T_PART: {
							edit_placement(sim, I, i.part->pos, edit_center);
							i.part->update_pins_pos();
						} break;
						case T_NODE: {
							edit_placement(sim, I, i.node->pos, edit_center);
						} break;
						INVALID_DEFAULT;
					}
				}
			}
			
			std::vector<float2> bounds_offsets;
			{
				bounds_offsets.resize(e.sel.items.size());
				for (int i=0; i<e.sel.items.size(); ++i) {
					bounds_offsets[i] = e.sel.items[i].get_pos() - bounds_center;
				}
			}

			// Drag with lmb 
			if (shift || ctrl) {
				e.dragging = false;
			}
			else {
				
				// Cursor is snapped, which is then used to move items as a group, thus unaligned items stay unaligned
				// snapping items individually is also possible
		
				if (!e.dragging && !shift) {
						// begin dragging gate
					if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
						e.drag_start  = snapped_cursor;
						e.drag_offset = snapped_cursor - bounds_center;
						e.dragging = true;
					}
				}
				if (e.dragging) {
					// move selection to cursor
					bounds_center = snapped_cursor - e.drag_offset;
					
					// drag gate
					if (I.buttons[MOUSE_BUTTON_LEFT].is_down) {
						if (length_sqr(snapped_cursor - e.drag_start) > 0) {
							sim.unsaved_changes = true;
						}
						
						for (int i=0; i<e.sel.items.size(); ++i) {
							e.sel.items[i].get_pos() = bounds_offsets[i] + bounds_center;

							if (e.sel.items[i].type == T_PART)
								e.sel.items[i].part->update_pins_pos();
						}

						if (e.sel.items.size() == 1) {
							auto& pos = e.sel.items[0].get_pos();
							pos = snap(pos);

							if (e.sel.items[0].type == T_PART)
								e.sel.items[0].part->update_pins_pos();
						}
					}
					// stop dragging gate
					else {
						e.dragging = false;
					}
				}
			}
		}

	//// Draw highlights after dragging / rotating / mirroring to avoid 1-frame jumps
		// Selection highlight
		{
			PartSelection& sel = e.box_selecting ? boxsel : e.sel;

			if (sel) {
				std::string buf;

				// only show text for single-part selections as to not spam too much text
				if (sel.items.size() == 1) {
					auto& item = sel.items[0];
					highlight(r, item, sel_col,
						item != hover, // only show text if not already hovered to avoid duplicate text at same spot
						SEL_HIGHL_SHRINK);
				}
				else {
					for (auto& item : sel.items) {
						highlight(r, item, sel_col, false, SEL_HIGHL_SHRINK);
					}
				}
			}
		}

		// Hover highlight
		if (hover) {
			if (hover.type == T_PART)
				highlight_chip_names(r, *hover.part->chip, hover.part->pos.calc_matrix());
			highlight(r, hover, hover_col, true);
		}

	//// Delete items after highlighting to avoid stale pointer access in hover/e.sel
		if (e.sel) {
			
			// remove gates via DELETE key
			if (I.buttons[KEY_DELETE].went_down) {

				for (auto& i : e.sel.items) {
					switch (i.type) {
						case T_PART: sim.remove_part(*sim.viewed_chip, i.part); break;
						case T_NODE: sim.remove_wire_node(*sim.viewed_chip, i.node); break;
						INVALID_DEFAULT;
					}
				}

				e.sel = {};
			}
			// Duplicate selected part with CTRL+C
			// TODO: CTRL+D moves the camera to the right, change that?
			//else if (I.buttons[KEY_LEFT_CONTROL].is_down && I.buttons['C'].went_down) {
			//	preview_part.chip = sel.part->chip;
			//	mode = PLACE_MODE;
			//}
		}
	}
	else if (in_mode<WireMode>()) {
		auto& w = std::get<WireMode>(mode);
		
		// Find hover
		ThingPtr hover = {};
		find_edit_hover(_cursor_pos, *sim.viewed_chip, false, true, hover);
		assert(hover.type == T_NONE || hover.type == T_NODE || hover.type == T_WIRE);

		float2 snapped_pos = snap(_cursor_pos);
		
		w.node = { snapped_pos };
		w.cur = &w.node;

		if (hover) {
			if (hover.type == T_NODE)
				w.cur = hover.node;

			highlight(r, hover, hover_col, true);

			if (hover.type == T_WIRE && I.buttons['X'].went_down)
				sim.remove_wire_edge(*sim.viewed_chip, hover.wire);
		}

		if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
			if (w.cur == &w.node) {
				// copy out the node struct into a real heap-alloced node
				w.cur = sim.add_wire_node(*sim.viewed_chip, w.node.pos);
			}

			if (hover.type == T_WIRE) {
				// insert node on existig wire by splitting wire

				auto* a = hover.wire->a;
				auto* b = hover.wire->b;

				sim.remove_wire_edge(*sim.viewed_chip, hover.wire);

				sim.connect_wire_nodes(*sim.viewed_chip, a, w.cur);
				sim.connect_wire_nodes(*sim.viewed_chip, w.cur, b);
			}

			if (w.prev && w.cur && w.prev != w.cur) {
				sim.connect_wire_nodes(*sim.viewed_chip, w.prev, w.cur);
			}

			//
			w.prev = w.cur;
		}

		if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
			// 1. RMB click with a line currently being drawn -> go back to adding first node
			if (w.prev) {
				w.prev = {};
			}
			// 2. RMB click or still in first node -> exit wire mode
			else {
				mode = EditMode();
			}
		}
	}

	highlight_chip_names(r, *sim.viewed_chip, float2x3::identity());
}

void Editor::update_toggle_gate (Input& I, LogicSim& sim, Window& window) {
	
	bool can_toggle = false;

	if (in_mode<ViewMode>()) {
		auto& v = std::get<ViewMode>(mode);

		can_toggle = v.hover_part.part && is_gate(v.hover_part.part->chip);
		
		if (v.toggle_sid < 0 && can_toggle && I.buttons[MOUSE_BUTTON_LEFT].went_down) {
			v.toggle_sid = v.hover_part.sid;
			v.state_toggle_value = !sim.state[sim.cur_state][v.toggle_sid];
		}
		if (v.toggle_sid >= 0) {
			sim.state[sim.cur_state][v.toggle_sid] = v.state_toggle_value;
	
			if (I.buttons[MOUSE_BUTTON_LEFT].went_up)
				v.toggle_sid = -1;
		}
	}
	
	static bool prev = false;
	if (prev != can_toggle || can_toggle)
		window.set_cursor(can_toggle ? Window::CURSOR_FINGER : Window::CURSOR_NORMAL);
	prev = can_toggle;
}
	
} // namespace logic_sim
