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

// Use vector like a set
// because std::unordered_set< std::unique_ptr<T> > does not work before C++20 (cannot lookup via T*)

// Acts like a set in that add/remove is O(1)
// but implemented as a vector so elements are ordered
// but order is unstable, ie changes on remove (implemented as a swap with last)
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

namespace logic_sim {
	
	inline constexpr float SEL_HIGHL_SHRINK = 1.0f/64;

	inline constexpr float WIRE_RADIUS = 0.04f;
	inline constexpr float WIRE_NODE_RADIUS_FAC = 2.25f;

	inline constexpr float PIN_SIZE   = 0.2f; // IO Pin hitbox size
	inline constexpr float PIN_LENGTH = 0.5f; // IO Pin base wire length

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
	struct WireNode;

	struct Partptr_equal {
		inline bool operator() (std::unique_ptr<Part> const& l, Part const* r) {
			return l.get() == r;
		}
		inline bool operator() (std::unique_ptr<Part> const& l, std::unique_ptr<Part> const& r) {
			return l == r;
		};
	};
	
	enum ThingType {
		T_NONE=0,
		T_PART,
		T_NODE,
		T_PIN_INP,
		T_PIN_OUT,
	};
	struct ThingPtr {
		ThingType type;
		int       pin;
		union {
			Part*     part;
			WireNode* node;
		};

		ThingPtr ()                                   : type{T_NONE}, pin{0}, part{nullptr} {}
		ThingPtr (Part* part)                         : type{T_PART}, pin{0}, part{part} {
			assert(part != nullptr);
		}
		ThingPtr (WireNode* node)                     : type{T_NODE}, pin{0}, node{node} {
			assert(node != nullptr);
		}
		ThingPtr (ThingType type, Part* part, int pin): type{type}, pin{pin}, part{part} {
			assert(part != nullptr);
		}
		
		operator bool () const { return type != T_NONE; }

		bool operator== (ThingPtr const& r) const {
			return memcmp(this, &r, sizeof(ThingPtr)) == 0;
		}
		
		AABB get_aabb () const;
		float2& get_pos ();
		float2 get_wire_pos () const;
	};

	struct WireNode {
		float2 pos;
		
		VectorSet<ThingPtr> edges; // TODO: small vec opt to 4 entries

		AABB get_aabb () const {
			return AABB{ pos - PIN_SIZE*0.5f,
			             pos + PIN_SIZE*0.5f };
		}
	};
	struct WireEdge {
		ThingPtr a; // WireNode* or T_PIN_INP or T_PIN_OUT
		ThingPtr b;
	};
	//struct WireGraph {
	//	VectorSet<WireNode*> nodes; // free nodes and part nodes
	//	VectorSet<WireEdge*> edges; // can be generated from nodes, should this even be cached at all?
	//
	//	int state_id = -1;
	//};
	//struct Part {
	//	struct IO {
	//		WireNode node;
	//		int state_id = -1;
	//	};
	//	vector<IO> ios;
	//};
	//struct LogicGraph {
	//	VectorSet<WireGraph> graphs;
	//
	//
	//};


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

		VectorSet< std::unique_ptr<WireNode> > wire_nodes = {};
		VectorSet< std::unique_ptr<WireEdge> > wire_edges = {};

		int _recurs = 0;

		
		VectorSet<Chip*> users;
		
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
		Chip& operator= (Chip&&) = default;

		Chip (Chip const&) = delete;
		Chip& operator= (Chip const&) = delete;
		
		Chip deep_copy () const;
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
		
		AABB get_aabb () const {
			// mirror does not matter
			float2 size = chip->size * pos.scale;

			// Does not handle non-90 deg rotations
			size = abs(ROT[pos.rot] * size);

			return AABB{ pos.pos - size*0.5f,
			             pos.pos + size*0.5f };
		}
	};
	
	struct WireConn {
		Part* part = nullptr;
		int   pin = 0;
	};

////
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

		bool unsaved_changes = false;
		
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
		void recompute_chip_users ();

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
			if (unsaved_changes) {
				ImGui::TextColored(ImVec4(1.00f, 0.67f, 0.00f, 1), "Unsaved changes");
			}
			else {
				ImGui::Text("No unsaved changes");
			}
			ImGui::Text("Gates (# of states): %d", (int)state[0].size());
		}
		
		// delete chip from saved_chips, and reset viewed chip such that chip will actually be deleted
		// this is probably the least confusing option for the user
		void delete_chip (Chip* chip, Camera2D& cam) {
			assert(chip->users.empty()); // hopefully users is correct or we will crash
			
			int idx = indexof_chip(saved_chips, chip);
			if (idx >= 0)
				saved_chips.erase(saved_chips.begin() + idx);
			
			if (viewed_chip.get() == chip)
				reset_chip_view(cam);

			unsaved_changes = true;
		}
		
		void add_part (Chip& chip, Chip* part_chip, Placement part_pos);
		void remove_part (Chip& chip, Part* part);
		
		void add_wire (Chip& chip, WireConn src, WireConn dst, std::vector<float2>&& wire_points);
		void remove_wire (Chip& chip, WireConn dst);


		void simulate (Input& I);
	};
	
	struct Editor {
		
		struct PartSelection {
			VectorSet<ThingPtr> items;
			
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
		
		struct ViewMode {
			struct Hover_Part {
				Part* part = nullptr;
				int   sid = -1;

				float2x3 part2world;
			};
			Hover_Part hover_part = {};

			int toggle_sid = -1;
			bool state_toggle_value; // new state value while toggle is 'held'

			
			void find_hover (float2 cursor_pos, Chip& chip,
					float2x3 chip2world, float2x3 world2chip, int sid);
		};
		struct PlaceMode {
			Chip*     place_chip = nullptr;
			Placement place_pos = {};
		};
		struct EditMode {

			PartSelection sel = {};

			bool dragging = false; // dragging selection
			float2 drag_start;
			float2 drag_offset;

			bool box_selecting = false;
			float2 box_sel_start;
		};
		struct WireMode {
			ThingPtr prev = {};
			ThingPtr cur;

			// buf for new node, cur points to this if new node will be created
			WireNode  node;
		};

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

		ModeVariant mode = ViewMode();

		template <typename T> bool in_mode () {
			return std::holds_alternative<T>(mode);
		}

		void reset () {
			mode = ViewMode();
		}

		float snapping_size = 0.125f;
		bool snapping = true;
	
		float2 snap (float2 pos) {
			return snapping ? round(pos / snapping_size) * snapping_size : pos;
		}

		int chips_reorder_src = -1;
		
		bool _cursor_valid;
		float2 _cursor_pos;

	////
		void select_gate_imgui (LogicSim& sim, const char* name, Chip* type);
		
		void viewed_chip_imgui (LogicSim& sim, Camera2D& cam);

		void saved_chip_imgui (LogicSim& sim, std::shared_ptr<Chip>& chip, bool can_place, bool is_viewed);
		void saved_chips_imgui (LogicSim& sim, Camera2D& cam);
		
		void selection_imgui (PartSelection& sel);

		void imgui (LogicSim& sim, Camera2D& cam);
		
	////
		void update (Input& I, LogicSim& sim, ogl::Renderer& r);
		
		void update_toggle_gate (Input& I, LogicSim& sim, Window& window);
	};
	
} // namespace logic_sim
