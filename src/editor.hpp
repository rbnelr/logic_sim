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

	// This variant stuff is dodgy as fuck
	// But writing this state-machine like code like this actually makes sense, it's just that c++ makes it hard
	// for example having the associated state of the editor be tied to named modes
	// prevents bugs where you don't properly reset state of other modes that I got when just throwing all variables into the editor struct
	// unfortunately the std::variant code is bloated and cumbersome, and I have no idea how to switch to a new mode from the update function of a mode
	// returning it by value either as newly constructed new mode or std::move(*this) seems to work, but is inefficient and potentially not actually safe
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
		struct HoverLoc {
			bool did_hover = false;
			int2 toggle_loc;
			Part* part = nullptr;
			float2x3 part2world;

			operator bool () const { return did_hover; }
		};

		void find_hover (LogicSim& sim, float2 const& cursor_pos, Chip& chip,
				float2x3 const& chip2world, float2x3 const& world2chip, HoverLoc& hover);

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

	bool can_toggle;
	bool did_toggle;

////
	void select_gate_imgui (LogicSim& sim, const char* name, Chip* type);
		
	void viewed_chip_imgui (LogicSim& sim, Camera2D& cam);
	void saved_chips_imgui (LogicSim& sim, Camera2D& cam);
		
	void selection_imgui (LogicSim& sim, PartSelection& sel);

	void imgui (LogicSim& sim, Camera2D& cam);
		
////
	void update (Input& I, LogicSim& sim, ogl::Renderer& r, Window& window);
};

} // namespace logic_sim
