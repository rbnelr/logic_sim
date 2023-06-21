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

inline float wire_radius = 0.30f;
inline float wire_node_junction = 2.0f;
inline float wire_node_dead_end = 1.3f;

constexpr lrgba line_col = lrgba(1.0f, 0.015f, 0.03f, 1);
constexpr lrgba preview_line_col = line_col * lrgba(1,1,1, 0.75f);

inline constexpr float2x2 ROT[] = {
	float2x2(  1, 0,  0, 1 ),
	float2x2(  0, 1, -1, 0 ),
	float2x2( -1, 0,  0,-1 ),
	float2x2(  0,-1,  1, 0 ),
};
inline constexpr float2x2 INV_ROT[] = {
	float2x2(  1, 0,  0, 1 ),
	float2x2(  0,-1,  1, 0 ),
	float2x2( -1, 0,  0,-1 ),
	float2x2(  0, 1, -1, 0 ),
};
	
inline constexpr float2x2 MIRROR[] = {
	float2x2(  1, 0,  0, 1 ),
	float2x2( -1, 0,  0, 1 ),
};

/*
inline constexpr int2 roti (int rot, int2 vec) {
	switch (rot & 3) {
		case 0: return vec;
		case 1: return int2(-vec.y, vec.x);
		case 2: return -vec;
		default:
		case 3: return int2(vec.y, -vec.x);
	}
}*/

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
	SERIALIZE(Placement, pos, rot, mirror)

	float2 pos    = 0;
	short  rot    = 0;
	bool   mirror = false;

	float2x3 calc_matrix () {
		return translate(pos) * (ROT[rot] * MIRROR[mirror]);
	}
	float2x3 calc_inv_matrix () {
		return (MIRROR[mirror] * INV_ROT[rot]) * translate(-pos);
	}

	void mirror_around (float2 center) {
		pos.x = center.x - (pos.x - center.x);
	
		if (rot % 2)
			rot = (rot + 2) & 3;
	
		mirror = !mirror;
	}
	void rotate_around (float2 center, short ang) {
		pos = (ROT[ang & 3] * (pos - center)) + center;
	
		rot = (rot + ang) & 3;
	}
};
	
////
struct WireNode {
	float2 pos;
	vector_set<WireNode*> edges = {}; // TODO: small vec opt to 4 entries
	
	AABB get_aabb () const {
		return AABB{ (float2)pos - wire_radius*wire_node_junction,
			         (float2)pos + wire_radius*wire_node_junction };
	}
};
struct WireEdge {
	WireNode* a;
	WireNode* b;
};

struct Part;

// A chip design that can be edited or simulated if viewed as the "global" chip
// Uses other chips as parts, which are instanced into it's own editing or simulation
// (but cannot use itself as part because this would cause infinite recursion)
struct Chip {
	std::string name = "";
	lrgb        col = lrgb(1);
		
	float2 size = float2(10, 6);

	struct Pin {
		std::string name;

		float2    pos;
		int       rot;
		float     len;
	};
	std::vector<Pin> pins;
	
	Chip () {
		
	}
	Chip (std::string name, lrgb col, float2 size, std::vector<Pin> pins): name{name}, col{col}, size{size}, pins{pins} {
		
	}

	vector_set< std::unique_ptr<Part> > parts = {};

	vector_set< std::unique_ptr<WireNode> > wire_nodes = {};
	vector_set< std::unique_ptr<WireEdge> > wire_edges = {};

	vector_set<Chip*> users;

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
		
	Part (Chip* chip, std::string&& name, Placement pos): chip{chip}, pos{pos}, name{name} {
		
	}
		
	// cannot more or copy part because pointer needs to be stable for wire nodes
	Part (Part const& p) = delete;
	Part& operator= (Part const& p) = delete;
	Part (Part&& p) = delete;
	Part& operator= (Part&& p) = delete;

	AABB get_aabb () const {
		float2 size = abs(ROT[pos.rot] * chip->size);

		return AABB{ (float2)pos.pos - size*0.5f,
			         (float2)pos.pos + size*0.5f };
	}
};

using _partptr_equal = _equal< std::unique_ptr<Part> >;

enum ThingType {
	T_NONE=0,
	T_PART,
	T_NODE,
	T_WIRE,
};
struct ThingPtr {
	ThingType type;
	union {
		void*      _ptr;
		Part*      part;
		WireNode*  node;
		WireEdge*  wire;
	};

