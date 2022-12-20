#pragma once
#include "common.hpp"
#include "camera.hpp"
#include "opengl/renderer.hpp"

#include <variant>
#include <unordered_set>

namespace ogl { struct Renderer; }

template<typename T>
inline std::unique_ptr<T[]> insert (std::unique_ptr<T[]>& old, int old_size, int idx, T&& val) {
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
inline std::unique_ptr<T[]> erase (std::unique_ptr<T[]>& old, int old_size, int idx) {
	assert(old_size > 0 && idx < old_size);
		
	auto ptr = std::make_unique<T[]>(old_size - 1);

	for (int i=0; i<idx; ++i)
		ptr[i] = std::move(old[i]);

	for (int i=idx+1; i<old_size; ++i)
		ptr[i-1] = std::move(old[i]);

	return ptr;
}

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

// Acts like a set, ie. elements are unordered
//  but implemented as a vector because std::unordered_set< std::unique_ptr<T> > does not work <C++20 (cannot search by raw pointer)
// though iteration of elements is supported,
//  so there is an order which is garantueed to stay fixed as long as no elements are added or removes
//  on add or remove the order is invalidated, ie may change arbitrarily
template <typename T, typename EQUAL=std::equal_to<T>>
struct VectorSet {
	std::vector<T> vec;

	typedef std::vector<T>::iterator       it_t;
	typedef std::vector<T>::const_iterator cit_t;

	VectorSet () {}

	VectorSet (int size, T const& val): vec{(size_t)size, val} {}
	VectorSet (int size): vec{(size_t)size} {}

	VectorSet (std::initializer_list<T> list): vec{list} {}

	VectorSet (VectorSet&& v): vec{std::move(v.vec)} {}
	VectorSet& operator= (VectorSet&& v) { vec = std::move(v.vec); return *this; }

	VectorSet (VectorSet const& v): vec{v.vec} {}
	VectorSet& operator= (VectorSet const& v) { vec = v.vec; return *this; }

	it_t begin () { return vec.begin(); }
	it_t end () { return vec.end(); }
	cit_t begin () const { return vec.begin(); }
	cit_t end () const { return vec.end(); }

	int size () const { return (int)vec.size(); }
	bool empty () const { return vec.empty(); }

	void clear () { vec.clear(); }
	void reserve (int size) { vec.reserve(size); }

	T& operator[] (int i) {
		assert(i >= 0 && i < (int)vec.size());
		return vec[i];
	}
	T const& operator[] (int i) const {
		assert(i >= 0 && i < (int)vec.size());
		return vec[i];
	}

	template <typename U>
	bool contains (U const& val) {
		return ::indexof(vec, val, EQUAL()) >= 0;
	}

	template <typename U>
	void add (U&& val) {
		assert(!contains(val));
		vec.emplace_back(std::move(val));
	}
	//void add (T const& val) {
	//	add(val);
	//}

	template <typename U>
	bool try_add (U&& val) {
		if (contains(val))
			return false;
		vec.emplace_back(std::move(val));
		return true;
	}
	template <typename U>
	bool try_remove (U const& val) {
		int idx = indexof(vec, val, EQUAL());
		if (idx < 0)
			return false;
		
		vec[idx] = std::move(vec[(int)vec.size()-1]);
		vec.pop_back();
		return true;
	}
	
	template <typename U>
	bool toggle (U const& val) {
		int idx = indexof(vec, val, EQUAL());
		if (idx < 0)
			vec.emplace_back(std::move(val));
		else {
			vec[idx] = std::move(vec[(int)vec.size()-1]);
			vec.pop_back();
		}
		return idx < 0; // if was added
	}
};

template <typename T>
inline std::unique_ptr<T[]> deep_copy (std::unique_ptr<T[]> const& arr, int size) {
	auto arr2 = std::make_unique<T[]>(size);
	for (int i=0; i<size; ++i)
		arr2[i] = arr[i];
	return arr2;
}
template <typename T>
inline std::vector<std::unique_ptr<T>> deep_copy (std::vector<std::unique_ptr<T>> const& vec) {
	std::vector<std::unique_ptr<T>> vec2(vec.size());
	for (size_t i=0; i<vec.size(); ++i)
		vec2[i] = std::make_unique<T>(*vec[i]);
	return vec2;
}
template <typename T, typename EQUAL>
inline VectorSet<std::unique_ptr<T>, EQUAL> deep_copy (VectorSet<std::unique_ptr<T>, EQUAL> const& vec) {
	VectorSet<std::unique_ptr<T>, EQUAL> vec2(vec.size());
	for (int i=0; i<vec.size(); ++i)
		vec2[i] = std::make_unique<T>(*vec[i]);
	return vec2;
}

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

		bool is_inside (float2 point) const {
			return point.x >= lo.x && point.x < hi.x &&
			       point.y >= lo.y && point.y < hi.y;
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

		void rotate_around (float2 center, short ang) {
			pos = (ROT[wrap(ang, 4)] * (pos - center)) + center;

			rot = wrap(rot + ang, 4);
		}
		void mirror_around (float2 center) {
			//pos = (MIRROR[1] * (pos - center)) + center;
			pos.x = center.x - (pos.x - center.x);

			if (rot % 2)
				rot = wrap(rot + 2, 4);

			mirror = !mirror;
		}
	};
	
////
	struct Part;

	struct Partptr_equal {
		inline bool operator() (std::unique_ptr<Part> const& l, Part const* r) {
			return l.get() == r;
		};
	};

	// A chip design that can be edited or simulated if viewed as the "global" chip
	// Uses other chips as parts, which are instanced into it's own editing or simulation
	// (but cannot use itself as part because this would cause infinite recursion)
	struct Chip {
		std::string name = "";
		lrgb        col = lrgb(1);
		
		float2 size = float2(10, 6);
		
		// how many total outputs are used (recursively)
		// and thus how many state vars need to be allocated
		// when this chip is placed in the simulation
		int state_count = -1; // -1 if stale

		std::vector< std::unique_ptr<Part> > outputs = {};
		std::vector< std::unique_ptr<Part> > inputs = {};
		
		//std::unordered_set< std::unique_ptr<Part> > parts = {};
		VectorSet< std::unique_ptr<Part>, Partptr_equal > parts = {};

		int _recurs = 0;

		
		// TODO: store set of direct users of chip as chip* -> usecount hashmap
		// adding a chip a as a part inside a chip c is a->users[c]++
		// this can be iterated to find if chip can be placed
		// this also can be iterated to recompute state indices on chip modification

		bool contains_part (Part* part) {
			return parts.contains(part) ||
				contains(outputs, part, Partptr_equal()) ||
				contains(inputs, part, Partptr_equal());
		}

		Chip () = default;
		Chip (Chip&&) = default;
		Chip (Chip const& chip): name{chip.name}, col{chip.col}, size{chip.size},
			outputs{ deep_copy(chip.outputs) },
			inputs { deep_copy(chip.inputs)  },
			parts  { deep_copy(chip.parts)   } {}
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
		int sid = -1; // check parent chip for state state_count, then this is also stale

		struct InputWire {
			Part* part = nullptr;
			// which output pin of the part is connected to
			int pin = 0;
			
			//int state_idx = 0;

			std::vector<float2> wire_points;
		};
		std::unique_ptr<InputWire[]> inputs;
		
		Part (Chip* chip, std::string&& name, Placement pos): chip{chip}, pos{pos}, name{name},
			inputs {std::make_unique<InputWire []>(chip ? chip->inputs .size() : 0)} {}
		
		Part (Part const& part): chip{part.chip}, name{part.name}, pos{part.pos},
			inputs{deep_copy(part.inputs, (int)part.chip->inputs.size())} {}

		AABB get_aabb (float padding=0) const {
			// mirror does not matter
			float2 size = chip->size * pos.scale;

			// Does not handle non-90 deg rotations
			size = abs(ROT[pos.rot] * size);

			return AABB{ pos.pos - size*0.5f - padding,
			             pos.pos + size*0.5f + padding };
		}
	};
	
	// Can uniquely identify a chip instance, needed for editor interations
	// This is safe as long as the ids are not recomputed
	// this only happens when parts are added or deleted, in which case the selection is reset
	struct ChipInstanceID {
		Chip* ptr = nullptr;
		int   sid = 0;

		operator bool () {
			return ptr;
		}

		bool operator== (ChipInstanceID const& r) {
			return ptr == r.ptr && sid == r.sid;
		}
		bool operator!= (ChipInstanceID const& r) {
			return !(sid == r.sid);
		}
	};


////
	inline constexpr float PIN_SIZE   = 0.225f; // IO Pin hitbox size
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
		_GATE("<input>",  srgb(190,255,  0), float2(PIN_LENGTH, 0.25f), {{"In", float2(0)}}, {{"Out", float2(0)}}),
		_GATE("<output>", srgb(255, 10, 10), float2(PIN_LENGTH, 0.25f), {{"In", float2(0)}}, {{"Out", float2(0)}}),

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
		friend void from_json (const json& j, LogicSim& sim);


		std::vector<std::shared_ptr<Chip>> saved_chips;

		std::shared_ptr<Chip> viewed_chip;

		std::vector<uint8_t> state[2];

		int cur_state = 0;
		
		static int update_state_indices (Chip& chip) {
			// state count cached, early out
			if (chip.state_count >= 0)
				return chip.state_count;
			
			// state count stale, recompute
			// states are placed flattened in order of depth first traversal of part (chip instance) tree
			chip.state_count = 0;
		
			for (auto& part : chip.outputs)
				part->sid = chip.state_count++;
			for (auto& part : chip.inputs)
				part->sid = chip.state_count++;

			for (auto& part : chip.parts) {
				part->sid = chip.state_count;
				// allocate as many states as are needed recursively for this part
				chip.state_count += update_state_indices(*part->chip);
			}

			return chip.state_count;
		}
		void update_all_chip_state_indices () {

			// invalidate all chips and recompute state_counts
			for (auto& c : saved_chips)
				c->state_count = -1;
			viewed_chip->state_count = -1;

			for (auto& c : saved_chips)
				update_state_indices(*c);
			update_state_indices(*viewed_chip);
		}
		
		void switch_to_chip_view (std::shared_ptr<Chip> chip) {
			// TODO: delete chip warning if main_chip will be deleted by this?
			viewed_chip = std::move(chip); // move copy of shared ptr (ie original still exists)

			update_all_chip_state_indices();

			for (int i=0; i<2; ++i) {
				state[i].assign(viewed_chip->state_count, 0);
				state[i].shrink_to_fit();
			}
			cur_state = 0;
		}
		void reset_chip_view (Camera2D& cam) {
			switch_to_chip_view(std::make_shared<Chip>());
			adjust_camera_for_viewed_chip(cam);
		}

		void adjust_camera_for_viewed_chip (Camera2D& cam) {
			float sz = max(viewed_chip->size.x, viewed_chip->size.y);
			sz = clamp(sz, 8.0f, 256.0f);

			cam.pos = 0;
			cam.zoom_to(sz * 1.25f);
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
			
			ChipInstanceID chip = {};
			Part* part = nullptr;

			int   pin = -1;
			
			float2x3 chip2world;
			float2x3 world2chip;
			
			operator bool () {
				return type != NONE;
			}
		};
		
		struct PartSelection {
			struct Item {
				Part*  part;
				float2 bounds_offs;
			};
			struct ItemCmp {
				inline bool operator() (Item const& l, Item const& r) { return l.part == r.part; }
				inline bool operator() (Item const& l, Part* r) { return l.part == r; }
			};

			ChipInstanceID chip = {};
			VectorSet<Item, ItemCmp> items; // TODO: use custom hashset here
			
			float2x3 chip2world;
			float2x3 world2chip;
			
			AABB bounds;
			
			operator bool () {
				return !items.empty();
			}

			static bool _cmp (Item const& i, Part const* p) { return i.part == p; }
			
			bool has_part (Chip* chip, Part* part) {
				if (this->chip.ptr != chip) assert(!items.contains(part));
				return this->chip.ptr == chip && items.contains(part);
			}
			bool toggle_part (Part* part) {
				return items.toggle(Item{ part }); // if was added
			}

			void add (PartSelection& r) {
				assert(chip == r.chip);

				for (auto& it : r.items) {
					items.try_add(Item{ it.part });
				}
			}
			void remove (PartSelection& r) {
				assert(chip == r.chip);

				for (auto& it : r.items) {
					items.try_remove(it.part);
				}
			}

			// Selection and Hover can be compared for their chip_sid to determine
			// if they refer to the same chip instance
			// This is safe as long as the ids are not recomputed
			// this only happens when parts are added or deleted, in which case the selection is reset
			bool inst_contains_inst (Hover& hov) {
				if (chip.ptr != hov.chip.ptr) assert(!items.contains(hov.part));
				return chip == hov.chip && items.contains(hov.part);
			}
		};
		
		struct PartPreview {
			Chip* chip = nullptr;
			Placement pos = {};
		};

		struct WireConn {
			Part* part = nullptr;
			int   pin = 0;
		};

		struct ViewMode {
			int toggle_sid = -1;
			bool state_toggle_value; // new state value while toggle is 'held'
		};
		struct EditMode {

			PartSelection sel = {};

			bool dragging = false; // dragging selection
			float2 drag_offset;

			bool box_selecting = false;
			float2 box_sel_start;
		};
		struct PlaceMode {
			
			PartPreview preview_part = {};
		};
		struct WireMode {
			
			ChipInstanceID chip = {};

			float2x3 world2chip = float2x3(0); // world2chip during hitbox test

			// wiring direction false: src->dst  true: dst->src
			bool dir;

			WireConn src = {}; // where wiring started
			WireConn dst = {}; // where wiring ended

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
			assert(_cursor_valid);
			
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
		
		void add_wire (LogicSim& sim, Chip* chip, WireConn src, WireConn dst, std::vector<float2>&& wire_points);
		void remove_wire (LogicSim& sim, Chip* chip, WireConn dst);

		struct SelectInput {
			ChipInstanceID only_chip = {}; // only hover in chip instance  null -> in all
			bool allow_pins;
			bool allow_parts;
		};
		void find_hover (Chip& chip, SelectInput& I,
			float2x3 const& chip2world, float2x3 const& world2chip, int state_base);

		void find_boxsel (Chip& chip, bool remove, AABB box,
			float2x3 const& chip2world, float2x3 const& world2chip, int state_base,
			PartSelection& sel);

		void update (Input& I, LogicSim& sim, ogl::Renderer& r);
		
		void update_toggle_gate (Input& I, LogicSim& sim, Window& window);
	};
	
}
