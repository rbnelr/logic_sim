#pragma once
#include "common.hpp"
#include "camera.hpp"
#include <variant>

template<typename T>
void insert (std::unique_ptr<T[]>& old, int old_size, int idx, T&& val) {
	assert(idx >= 0 && idx <= old_size);

	auto ptr = std::make_unique<T[]>(old_size + 1);

	for (int i=0; i<idx; ++i)
		ptr[i] = std::move(old[i]);

	ptr[idx] = std::move(val);

	for (int i=idx; i<old_size; ++i)
		ptr[i+1] = std::move(old[i]);

	return ptr;
}
template<typename T>
void erase (std::unique_ptr<T[]>& old, int old_size, int idx) {
	assert(old_size > 0 && idx < old_size);
		
	auto ptr = std::make_unique<T[]>(old_size - 1);

	for (int i=0; i<idx; ++i)
		ptr[i] = std::move(old[i]);

	for (int i=idx+1; i<old_size; ++i)
		ptr[i-1] = std::move(old[i]);

	return ptr;
}

template <typename T>
struct _DefaultKeySel {
	T const& get_key (T const& t) const {
		return t;
	}
};

#include <unordered_set>

/*
template<typename T, typename KSel=_DefaultKeySel<T>>
struct hashset {

	typedef key_t = decltype(KSel::get_key(t));

	struct _Hash {
		static size_t operator() (T const& t) {
			return std::hash<key_t>()
				(KSel::get_key(t));
		}
	};
	struct _KeyEqual {
		static bool operator() (T const& l, T const& r) {
			return KSel::get_key(l) == KSel::get_key(r);
		}
	};

	typedef set_t std::unordered_set<T, _Hash, _KeyEqual>;

	set_t set;

	hashset () {}

	hashset (hashset&& r): set{ std::move(r) };
	hashset& operator= (hashset&& r) {
		this->set = std::move(r);
		return *this
	};

	hashset (hashset const& r): set{ r };
	hashset& operator= (hashset const& r) {
		this->set = r;
		return *this
	};

	bool empty () const {
		return set.empty();
	}

	set_t::iterator begin () {
		return set.begin();
	}
	set_t::iterator end () {
		return set.end();
	}

	set_t::const_iterator begin () {
		return set.begin();
	}
	set_t::const_iterator end () {
		return set.end();
	}

	bool contains (key_t const& key) {
		return set.find(key) != set.end();
	}

	bool add (T&& val) {
		return set.emplace(val);
	}

	bool remove (key_t const& key) {
		return set.erase(key);
	}

	set_t::iterator find (key_t const& key) {
		return set.find(key);
	}
};*/

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
	
	constexpr float2x2 MIRROR[] = {
		float2x2(  1, 0,  0, 1 ),
		float2x2( -1, 0,  0, 1 ),
	};
	
	struct AABB {
		float2 lo; // min
		float2 hi; // max

		static AABB inf () {
			return { INF, -INF };
		}

		void add (AABB const& a) {
			lo.x = min(lo.x, a.lo.x);
			lo.y = min(lo.y, a.lo.y);

			hi.x = max(hi.x, a.hi.x);
			hi.y = max(hi.y, a.hi.y);
		}
	};

	struct Placement {
		SERIALIZE(Placement, pos, rot, mirror, scale)

		float2 pos = 0;
		short  rot = 0;
		bool   mirror = false;
		float  scale = 1.0f;

		float2x3 calc_matrix () {
			return translate(pos) * ::scale(float2(scale)) * ROT[rot] * MIRROR[mirror];
		}
		float2x3 calc_inv_matrix () {
			return MIRROR[mirror] * INV_ROT[rot] * ::scale(float2(1.0f/scale)) * translate(-pos);
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

		std::vector< std::unique_ptr<Part> > outputs = {};
		std::vector< std::unique_ptr<Part> > inputs = {};
		
		std::unordered_set< std::unique_ptr<Part> > parts = {};
		
		int _recurs = 0;
	};

	// An instance of a chip placed down in a chip
	// (Primitive gates are also implemented as chips)
	struct Part {
		Chip* chip = nullptr;

		// optional part name
		std::string name;

		Placement pos = {};

		// where the states of this parts outputs are stored for this chip
		// ie. any chip being placed itself is a part with a state_idx, it's subparts then each have a state_idx relative to it
		int state_idx = -1; // check parent chip for state state_count, then this is also stale

		struct InputWire {
			Part* part = nullptr;
			// which output pin of the part is connected to
			int pin_idx = 0;
			
			//int state_idx = 0;

			std::vector<float2> wire_points;
		};
		std::unique_ptr<InputWire[]> inputs;
		
		struct OutputWire {
			Part* part = nullptr;
			// which input pin of the part is connected to
			int pin_idx = 0;

			// no wire 
		};
		std::unique_ptr<OutputWire[]> outputs;

		Part (Chip* chip, std::string&& name="", Placement pos={}): chip{chip}, pos{pos}, name{name},
			inputs {std::make_unique<InputWire []>(chip ? chip->inputs .size() : 0)},
			outputs{std::make_unique<OutputWire[]>(chip ? chip->outputs.size() : 0)} {}
		
		AABB get_aabb () const {
			// mirror does not matter
			float2 size = chip->size * pos.scale;

			// Does not handle non-90 deg rotations
			size = abs(ROT[pos.rot] * size);

			return AABB{ pos.pos - size*0.5f, pos.pos + size*0.5f };
		}
	};


////
	inline constexpr float PIN_SIZE   = 0.25f; // IO Pin hitbox size
	inline constexpr float PIN_LENGTH = 0.5f; // IO Pin base wire length

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

		AND3_GATE  ,
		NAND3_GATE ,
		OR3_GATE   ,
		NOR3_GATE  ,

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
		
		for (auto& i : inputs) {
			c.inputs.emplace_back(std::make_unique<Part>( nullptr, i.name, Placement{ i.pos } ));
		}
		for (auto& o : outputs) {
			c.outputs.emplace_back(std::make_unique<Part>( nullptr, o.name, Placement{ o.pos } ));
		}

		return c;
	}
	
	// Not const because we use Chip* for both editable (user-defined) chips and primitve gates
	// alternatively just cast const away?
	inline Chip gates[GATE_COUNT] = {
		_GATE("<input>",  srgb(190,255,  0), float2(PIN_LENGTH+0.2f, PIN_SIZE+0.1f), {{"In", float2(0)}}, {{"Out", float2(0)}}),
		_GATE("<output>", srgb(255, 10, 10), float2(PIN_LENGTH+0.2f, PIN_SIZE+0.1f), {{"In", float2(0)}}, {{"Out", float2(0)}}),

		_GATE("Buffer Gate", lrgb(0.5f, 0.5f,0.75f), float2(1,0.5f), {{"In", float2(-0.25f, +0)}}, {{"Out", float2(0.25f, 0)}}),
		_GATE("NOT Gate",    lrgb(   0,    0,    1), float2(1,0.5f), {{"In", float2(-0.25f, +0)}}, {{"Out", float2(0.25f, 0)}}),
		_GATE("AND Gate",    lrgb(   1,    0,    0), float2(1,   1), {{"A", float2(-0.25f, +0.25f)}, {"B", float2(-0.25f, -0.25f)}}, {{"Out", float2(0.25f, 0)}}),
		_GATE("NAND Gate",   lrgb(0.5f,    1,    0), float2(1,   1), {{"A", float2(-0.25f, +0.25f)}, {"B", float2(-0.25f, -0.25f)}}, {{"Out", float2(0.25f, 0)}}),
		_GATE("OR Gate",     lrgb(   1, 0.5f,    0), float2(1,   1), {{"A", float2(-0.25f, +0.25f)}, {"B", float2(-0.25f, -0.25f)}}, {{"Out", float2(0.25f, 0)}}),
		_GATE("NOR Gate",    lrgb(   0,    1, 0.5f), float2(1,   1), {{"A", float2(-0.25f, +0.25f)}, {"B", float2(-0.25f, -0.25f)}}, {{"Out", float2(0.25f, 0)}}),
		_GATE("XOR Gate",    lrgb(   0,    1,    0), float2(1,   1), {{"A", float2(-0.25f, +0.25f)}, {"B", float2(-0.25f, -0.25f)}}, {{"Out", float2(0.25f, 0)}}),

		_GATE("AND-3 Gate",  lrgb(   1,    0,    0), float2(1,   1), {{"A", float2(-0.25f, +0.25f)}, {"B", float2(-0.25f, 0)}, {"C", float2(-0.25f, -0.25f)}}, {{"Out", float2(0.25f, 0)}}),
		_GATE("NAND-3 Gate", lrgb(0.5f,    1,    0), float2(1,   1), {{"A", float2(-0.25f, +0.25f)}, {"B", float2(-0.25f, 0)}, {"C", float2(-0.25f, -0.25f)}}, {{"Out", float2(0.25f, 0)}}),
		_GATE("OR-3 Gate",   lrgb(   1, 0.5f,    0), float2(1,   1), {{"A", float2(-0.25f, +0.25f)}, {"B", float2(-0.25f, 0)}, {"C", float2(-0.25f, -0.25f)}}, {{"Out", float2(0.25f, 0)}}),
		_GATE("NOR-3 Gate",  lrgb(   0,    1, 0.5f), float2(1,   1), {{"A", float2(-0.25f, +0.25f)}, {"B", float2(-0.25f, 0)}, {"C", float2(-0.25f, -0.25f)}}, {{"Out", float2(0.25f, 0)}}),
	};
	
	inline bool is_gate (Chip* chip) {
		return chip >= gates && chip < &gates[GATE_COUNT];
	}
	inline GateType gate_type (Chip* chip) {
		assert(is_gate(chip));
		return (GateType)(chip - gates);
	}
	
	inline float2 get_inp_pos (Part& pin_part) {
		return pin_part.pos.calc_matrix() * float2(-PIN_LENGTH/2, 0);
	}
	inline float2 get_out_pos (Part& pin_part) {
		return pin_part.pos.calc_matrix() * float2(+PIN_LENGTH/2, 0);
	}
	
	inline float2x3 get_inp_pos_invmat (Part& pin_part) {
		return translate(float2(+PIN_LENGTH/2, 0)) * pin_part.pos.calc_inv_matrix();
	}
	inline float2x3 get_out_pos_invmat (Part& pin_part) {
		return translate(float2(-PIN_LENGTH/2, 0)) * pin_part.pos.calc_inv_matrix();
	}