	ThingPtr ()                                   : type{T_NONE}, part{nullptr} {}
	ThingPtr (Part* part)                         : type{T_PART}, part{part} {
		assert(part != nullptr);
	}
	ThingPtr (WireNode* node)                     : type{T_NODE}, node{node} {
		assert(node != nullptr);
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

////
// (GateType % 10) > 5: inverting gate  (for shader)
enum GateType {
	BUF_GATE  = 0,
	NOT_GATE  = 5,

	AND_GATE  =10,
	AND3_GATE =11,
	AND4_GATE =12,
	NAND_GATE =15,
	NAND3_GATE=16,
	NAND4_GATE=17,

	OR_GATE   =20,
	OR3_GATE  =21,
	OR4_GATE  =22,
	NOR_GATE  =25,
	NOR3_GATE =26,
	NOR4_GATE =27,

	XOR_GATE  =30,

	MUX_GATE  =40,
	DMUX_GATE =41,
	MUX4_GATE =42,
	DMUX4_GATE=43,

	GATE_COUNT=1000,
};


#define _INP2 {"A", float2(-4, +2), 0, 2}, {"B", float2(-4, -2), 0, 2}
#define _INP3 {"A", float2(-4, +2), 0, 2}, {"B", float2(-4, 0), 0, 2}, {"C", float2(-4, -2), 0, 2}
#define _INP4 {"A", float2(-4, +3), 0, 2}, {"B", float2(-4, +1), 0, 2}, {"C", float2(-4, -1), 0, 2}, {"D", float2(-4, -3), 0, 2}

#define _INP2_MUX {"A", float2(-4, +1), 0, 2}, {"B", float2(-4, -1), 0, 2}

#define _OUT2_DMUX {"A", float2(+4, +1), 2, 2}, {"B", float2(+4, -1), 2, 2}
#define _OUT4 {"A", float2(+4, +3), 2, 2}, {"B", float2(+4, +1), 2, 2}, {"C", float2(+4, -1), 2, 2}, {"D", float2(+4, -3), 2, 2}

// Not const because we use Chip* for both editable (user-defined) chips and primitve gates
// alternatively just cast const away?
inline Chip gate_chips[GATE_COUNT] = {
	Chip("Buffer Gate", lrgb(0.5f, 0.5f,0.75f), float2(8,4), {{"Out", float2(4, 0), 2, 4}, {"In", float2(-4, 0), 0, 2}} ),
	{},{},{},{},
	Chip("NOT Gate",    lrgb(   0,    0,    1), float2(8,4), {{"Out", float2(4, 0), 2, 1}, {"In", float2(-4, 0), 0, 2}} ),
	{},{},{},{},

	Chip("AND Gate",    lrgb(   1,    0,    0), float2(8, 8), {{"Out", float2(4, 0), 2, 2}, _INP2} ),
	Chip("AND-3 Gate",  lrgb(   1,    0,    0), float2(8, 8), {{"Out", float2(4, 0), 2, 2}, _INP3} ),
	Chip("AND-4 Gate",  lrgb(   1,    0,    0), float2(8,10), {{"Out", float2(4, 0), 2, 2}, _INP4} ),
	{},{},
	Chip("NAND Gate",   lrgb(0.5f,    1,    0), float2(8, 8), {{"Out", float2(4, 0), 2, 1}, _INP2} ),
	Chip("NAND-3 Gate", lrgb(0.5f,    1,    0), float2(8, 8), {{"Out", float2(4, 0), 2, 1}, _INP3} ),
	Chip("NAND-4 Gate", lrgb(0.5f,    1,    0), float2(8,10), {{"Out", float2(4, 0), 2, 1}, _INP4} ),
	{},{},

	Chip("OR Gate",     lrgb(   1, 0.5f,    0), float2(8, 8), {{"Out", float2(4, 0), 2, 2}, _INP2} ),
	Chip("OR-3 Gate",   lrgb(   1, 0.5f,    0), float2(8, 8), {{"Out", float2(4, 0), 2, 2}, _INP3} ),
	Chip("OR-4 Gate",   lrgb(   1, 0.5f,    0), float2(8,10), {{"Out", float2(4, 0), 2, 2}, _INP4} ),
	{},{},
	Chip("NOR Gate",    lrgb(   0,    1, 0.5f), float2(8, 8), {{"Out", float2(4, 0), 2, 1}, _INP2} ),
	Chip("NOR-3 Gate",  lrgb(   0,    1, 0.5f), float2(8, 8), {{"Out", float2(4, 0), 2, 1}, _INP3} ),
	Chip("NOR-4 Gate",  lrgb(   0,    1, 0.5f), float2(8,10), {{"Out", float2(4, 0), 2, 1}, _INP4} ),
	{},{},
	
	Chip("XOR Gate",    lrgb(   0,    1,    0), float2(8, 8), {{"Out", float2(4, 0), 2, 2}, _INP2} ),
	{},{},{},{},
	{},{},{},{},{},

	Chip("Mux",         lrgb(   1,    1,    0), float2(8, 6), {{"Out", float2(4, 0), 2, 2}, _INP2_MUX, {"Sel", float2(0, -3), 3, 2}} ),
	Chip("Dmux",        lrgb(   1,    1, 0.5f), float2(8, 6), {_OUT2_DMUX, {"In", float2(-4, 0), 0, 2}, {"Sel", float2(0, -3), 3, 2}} ),
	Chip("Mux-4",       lrgb(   1,    1,    0), float2(8,10), {{"Out", float2(4, 0), 2, 2}, _INP4, {"Sel0", float2(-1, -5), 3, 2}, {"Sel1", float2(+1, -5), 3, 2}} ),
	Chip("Dmux-4",      lrgb(   1,    1, 0.5f), float2(8,10), {_OUT4, {"In", float2(-4, 0), 0, 2}, {"Sel0", float2(-1, -5), 3, 2}, {"Sel1", float2(+1, -5), 3, 2}} ),
	{},{},{},{},{},{},
};

inline bool is_gate (Chip* chip) {
	return chip >= gate_chips && chip < &gate_chips[GATE_COUNT];
}
inline GateType gate_type (Chip* chip) {
	assert(is_gate(chip));
	return (GateType)(chip - gate_chips);
}

struct Circuit {
	
	struct Gate {
		GateType type;
		int pins[10] = { -1, -1, -1, -1, -1, -1, -1 }; // outputs, inputs
	};
	std::vector<Gate> gates;

	int cur_state = 0;

	struct State {
		std::vector<uint8_t> state;
	};
	State states[2];

	// needed for meshing
	// TODO: if remeshing is always triggered on circuit rebuild this could be simply passed instead
	struct NodeMapEntry {
		int state_id = -1;
		int num_wires = 0;
	};
	std::unordered_map<int2, NodeMapEntry> node_map;
	
	// for rendering and toggling purposes of gates
	// get output pin's state id for normal gates
	// for DMUX gates gets the active output pin via prev state
	NodeMapEntry const& get_gate_state_id (Chip* gate, float2x3 const& chip2world) {
		assert(is_gate(gate) && gate->pins.size() > 0);
		
		auto& prev_state = states[cur_state^1];

		// Complicated logic to make specifically the active output of dmux gates toggleable
		// If this creates problems just don't make them toggleable and use the input of them for rendering
		int output_pin = 0;
		switch (gate_type(gate)) {
		case DMUX_GATE: {
			auto sel_state_id = node_map[roundi( chip2world * gate->pins[3].pos )].state_id;
			assert(sel_state_id >= 0);
			bool sel_state = prev_state.state[sel_state_id];

			output_pin = (int)sel_state;
		} break;
		case DMUX4_GATE: {
			auto sel0_state_id = node_map[roundi( chip2world * gate->pins[5].pos )].state_id;
			auto sel1_state_id = node_map[roundi( chip2world * gate->pins[6].pos )].state_id;
			assert(sel0_state_id >= 0 && sel1_state_id >= 0);
			bool sel0_state = prev_state.state[sel0_state_id];
			bool sel1_state = prev_state.state[sel1_state_id];

			output_pin = ((int)sel1_state<<1) | (int)sel0_state;
		} break;
		}

		auto& res = node_map[roundi( chip2world * gate->pins[output_pin].pos )];
		assert(res.state_id >= 0);
		return res;
	}

	void simulate ();
};

////
struct LogicSim {
	
	// (de)serialize all saved chips
	// simulation state is never (de)serialized
	// editor state is never (de)serialized
	friend void to_json (json& j, const LogicSim& sim);
	friend void from_json (const json& j, LogicSim& sim);


	std::vector<std::shared_ptr<Chip>> saved_chips;

	std::shared_ptr<Chip> viewed_chip;
	
	Circuit circuit;

	bool unsaved_changes = false;

	bool recompute = true;
	
	int indexof_chip (Chip* chip) const {
		int idx = indexof(saved_chips, chip, [] (std::shared_ptr<Chip> const& l, Chip* r) { return l.get() == r; });
		return idx;
	}

	void recreate_simulator ();

	void recompute_chip_users ();

	void switch_to_chip_view (std::shared_ptr<Chip> chip) {
		// TODO: delete chip warning if main_chip will be deleted by this?
		viewed_chip = std::move(chip); // move copy of shared ptr (ie original still exists)

		recompute_chip_users();
		recompute = true;

		//viewed_chip->state_count = -1; // TODO: needed?
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

		//ImGui::Text("Gates (# of states): %d", (int)state[0].size());

		if (ImGui::TreeNode("Visuals")) {
			ImGui::DragFloat("wire_radius", &wire_radius, 0.0001f);
			ImGui::DragFloat("wire_node_junction", &wire_node_junction, 0.01f);
			ImGui::DragFloat("wire_node_dead_end", &wire_node_dead_end, 0.01f);
			ImGui::TreePop();
		}
	}
		
	// delete chip from saved_chips, and reset viewed chip such that chip will actually be deleted
	// this is probably the least confusing option for the user
	void delete_chip (Chip* chip, Camera2D& cam) {
		assert(chip->users.empty()); // hopefully users is correct or we will crash
			
		int idx = indexof_chip(chip);
		if (idx >= 0)
			saved_chips.erase(saved_chips.begin() + idx);
			
		if (viewed_chip.get() == chip)
			reset_chip_view(cam);

		unsaved_changes = true;
		//recompute = true;
	}
	
	void update_chip_state ();

	void add_part (Chip& chip, Chip* part_chip, Placement part_pos) {
		assert(&chip == viewed_chip.get());

		auto* ptr = new Part(part_chip, "", part_pos);

		// insert part at end of parts list
		chip.parts.add(ptr);
	
		unsaved_changes = true;
		recompute = true;
	}
	void remove_part (Chip& chip, Part* part) {
		assert(&chip == viewed_chip.get());

		chip.parts.try_remove(part);
	
		unsaved_changes = true;
		recompute = true;
	}
		
	WireNode* add_wire_node (Chip& chip, float2 pos) {
		auto ptr = new WireNode { pos };
		chip.wire_nodes.add(std::unique_ptr<WireNode>(ptr));
		
		unsaved_changes = true;
		recompute = true;
		return ptr;
	}
	void disconnect_wire_node (Chip& chip, WireNode* node) {
		int count = node->edges.size();

		for (WireNode* n : node->edges) {
			assert(n != node); // if this happens the loop is invalid
			n->edges.remove(node);
		}

		int removed_edges = chip.wire_edges.remove_if(
			[&] (std::unique_ptr<WireEdge>& edge) {
				return edge->a == node || edge->b == node;
			});
		assert(count == removed_edges);
		
		unsaved_changes = true;
		recompute = true;
	}
	void remove_wire_node (Chip& chip, WireNode* node) {
		disconnect_wire_node(chip, node);

		bool removed = chip.wire_nodes.try_remove(node);
		assert(removed);
		
		unsaved_changes = true;
		recompute = true;
	}
	// existing connections are allowed as inputs but will be ignored
	void connect_wire_nodes (Chip& chip, WireNode* a, WireNode* b) {
		assert(a != b);

		bool existing = a->edges.contains(b);
		assert(existing == b->edges.contains(a));
		if (existing) return;

		a->edges.add(b);
		b->edges.add(a);

		chip.wire_edges.add(std::make_unique<WireEdge>(a, b));
		
		unsaved_changes = true;
		recompute = true;
	}
	void remove_wire_edge (Chip& chip, WireEdge* edge) {
			
		edge->a->edges.remove(edge->b);
		edge->b->edges.remove(edge->a);

		chip.wire_edges.remove(edge);

		unsaved_changes = true;
		recompute = true;
	}
};
	
} // namespace logic_sim
