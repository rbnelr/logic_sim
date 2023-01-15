#pragma once
#include "common.hpp"
#include "camera.hpp"
#include "opengl/renderer.hpp"

#include <variant>
#include <unordered_set>

namespace ogl { struct Renderer; }

// Use vector like a set
// because std::unordered_set< std::unique_ptr<T> > does not work before C++20 (cannot lookup via T*)

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

	using _partptr_equal = _equal< std::unique_ptr<Part> >;

	struct WireNode {
		float2 pos;
		
		Part* parent_part = nullptr;
		vector_set<WireNode*> edges = {}; // TODO: small vec opt to 4 entries

		int num_wires () {
			return edges.size() + (parent_part ? 1 : 0);
		}

		AABB get_aabb () const {
			return AABB{ pos - PIN_SIZE*0.5f,
			             pos + PIN_SIZE*0.5f };
		}
	};
	struct WireEdge {
		WireNode* a;
		WireNode* b;
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
		vector_set< std::unique_ptr<Part> > parts = {};

		vector_set< std::unique_ptr<WireNode> > wire_nodes = {};
		vector_set< std::unique_ptr<WireEdge> > wire_edges = {};

		int _recurs = 0;

		
		vector_set<Chip*> users;
		
		// TODO: store set of direct users of chip as chip* -> usecount hashmap
		// adding a chip a as a part inside a chip c is a->users[c]++
		// this can be iterated to find if chip can be placed
		// this also can be iterated to recompute state indices on chip modification

		bool contains_part (Part* part) {
			return parts.contains(part) ||
				contains(outputs, part, _partptr_equal()) ||
				contains(inputs, part,  _partptr_equal());
		}

		Chip () = default;

		// move needed for  Chip gates[GATE_COUNT]  be careful not to move a chip after init, because we need stable pointers for parts
		Chip (Chip&&) = default;
		Chip& operator= (Chip&&) = default;
		// can't copy, use deep_copy()
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
		std::vector<InputWire> inputs;

		struct Pin {
			std::unique_ptr<WireNode> node = nullptr;
		};
		std::vector<Pin> pins;
		
		Part (Chip* chip, std::string&& name, Placement pos): chip{chip}, pos{pos}, name{name} {
			if (chip) {
				inputs.resize(chip->inputs.size());
				pins.resize(chip->inputs.size() + chip->outputs.size());

				for (auto& pin : pins)
					pin.node = std::make_unique<WireNode>(float2(0), this);
			}
		}
		
		// cannot more or copy part because pointer needs to be stable for wire nodes
		Part (Part const& p) = delete;
		Part& operator= (Part const& p) = delete;
		Part (Part&& p) = delete;
		Part& operator= (Part&& p) = delete;

		AABB get_aabb () const {
			// mirror does not matter
			float2 size = chip->size * pos.scale;

			// Does not handle non-90 deg rotations
			size = abs(ROT[pos.rot] * size);

			return AABB{ pos.pos - size*0.5f,
			             pos.pos + size*0.5f };
		}

		void update_pins_pos ();
	};

	enum ThingType {
		T_NONE=0,
		T_PART,
		T_NODE,
		T_PIN,
		T_WIRE,
	};
	struct ThingPtr {
		ThingType type;
		union {
			void*      _ptr;
			Part*      part;
			WireNode*  node;
			Part::Pin* pin;
			WireEdge*  wire;
		};

		ThingPtr ()                                   : type{T_NONE}, part{nullptr} {}
		ThingPtr (Part* part)                         : type{T_PART}, part{part} {
			assert(part != nullptr);
		}
		ThingPtr (WireNode* node)                     : type{T_NODE}, node{node} {
			assert(node != nullptr);
		}
		ThingPtr (Part::Pin* pin)                     : type{T_PIN}, pin{pin} {
			assert(pin != nullptr);
		}
		ThingPtr (WireEdge* wire)                     : type{T_WIRE}, wire{wire} {
			assert(wire != nullptr);
		}

		operator bool () const { return type != T_NONE; }

		bool operator== (ThingPtr const& r) const {
			// TODO: using _ptr safe?
			return type == r.type && _ptr == r._ptr;
		}
		
		AABB get_aabb () const;
		float2& get_pos ();
	};


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
	
	inline void Part::update_pins_pos () {
		auto mat = pos.calc_matrix();

		int i = 0;
		for (auto& pin : chip->inputs)
			pins[i++].node->pos = mat * get_inp_pos(*pin);
		for (auto& pin : chip->outputs)
			pins[i++].node->pos = mat * get_out_pos(*pin);
	}

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
		
		struct WireConn {
			Part* part = nullptr;
			int   pin = 0;
		};

		void add_wire (Chip& chip, WireConn src, WireConn dst, std::vector<float2>&& wire_points);
		void remove_wire (Chip& chip, WireConn dst);
		
		WireNode* add_wire_node (Chip& chip, float2 pos) {
			auto ptr = new WireNode(pos);
			chip.wire_nodes.add(std::unique_ptr<WireNode>(ptr));
			return ptr;
		}
		void disconnect_wire_node (Chip& chip, WireNode* node) {
			int count = node->edges.size();

			for (WireNode* n : node->edges) {
				n->edges.remove(node);
			}

			int removed_edges = chip.wire_edges.remove_if(
				[&] (std::unique_ptr<WireEdge>& edge) {
					return edge->a == node || edge->b == node;
				});
			assert(count == removed_edges);
		}
		void remove_wire_node (Chip& chip, WireNode* node) {
			disconnect_wire_node(chip, node);

			bool removed = chip.wire_nodes.try_remove(node);
			assert(removed);
		}
		// existing connections are allowed as inputs but will be ignored
		void connect_wire_nodes (Chip& chip, WireNode* a, WireNode* b) {
			bool existing = a->edges.contains(b);
			assert(existing == b->edges.contains(a));
			if (existing) return;

			a->edges.add(b);
			b->edges.add(a);

			chip.wire_edges.add(std::make_unique<WireEdge>(a, b));
		}
		void remove_wire_edge (Chip& chip, WireEdge* edge) {
			
			edge->a->edges.remove(edge->b);
			edge->b->edges.remove(edge->a);

			chip.wire_edges.remove(edge);
		}

		void simulate (Input& I);
	};
	
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
			WireNode* prev = nullptr;
			WireNode* cur;

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
