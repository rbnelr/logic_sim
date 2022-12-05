#pragma once
#include "common.hpp"
#include "camera.hpp"

// smart ptr array that can be inserted into and erased from like a vector
// preferred because it's footprint is 1 ptr rather than 3
// insertions happen rarely (only on edits of chip instances) but reads happen very often (every sim tick)
template<typename T>
struct schmart_array {
	std::unique_ptr<T[]> ptr;

	schmart_array (): ptr{nullptr} {}
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

namespace logic_sim {
	
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
	
////
	struct Part;
	
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
	};

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
		schmart_array<InputWire> inputs;
		
		Part (Chip* chip, Placement pos={}): chip{chip}, pos{pos}, inputs{chip ? (int)chip->inputs.size() : 0} {}
	};


////
	inline constexpr float2 PIN_SIZE = 0.25f;

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

	struct _IO {
		const char* name;
		float2 pos;
	};
	inline Chip _GATE (const char* name, lrgb col, float2 size, std::initializer_list<_IO> inputs, std::initializer_list<_IO> outputs) {
		Chip c;
		c.name = name;
		c.col = col;
		c.size = size;
		c.state_count = 1;

		for (auto& o : outputs) {
			c.outputs.emplace_back(o.name);
			c.parts.emplace_back(nullptr, Placement{o.pos});
		}
		for (auto& i : inputs) {
			c.inputs.emplace_back(i.name);
			c.parts.emplace_back(nullptr, Placement{i.pos});
		}
		return c;
	}
	
	// Not const because we use Chip* for both editable (user-defined) chips and primitve gates
	// alternatively just cast const away?
	inline Chip gates[GATE_COUNT] = {
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
	
	inline bool is_gate (Chip* chip) {
		return chip >= gates && chip < &gates[GATE_COUNT];
	}
	inline GateType gate_type (Chip* chip) {
		assert(is_gate(chip));
		return (GateType)(chip - gates);
	}
	
////
	inline int update_state_indices (Chip& chip) {
		// state count cached, early out
		if (chip.state_count >= 0)
			return chip.state_count;
			
		// state count stale, recompute
		chip.state_count = 0;
		for (auto& part : chip.parts) {
			// states are placed flattened in order of depth first traversal of part (chip instance) tree
			part.state_idx = chip.state_count;
			// allocate as many states as are needed recursively for this part
			chip.state_count += update_state_indices(*part.chip);
		}

		return chip.state_count;
	}

	inline int indexof_part (std::vector<Part> const& vec, Part* part) {
		//return indexof(parts, part, [] (Part const& l, Part* r) { return &l == r; });
		size_t idx = part - vec.data();
		assert(idx >= 0 && idx < vec.size());
		return (int)idx;
	}
	inline int indexof_chip (std::vector<std::shared_ptr<Chip>> const& vec, Chip* chip) {
		return indexof(vec, chip, [] (std::shared_ptr<Chip> const& l, Chip* r) { return l.get() == r; });
	}
	
////
	struct LogicSim {
		
		// (de)serialize a chip to json, translating between gate and custom chip pointers and a single integer id
		friend void to_json (json& j, const Chip& chip, const LogicSim& sim);
		friend void from_json (const json& j, Chip& chip, LogicSim& sim);

		// (de)serialize all saved chips (if the viewed_chip is not saved as a new chip it will be deleted, TODO: add warning?)
		// simulation state is never (de)serialized
		// editor state is never (de)serialized
		friend void to_json (json& j, const LogicSim& sim);
		friend void from_json (const json& j, LogicSim& sim);


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
				state[i].assign(viewed_chip->state_count, 0);
				state[i].shrink_to_fit();
			}
			cur_state = 0;
		}
		void reset_chip_view () {
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
				update_state_indices(*c);
			update_state_indices(*viewed_chip);
		}

		void imgui (Input& I) {
			ImGui::Text("Gates (# of states): %d", (int)state[0].size());
		}
		
		void simulate (Input& I);
	};
	
	struct Editor {

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
		
		struct PartPreview {
			Chip* chip = nullptr;
			Placement pos = {};
		};
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

		enum Mode {
			VIEW_MODE=0,
			EDIT_MODE,
			PLACE_MODE,
		};
		
	////
		Mode mode = VIEW_MODE;

		float snapping_size = 0.125f;
		bool snapping = true;
	
		float2 snap (float2 pos) {
			return snapping ? round(pos / snapping_size) * snapping_size : pos;
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
		
		PartPreview preview_part = {};
		WirePreview preview_wire = {};

		void reset () {
			mode = EDIT_MODE;

			hover = {};
			sel = {};

			dragging = false;

			preview_wire = {};
			preview_part.chip = nullptr;
		}
		
	////
		void select_preview_gate_imgui (LogicSim& sim, const char* name, Chip* type);
		void select_preview_chip_imgui (LogicSim& sim, const char* name, std::shared_ptr<Chip>& chip, bool can_place, bool is_viewed);

		void saved_chips_imgui (LogicSim& sim);
		void viewed_chip_imgui (LogicSim& sim);
		void selection_imgui (Selection& sel);

		void parts_hierarchy_imgui (LogicSim& sim, Chip& chip);

		void imgui (LogicSim& sim);
		
	////
		void add_part (LogicSim& sim, Chip& chip, PartPreview& part);
		void remove_part (LogicSim& sim, Selection& sel);
		void add_wire (WirePreview& wire);

		void edit_chip (Input& I, LogicSim& sim, Chip& chip,
				float2x3 const& world2chip, float2x3 const& chip2world, int state_base);

		void update (Input& I, View3D& view, Window& window, LogicSim& sim);
	};
	
}
