#pragma once
#include "common.hpp"
#include "logic_sim.hpp"

namespace logic_sim {

struct Editor {
		
	struct PartSelection {
		vector_set<ThingPtr> items;
			
		operator bool () {
			return !items.empty();
		}
			
		void add (PartSelection& r) {
			for (auto& it : r.items) {
				items.try_add(it);
			}
		}
		void remove (PartSelection& r) {
			for (auto& it : r.items) {
				items.try_remove(it);
			}
		}
	};

	struct ViewMode;
	struct PlaceMode;
	struct EditMode;
	struct WireMode;

	static constexpr const char* mode_names[] = {
		"View",
		"Place",
		"Edit",
		"Wire"
	};
	typedef std::variant<
		ViewMode,
		PlaceMode,
		EditMode,
		WireMode
	> ModeVariant;
	
	struct ViewMode {
		struct Hover_Part {
			Part* part = nullptr;
			float2x3 part2world;
		};
		Hover_Part hover_part = {};

		int toggle_state_id = -1;
		bool state_toggle_value; // new state value while toggle is 'held'

		void find_hover (float2 const& cursor_pos, Chip& chip,
				float2x3 const& chip2world, float2x3 const& world2chip, int sid);

		ModeVariant update (Editor& ed, Input& I, LogicSim& sim, ogl::Renderer& r);
	};
	struct PlaceMode {
		Chip*     place_chip = nullptr;
		Placement place_pos = {};

		ModeVariant update (Editor& ed, Input& I, LogicSim& sim, ogl::Renderer& r);
	};
	struct EditMode {

		PartSelection sel = {};

		bool dragging = false; // dragging selection
		float2 drag_start;
		float2 drag_offset;

		bool box_selecting = false;
		float2 box_sel_start;

		ModeVariant update (Editor& ed, Input& I, LogicSim& sim, ogl::Renderer& r);
	};
	struct WireMode {
		WireNode* prev = nullptr;
		WireNode* cur;

		// buf for new node, cur points to this if new node will be created
		WireNode  node;

		ModeVariant update (Editor& ed, Input& I, LogicSim& sim, ogl::Renderer& r);
	};

	ModeVariant mode = ViewMode();

	template <typename T> bool in_mode () {
		return std::holds_alternative<T>(mode);
	}

	void reset () {
		mode = ViewMode();
	}
	
	float2 snap (float2 pos) {
		return round(pos);
	}

	int chips_reorder_src = -1;
		
	bool _cursor_valid;
	float2 _cursor_pos;

////
	void select_gate_imgui (LogicSim& sim, const char* name, Chip* type);
		
	void viewed_chip_imgui (LogicSim& sim, Camera2D& cam);
	void saved_chips_imgui (LogicSim& sim, Camera2D& cam);
		
	void selection_imgui (LogicSim& sim, PartSelection& sel);

	void imgui (LogicSim& sim, Camera2D& cam);
		
////
	void update (Input& I, LogicSim& sim, ogl::Renderer& r);
		
	bool update_toggle_gate (Input& I, LogicSim& sim, Window& window);
};

} // namespace logic_sim
