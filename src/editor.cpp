#include "common.hpp"
#include "editor.hpp"
#include "app.hpp"
#include "opengl/renderer.hpp"

namespace logic_sim {
	
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

	int2 size = roundi(sim.viewed_chip->size);
	ImGui::DragInt2("size", &size.x, 0.1f);
	sim.viewed_chip->size = (float2)size;

	// TODO:
	//if (ImGui::TreeNodeEx("Inputs")) {
	//	for (int i=0; i<(int)sim.viewed_chip->inputs.size(); ++i) {
	//		ImGui::PushID(i);
	//		ImGui::SetNextItemWidth(ImGui::CalcItemWidth() * 0.5f);
	//		ImGui::InputText(prints("Input Pin #%d###name", i).c_str(), &sim.viewed_chip->inputs[i]->name);
	//		ImGui::PopID();
	//	}
	//	ImGui::TreePop();
	//}

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
	
	int idx = sim.indexof_chip(sim.viewed_chip.get());
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

void Editor::selection_imgui (LogicSim& sim, PartSelection& sel) {
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

		sim.recompute |= ImGui::InputText("name",          &part.name);

		ImGui::Text("Placement in parent chip:");

		int rot = (int)part.pos.rot;
		int2 pos = roundi(part.pos.pos);
		sim.recompute |= ImGui::DragInt2("pos",          &pos.x, 0.1f);
		sim.recompute |= ImGui::SliderInt("rot [R]",       &rot, 0, 3);
		sim.recompute |= ImGui::Checkbox("mirror (X) [M]", &part.pos.mirror);
		part.pos.pos = (float2)pos;
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
		
		if (imgui_Header("Selection", true)) {
			ImGui::Indent(10);
			
			// Child region so that window contents don't shift around when selection changes
			if (ImGui::BeginChild("SelectionRegion", ImVec2(0, TEXT_BASE_HEIGHT * 9))) {
				if (!in_mode<EditMode>()) {
					ImGui::Text("Not in Edit Mode");
				}
				else {
					auto& e = std::get<EditMode>(mode);
					
					selection_imgui(sim, e.sel);
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
				if (ImGui::BeginTable("TableGates", 6, ImGuiTableFlags_Borders)) {
					
					struct Entry {
						const char* name = nullptr;
						Chip*       gate;
					};
					constexpr Entry entries[] = {
						//{ "INP"    , &gate_chips[INP_PIN   ] },
						//{},
						//{ "OUT"    , &gate_chips[OUT_PIN   ] },
						//{},
						//     
						{ "BUF"    , &gate_chips[BUF_GATE  ] },
						{},
						{},
						{ "NOT"    , &gate_chips[NOT_GATE  ] },
						{},
						{},

						{ "AND"    , &gate_chips[AND_GATE  ] },
						{ "AND-3"  , &gate_chips[AND3_GATE ] },
						{ "AND-4"  , &gate_chips[AND4_GATE ] },
						{ "NAND"   , &gate_chips[NAND_GATE ] },
						{ "NAND-3" , &gate_chips[NAND3_GATE] },
						{ "NAND-4" , &gate_chips[NAND4_GATE] },

						{ "OR"     , &gate_chips[OR_GATE   ] },
						{ "OR-3"   , &gate_chips[OR3_GATE  ] },
						{ "OR-4"   , &gate_chips[OR4_GATE  ] },
						{ "NOR"    , &gate_chips[NOR_GATE  ] },
						{ "NOR-3"  , &gate_chips[NOR3_GATE ] },
						{ "NOR-4"  , &gate_chips[NOR4_GATE ] },
					
						{ "XOR"    , &gate_chips[XOR_GATE  ] },
						{},
						{},
						{},
						{},
						{},

						{ "Mux", &gate_chips[MUX_GATE] },
						{ "Mux-4", &gate_chips[MUX4_GATE] },
						{ "Dmux", &gate_chips[DMUX_GATE] },
						{ "Dmux-4", &gate_chips[DMUX4_GATE] },
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
		sim.recompute = true;
	}
	if (I.buttons['M'].went_down) {
		p.mirror_around(center);
		sim.unsaved_changes = true;
		sim.recompute = true;
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
	//if (part.chip == &gates[OUT_PIN] || part.chip == &gates[INP_PIN])
	//	return part.name;
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
	for (auto& part : chip.parts) {
		if (!part->name.empty())
			r.draw_text(part->name, chip2world * part->pos.pos, part_text_sz, named_part_col, 0.5f, 3.2f);
	}
}

void highlight_toggle (ogl::Renderer& r, int2 const& loc, Chip::ToggleLoc const& value) {
	auto size = wire_node_junction * 1.0f;
	auto mat = translate((float2)loc);
	r.draw_highlight_box(size, mat, toggle_col);
	
	float2 center = mat * float2(0) + size*0.5f;
	r.draw_text(prints("%d", value), center, pin_text_sz, toggle_col, float2(1,0));
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
void highlight_wire_node (ogl::Renderer& r, float2 const& pos, lrgba col, bool show_text, float shrink=0.0f) {
	float pin_size = wire_radius*wire_node_junction*2;

	std::string_view name;
	if (show_text) {
		// TODO: pin name?
		name = "<wire_node>";
	}

	highlight(r, pin_size - shrink*2.0f, translate(pos), col * pin_col, name);
}

void highlight (ogl::Renderer& r, ThingPtr ptr, lrgba col, bool show_text, float shrink=0.0f) {
	switch (ptr.type) {
		case T_PART: {
			auto part2world = ptr.part->pos.calc_matrix();
			
			highlight_part(r, *ptr.part, part2world, col, show_text, shrink);
		} break;
		case T_NODE: {
			highlight_wire_node(r, ptr.node->pos, col * pin_col, show_text, shrink);
		} break;

		case T_WIRE: {
			highlight_wire(r, *ptr.wire, wire_col * col, shrink);
		} break;
	}
}

void Editor::ViewMode::find_hover (LogicSim& sim, float2 const& cursor_pos, Chip& chip,
		float2x3 const& chip2world, float2x3 const& world2chip, Editor::ViewMode::HoverLoc& hover) {

	for (auto& part : chip.parts) {
		auto part2world = chip2world * part->pos.calc_matrix();
		auto world2part = part->pos.calc_inv_matrix() * world2chip;
		
		if (hitbox(cursor_pos, part->chip->size, world2part)) {
			if (!is_gate(part->chip)) {
				find_hover(sim, cursor_pos, *part->chip, part2world, world2part, hover);
			}
			else {
				auto loc = sim.circuit.get_gate_output_loc(part->chip, part2world);
				hover = { true, loc, part.get(), part2world };
			}
		}
		
		float pin_size = wire_radius*wire_node_junction*2;
		for (auto& node : chip.wire_nodes) {
			if (hitbox(cursor_pos, pin_size, translate(-node->pos))) {
				hover = { true, roundi(node->pos) };
			}
		}
	}
}

void find_edit_hover (float2 const& cursor_pos, Chip& chip, bool allow_parts, bool allow_wires, ThingPtr& hover) {
	float pin_size = wire_radius*wire_node_junction*2;

	for (auto& part : chip.parts) {
		auto part2world = part->pos.calc_matrix();
		auto world2part = part->pos.calc_inv_matrix();
		
		if (allow_parts && hitbox(cursor_pos, part->chip->size, world2part))
			hover = part.get();
	}

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
	
	for (auto& part : chip.parts) {
		for_item(part.get());
	}

	for (auto& node : chip.wire_nodes) {
		for_item(node.get());
	}
}

void Editor::update (Input& I, LogicSim& sim, ogl::Renderer& r, Window& window) {
	
	if (I.buttons['E'].went_down) {
		if (in_mode<EditMode>()) mode = ViewMode();
		else                     mode = EditMode();
	}
	if (I.buttons['C'].went_down) {
		if (in_mode<WireMode>()) mode = EditMode();
		else                     mode = WireMode();
	}
	
	{ // Get cursor position
		float3 cur_pos = 0;
		_cursor_valid = !ImGui::GetIO().WantCaptureMouse && r.view.cursor_ray(I, &cur_pos);
		_cursor_pos = (float2)cur_pos;
	}

	can_toggle = false;
	did_toggle = false;

	mode = std::move(std::visit([&] (auto&& arg) { return arg.update(*this, I, sim, r); }, mode));
	

	highlight_chip_names(r, *sim.viewed_chip, float2x3::identity());

	if (sim.unsaved_changes) {
		sim.recompute_chip_users();
	}
	
////
	for (auto& kv : sim.viewed_chip->toggle_locs) {
		highlight_toggle(r, kv.first, kv.second);
	}

	// I don't remember why I changed to logic to only set the cursor when it needs to change
	// re add this code if problems occur, but get rid of the static please
	
	//static bool prev = false;
	//if (prev != can_toggle || can_toggle)
		window.set_cursor(can_toggle ? Window::CURSOR_FINGER : Window::CURSOR_NORMAL);
	//prev = can_toggle;
}

Editor::ModeVariant Editor::ViewMode::update (Editor& ed, Input& I, LogicSim& sim, ogl::Renderer& r) {
	HoverLoc hover = {};
	find_hover(sim, ed._cursor_pos, *sim.viewed_chip, float2x3::identity(), float2x3::identity(), hover);
	
	for (auto& kv : sim.viewed_chip->toggle_locs) {
		if (hitbox(ed._cursor_pos, wire_node_junction, translate(-(float2)kv.first) ))
			hover = { true, kv.first };
	}

	if (hover) {
		if (hover.part)
			highlight_part(r, *hover.part, hover.part2world, hover_col);
		else
			highlight_wire_node(r, (float2)hover.toggle_loc, hover_col, false);

		if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
			auto it = sim.viewed_chip->toggle_locs.find(hover.toggle_loc);
			if (it == sim.viewed_chip->toggle_locs.end()) {
				// create new toggle
				int state_id = sim.circuit.node_map[hover.toggle_loc].state_id;
				assert(state_id >= 0);
				bool cur_state = sim.circuit.states[sim.circuit.cur_state].state[state_id];

				sim.viewed_chip->toggle_locs.emplace(hover.toggle_loc, !cur_state);
			}
			else {
				it->second.force_state = !it->second.force_state;
			}

			ed.did_toggle = true;
		}
		else if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
			auto it = sim.viewed_chip->toggle_locs.find(hover.toggle_loc);
			if (it != sim.viewed_chip->toggle_locs.end()) {
				sim.viewed_chip->toggle_locs.erase(hover.toggle_loc); // try to erase
				ed.did_toggle = true;
			}
		}
	}

	ed.can_toggle = hover;

	return std::move(*this);
}
Editor::ModeVariant Editor::PlaceMode::update (Editor& ed, Input& I, LogicSim& sim, ogl::Renderer& r) {
	edit_placement(sim, I, place_pos);
		
	if (ed._cursor_valid) {
			
		// move to-be-placed gate preview with cursor
		place_pos.pos = ed.snap(ed._cursor_pos);
			
		highlight(r, place_chip->size, place_pos.calc_matrix(), preview_box_col);
			
		// place gate on left click
		if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
			sim.add_part(*sim.viewed_chip, place_chip, place_pos); // preview becomes real gate, TODO: add a way to add parts to chips other than the viewed chip (keep selected chip during part placement?)
			// preview of gate still exists with same pos
		}
	}
	
	// exit edit mode with RMB
	if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
		return EditMode();
	}

	return std::move(*this);
}
Editor::ModeVariant Editor::EditMode::update (Editor& ed, Input& I, LogicSim& sim, ogl::Renderer& r) {
	
//// Find hovered item
	ThingPtr hover = {};
	find_edit_hover(ed._cursor_pos, *sim.viewed_chip, true, false, hover);

//// Item selection logic + box selection start
		
	// LMB selection logic
	if (!ed._cursor_valid) {
		dragging = false;
		box_selecting = false;
	}
		
	bool ctrl  = I.buttons[KEY_LEFT_CONTROL].is_down;
	bool shift = I.buttons[KEY_LEFT_SHIFT].is_down;
	auto& lmb  = I.buttons[MOUSE_BUTTON_LEFT];
		
	if (lmb.went_down) {
		if (hover.type == T_PART || hover.type == T_NODE) {
			if (shift && sel) {
				// shift click toggles selection
				sel.items.toggle(hover);
			}
			else if (ctrl && sel) {
				// ignore
			}
			else {
				if (sel.items.contains(hover)) {
					// keep multiselect if clicked on already selected item
				}
				// non-shift click on non-selected part, replace selection
				else {
					sel = {{ hover }};
				}
			}
		}
		else {
			// replace selection if boxselect without shift or ctrl
			if (!shift && !ctrl) {
				sel = {};
			}
				
			// begin boxselect
			box_selecting = true;
			box_sel_start = ed._cursor_pos;
		}
	}
		
//// Find box selection
	PartSelection boxsel = {};

	if (box_selecting) {
		
		bool remove = false;
		
		// shift lmb: add to selection
		// ctrl  lmb: remove from selection
		if (shift || ctrl) {
			remove = ctrl;
		}
		// just lmb: replace selection
		else {
			sel = {};
		}
			
		boxsel = sel;
		if (!boxsel) 
			boxsel = {};
		
		float2 lo = min(box_sel_start, ed._cursor_pos);
		float2 hi = max(box_sel_start, ed._cursor_pos);
		float2 size = hi - lo;
			
		find_boxsel(*sim.viewed_chip, remove, AABB{lo,hi}, boxsel);
			
		r.dbgdraw.wire_quad(float3(lo, 0), size, boxsel_box_col);
		
		if (lmb.went_up) {
			sel = boxsel;
		
			box_selecting = false;
		}
	}

//// Move selected parts and wire nodes
	if (sel) { // still in edit mode? else e becomes invalid
		
		float2 snapped_cursor = ed.snap(ed._cursor_pos);
			
		float2 bounds_center;
		{
			AABB bounds = AABB::inf();
		
			for (auto& item : sel.items) {
				bounds.add(item.get_aabb());
			}
				
			bounds_center = (bounds.lo + bounds.hi) * 0.5f;
			// need to snap this right here (before item.ptr.get_pos() - bounds_center)
			// so that all computations are in snapped space, or when dragging and or rotation selections they can end up out of alignment
			bounds_center = ed.snap(bounds_center);
		}
			
		// Rotate and mirror selection via keys
		// TODO: rotate around mouse cursor when dragging?
		// TODO: add some way of scaling? Scale as box or would you more commonly want to scale indivdual items?
		{
			float2 edit_center = dragging ? snapped_cursor : bounds_center;

			if (dragging) {
				// do confusing computation to correctly rotate/mirror around mouse cursor while dragging
				float2 orig_snapped_cursor = bounds_center + drag_offset;
				edit_placement(sim, I, bounds_center, snapped_cursor); // rotate/mirror bounds center around snapped_cursor
				drag_offset = orig_snapped_cursor - bounds_center;
			}

			for (auto& i : sel.items) {
				switch (i.type) {
					case T_PART: {
						edit_placement(sim, I, i.part->pos, edit_center);
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
			bounds_offsets.resize(sel.items.size());
			for (int i=0; i<sel.items.size(); ++i) {
				bounds_offsets[i] = sel.items[i].get_pos() - bounds_center;
			}
		}

		// Drag with lmb 
		if (shift || ctrl) {
			dragging = false;
		}
		else {
				
			// Cursor is snapped, which is then used to move items as a group, thus unaligned items stay unaligned
			// snapping items individually is also possible
		
			if (!dragging && !shift) {
					// begin dragging gate
				if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
					drag_start  = snapped_cursor;
					drag_offset = snapped_cursor - bounds_center;
					dragging = true;
				}
			}
			if (dragging) {
				// move selection to cursor
				bounds_center = snapped_cursor - drag_offset;
					
				// drag gate
				if (I.buttons[MOUSE_BUTTON_LEFT].is_down) {
					if (length_sqr(snapped_cursor - drag_start) > 0) {
						sim.unsaved_changes = true;
					}
						
					for (int i=0; i<sel.items.size(); ++i) {
						sel.items[i].get_pos() = bounds_offsets[i] + bounds_center;
					}

					if (sel.items.size() == 1) {
						auto& pos = sel.items[0].get_pos();
						pos = ed.snap(pos);
					}
				}
				// stop dragging gate
				else {
					dragging = false;
				}
					
				sim.recompute = true;
			}
		}
	}

//// Draw highlights after dragging / rotating / mirroring to avoid 1-frame jumps
	// Selection highlight
	{
		PartSelection& _sel = box_selecting ? boxsel : sel;

		if (_sel) {
			std::string buf;

			// only show text for single-part selections as to not spam too much text
			if (_sel.items.size() == 1) {
				auto& item = _sel.items[0];
				highlight(r, item, sel_col,
					item != hover, // only show text if not already hovered to avoid duplicate text at same spot
					SEL_HIGHL_SHRINK);
			}
			else {
				for (auto& item : _sel.items) {
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

//// Delete items after highlighting to avoid stale pointer access in hover/sel
	if (sel) {
			
		// remove gates via DELETE key
		if (I.buttons[KEY_DELETE].went_down) {

			for (auto& i : sel.items) {
				switch (i.type) {
					case T_PART: sim.remove_part(*sim.viewed_chip, i.part); break;
					case T_NODE: sim.remove_wire_node(*sim.viewed_chip, i.node); break;
					INVALID_DEFAULT;
				}
			}

			sel = {};
		}
		// Duplicate selected part with CTRL+C
		// TODO: CTRL+D moves the camera to the right, change that?
		//else if (I.buttons[KEY_LEFT_CONTROL].is_down && I.buttons['C'].went_down) {
		//	preview_part.chip = sel.part->chip;
		//	mode = PLACE_MODE;
		//}
	}

	return std::move(*this);
}
Editor::ModeVariant Editor::WireMode::update (Editor& ed, Input& I, LogicSim& sim, ogl::Renderer& r) {
	// Find hover
	ThingPtr hover = {};
	find_edit_hover(ed._cursor_pos, *sim.viewed_chip, false, true, hover);
	assert(hover.type == T_NONE || hover.type == T_NODE || hover.type == T_WIRE);

	float2 snapped_pos = ed.snap(ed._cursor_pos);
		
	node = { snapped_pos };
	cur = &node;

	if (hover) {
		if (hover.type == T_NODE)
			cur = hover.node;

		highlight(r, hover, hover_col, true);

		if (hover.type == T_WIRE && I.buttons['X'].went_down)
			sim.remove_wire_edge(*sim.viewed_chip, hover.wire);
	}

	if (I.buttons[MOUSE_BUTTON_LEFT].went_down) {
		if (cur == &node) {
			// copy out the node struct into a real heap-alloced node
			cur = sim.add_wire_node(*sim.viewed_chip, node.pos);
		}

		if (hover.type == T_WIRE) {
			// insert node on existig wire by splitting wire

			auto* a = hover.wire->a;
			auto* b = hover.wire->b;

			sim.remove_wire_edge(*sim.viewed_chip, hover.wire);

			sim.connect_wire_nodes(*sim.viewed_chip, a, cur);
			sim.connect_wire_nodes(*sim.viewed_chip, cur, b);
		}

		if (prev && cur && prev != cur) {
			sim.connect_wire_nodes(*sim.viewed_chip, prev, cur);
		}

		//
		prev = cur;
	}

	if (I.buttons[MOUSE_BUTTON_RIGHT].went_down) {
		// 1. RMB click with a line currently being drawn -> go back to adding first node
		if (prev) {
			prev = {};
		}
		// 2. RMB click or still in first node -> exit wire mode
		else {
			return EditMode();
		}
	}

	return std::move(*this);
}

} // namespace logic_sim