////
	inline int update_state_indices (Chip& chip) {
		// state count cached, early out
		if (chip.state_count >= 0)
			return chip.state_count;
			
		// state count stale, recompute
		// states are placed flattened in order of depth first traversal of part (chip instance) tree
		chip.state_count = 0;
		
		for (auto& part : chip.outputs)
			part->state_idx = chip.state_count++;
		for (auto& part : chip.inputs)
			part->state_idx = chip.state_count++;

		for (auto& part : chip.parts) {
			part->state_idx = chip.state_count;
			// allocate as many states as are needed recursively for this part
			chip.state_count += update_state_indices(*part->chip);
		}

		return chip.state_count;
	}

	inline int indexof_chip (std::vector<std::shared_ptr<Chip>> const& vec, Chip* chip) {
		int idx = indexof(vec, chip, [] (std::shared_ptr<Chip> const& l, Chip* r) { return l.get() == r; });
		return idx;
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
		friend void from_json (const json& j, LogicSim& sim, Camera2D& cam);


		std::vector<std::shared_ptr<Chip>> saved_chips;

		std::shared_ptr<Chip> viewed_chip;

		std::vector<uint8_t> state[2];

		int cur_state = 0;

		void switch_to_chip_view (std::shared_ptr<Chip> chip, Camera2D& cam) {
			// TODO: delete chip warning if main_chip will be deleted by this?
			viewed_chip = std::move(chip); // move copy of shared ptr (ie original still exists)

			// not actually needed?
			update_all_chip_state_indices();

			for (int i=0; i<2; ++i) {
				state[i].assign(viewed_chip->state_count, 0);
				state[i].shrink_to_fit();
			}
			cur_state = 0;

			float sz = max(viewed_chip->size.x, viewed_chip->size.y);
			sz = clamp(sz, 8.0f, 256.0f);

			cam.pos = 0;
			cam.zoom_to(sz * 1.25f);
		}
		void reset_chip_view (Camera2D& cam) {
			switch_to_chip_view(
				std::make_shared<Chip>("", lrgb(1), float2(10, 6)),
				cam);
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

		struct Hover {
			enum Type {
				NONE = 0,
				PART,
				PIN_INP,
				PIN_OUT,
			};
			Type type = NONE;

			Part* part = nullptr;
			Chip* chip = nullptr;
			int   pin = -1;
			
			int part_state_idx = -1; // needed to toggle gates even when they are part of a chip placed in the viewed chip
			
			float2x3 world2chip = float2x3(0); // world2chip during hitbox test

			operator bool () {
				return type != NONE;
			}

			bool is_part (Part* part) {
				return this->part == part;
			}
		};
		
		struct PartSelection {
			Chip* chip = nullptr;

			struct Item {
				Part*  part;
				float2 bounds_offs;
			};
			struct _cmp {
				bool operator() (Item const& l, Item const& r) { return l.part == r.part; }
			};
			std::vector<Item> items; // TODO: use custom hashset here
			
			float2x3 world2chip = float2x3(0);
			
			AABB bounds;
			
			operator bool () {
				return !items.empty();
			}

			static bool _cmp (Item const& i, Part const* p) { return i.part == p; }
			
			bool has_part (Chip* chip, Part* part) {
				return this->chip == chip && contains(items, part, _cmp);
			}
			bool toggle_part (Part* part) {
				int idx = indexof(items, part, _cmp);
				if (idx < 0)
					items.push_back({ part, 0 });
				else
					items.erase(items.begin() + idx);

				return idx < 0; // if was added
			}
		};
		
		struct PartPreview {
			Chip* chip = nullptr;
			Placement pos = {};
		};

		struct ViewMode {

			Hover toggle_gate = {};
			bool state_toggle_value; // new state value while toggle is 'held'
		};
		struct EditMode {

			PartSelection sel = {};

			bool dragging = false; // dragging selection
			float2 drag_offset;
		};
		struct PlaceMode {
			
			PartPreview preview_part = {};
		};
		struct WireMode {
			
			Chip* chip = nullptr;

			float2x3 world2chip = float2x3(0); // world2chip during hitbox test

			// wiring direction false: out->inp  true: inp->out
			bool dir;

			struct Connection {
				Part* part = nullptr;
				int   pin = 0;
			};
			Connection src = {}; // where wiring started
			Connection dst = {}; // where wiring ended

			float2 unconn_pos;

			std::vector<float2> points;
		};

		std::variant<ViewMode, EditMode, PlaceMode, WireMode>
			mode = ViewMode();

		template <typename T> bool in_mode () {
			return std::holds_alternative<T>(mode);
		}

		Hover hover = {};

		void reset () {
			mode = ViewMode();
			hover = {};
		}

		float snapping_size = 0.125f;
		bool snapping = true;
	
		float2 snap (float2 pos) {
			return snapping ? round(pos / snapping_size) * snapping_size : pos;
		}

		bool _cursor_valid;
		float2 _cursor_pos;

		int chips_reorder_src = -1;
		
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
		
	////
		void select_gate_imgui (LogicSim& sim, const char* name, Chip* type);
		
		void saved_chip_imgui (LogicSim& sim, std::shared_ptr<Chip>& chip, bool can_place, bool is_viewed);
		void saved_chips_imgui (LogicSim& sim, Camera2D& cam);
		
		void viewed_chip_imgui (LogicSim& sim);
		void selection_imgui (PartSelection& sel);

		void imgui (LogicSim& sim, Camera2D& cam);
		
	////
		void add_part (LogicSim& sim, Chip& chip, PartPreview& part);
		void remove_part (LogicSim& sim, Chip* chip, Part* part);
		void add_wire (WireMode& wire);

		void edit_part (Input& I, LogicSim& sim, Chip& chip, Part& part, float2x3 const& world2chip, int state_base);
		void edit_chip (Input& I, LogicSim& sim, Chip& chip, float2x3 const& world2chip, int state_base);

		void update (Input& I, Game& g);
		
		void update_toggle_gate (Input& I, LogicSim& sim, Window& window);
	};
	
}
