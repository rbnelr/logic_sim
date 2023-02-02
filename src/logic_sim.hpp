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

inline float wire_radius = 0.04f;
inline float wire_node_junction = 2.0f;
inline float wire_node_dead_end = 1.3f;

inline constexpr float PIN_LENGTH = 0.5f; // IO Pin base wire length

constexpr lrgba line_col = lrgba(0.8f, 0.01f, 0.025f, 1);
constexpr lrgba preview_line_col = line_col * lrgba(1,1,1, 0.75f);

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
	
	//bool visited = false;
	int  local_id = -1;

	int num_wires () {
		return edges.size() + (parent_part ? 1 : 0);
	}

	AABB get_aabb () const {
		return AABB{ pos - wire_radius*wire_node_junction,
			         pos + wire_radius*wire_node_junction };
	}
};
struct WireEdge {
	WireNode* a;
	WireNode* b;
};

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
		// mirror does not matter
		float2 size = chip->size * pos.scale;

		// Does not handle non-90 deg rotations
		size = abs(ROT[pos.rot] * size);

		return AABB{ pos.pos - size*0.5f,
			         pos.pos + size*0.5f };
	}
};

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
enum GateType {
	BUF_GATE  =0,
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

// Not const because we use Chip* for both editable (user-defined) chips and primitve gates
// alternatively just cast const away?
inline Chip gates[GATE_COUNT] = {
	Chip("Buffer Gate", lrgb(0.5f, 0.5f,0.75f), float2(1,0.5f), {{"In", float2(-0.5f, 0), 0, 0.25f}, {"Out", float2(0.5f, 0), 2, 0.5f}} ),
	Chip("NOT Gate",    lrgb(   0,    0,    1), float2(1,0.5f), {{"In", float2(-0.5f, 0), 0, 0.25f}, {"Out", float2(0.5f, 0), 2, 0.125f}} ),
	Chip("AND Gate",    lrgb(   1,    0,    0), float2(1,   1), {{"A", float2(-0.5f, +0.25f), 0, 0.25f}, {"B", float2(-0.5f, -0.25f), 0, 0.25f}, {"Out", float2(0.5f, 0), 2, 0.25f}} ),
	Chip("NAND Gate",   lrgb(0.5f,    1,    0), float2(1,   1), {{"A", float2(-0.5f, +0.25f), 0, 0.25f}, {"B", float2(-0.5f, -0.25f), 0, 0.25f}, {"Out", float2(0.5f, 0), 2, 0.125f}} ),
	Chip("OR Gate",     lrgb(   1, 0.5f,    0), float2(1,   1), {{"A", float2(-0.5f, +0.25f), 0, 0.25f}, {"B", float2(-0.5f, -0.25f), 0, 0.25f}, {"Out", float2(0.5f, 0), 2, 0.25f}} ),
	Chip("NOR Gate",    lrgb(   0,    1, 0.5f), float2(1,   1), {{"A", float2(-0.5f, +0.25f), 0, 0.25f}, {"B", float2(-0.5f, -0.25f), 0, 0.25f}, {"Out", float2(0.5f, 0), 2, 0.125f}} ),
	Chip("XOR Gate",    lrgb(   0,    1,    0), float2(1,   1), {{"A", float2(-0.5f, +0.25f), 0, 0.25f}, {"B", float2(-0.5f, -0.25f), 0, 0.25f}, {"Out", float2(0.5f, 0), 2, 0.25f}} ),
	
	Chip("AND-3 Gate",  lrgb(   1,    0,    0), float2(1,   1), {{"A", float2(-0.5f, +0.25f), 0, 0.25f}, {"B", float2(-0.5f, 0), 0, 0.25f}, {"C", float2(-0.5f, -0.25f), 0, 0.25f}, {"Out", float2(0.5f, 0), 2, 0.25f}} ),
	Chip("NAND-3 Gate", lrgb(0.5f,    1,    0), float2(1,   1), {{"A", float2(-0.5f, +0.25f), 0, 0.25f}, {"B", float2(-0.5f, 0), 0, 0.25f}, {"C", float2(-0.5f, -0.25f), 0, 0.25f}, {"Out", float2(0.5f, 0), 2, 0.125f}} ),
	Chip("OR-3 Gate",   lrgb(   1, 0.5f,    0), float2(1,   1), {{"A", float2(-0.5f, +0.25f), 0, 0.25f}, {"B", float2(-0.5f, 0), 0, 0.25f}, {"C", float2(-0.5f, -0.25f), 0, 0.25f}, {"Out", float2(0.5f, 0), 2, 0.25f}} ),
	Chip("NOR-3 Gate",  lrgb(   0,    1, 0.5f), float2(1,   1), {{"A", float2(-0.5f, +0.25f), 0, 0.25f}, {"B", float2(-0.5f, 0), 0, 0.25f}, {"C", float2(-0.5f, -0.25f), 0, 0.25f}, {"Out", float2(0.5f, 0), 2, 0.125f}} ),
};
	
inline bool is_gate (Chip* chip) {
	return chip >= gates && chip < &gates[GATE_COUNT];
}
inline GateType gate_type (Chip* chip) {
	assert(is_gate(chip));
	return (GateType)(chip - gates);
}

struct Circuit {
	
	struct Gate {
		GateType type;
		int pins[4] = { -1, -1, -1, -1 };
	};
	std::vector<Gate> gates;

	int cur_state = 0;

	std::vector<uint8_t> state[2];
};

////
struct LogicSim {
		
	// (de)serialize a chip to json, translating between gate and custom chip pointers and a single integer id
	friend void to_json (json& j, const Chip& chip, const LogicSim& sim);
	friend void from_json (const json& j, Chip& chip, LogicSim& sim);

	// (de)serialize all saved chips
	// simulation state is never (de)serialized
	// editor state is never (de)serialized
	friend void to_json (json& j, const LogicSim& sim);
	friend void from_json (const json& j, LogicSim& sim);


	std::vector<std::shared_ptr<Chip>> saved_chips;

	std::shared_ptr<Chip> viewed_chip;
	
	Circuit circuit;

	bool unsaved_changes = false;
	
	int indexof_chip (Chip* chip) const {
		int idx = indexof(saved_chips, chip, [] (std::shared_ptr<Chip> const& l, Chip* r) { return l.get() == r; });
		return idx;
	}

	void recreate_simulator (ogl::Renderer& r);

	void recompute_chip_users ();

	void switch_to_chip_view (std::shared_ptr<Chip> chip) {
		// TODO: delete chip warning if main_chip will be deleted by this?
		viewed_chip = std::move(chip); // move copy of shared ptr (ie original still exists)

		recompute_chip_users();

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
		if (ImGui::TreeNode("settings")) {
			ImGui::DragFloat("wire_radius", &wire_radius, 0.0001f);
			ImGui::DragFloat("wire_node_junction", &wire_node_junction, 0.01f);
			ImGui::DragFloat("wire_node_dead_end", &wire_node_dead_end, 0.01f);
			ImGui::TreePop();
		}

		if (unsaved_changes) {
			ImGui::TextColored(ImVec4(1.00f, 0.67f, 0.00f, 1), "Unsaved changes");
		}
		else {
			ImGui::Text("No unsaved changes");
		}

		//ImGui::Text("Gates (# of states): %d", (int)state[0].size());
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
	}
	
	void update_chip_state ();

	void add_part (Chip& chip, Chip* part_chip, Placement part_pos) {
		assert(&chip == viewed_chip.get());

		auto* ptr = new Part(part_chip, "", part_pos);

		// insert part at end of parts list
		chip.parts.add(ptr);
	
		unsaved_changes = true;
	}
	void remove_part (Chip& chip, Part* part) {
		assert(&chip == viewed_chip.get());

		chip.parts.try_remove(part);
	
		unsaved_changes = true;
	}
		
	WireNode* add_wire_node (Chip& chip, float2 pos) {
		auto ptr = new WireNode(pos);
		chip.wire_nodes.add(std::unique_ptr<WireNode>(ptr));
		
		unsaved_changes = true;
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
		
		unsaved_changes = true;
	}
	void remove_wire_node (Chip& chip, WireNode* node) {
		disconnect_wire_node(chip, node);

		bool removed = chip.wire_nodes.try_remove(node);
		assert(removed);
		
		unsaved_changes = true;
	}
	// existing connections are allowed as inputs but will be ignored
	void connect_wire_nodes (Chip& chip, WireNode* a, WireNode* b) {
		bool existing = a->edges.contains(b);
		assert(existing == b->edges.contains(a));
		if (existing) return;

		a->edges.add(b);
		b->edges.add(a);

		chip.wire_edges.add(std::make_unique<WireEdge>(a, b));
		
		unsaved_changes = true;
	}
	void remove_wire_edge (Chip& chip, WireEdge* edge) {
			
		edge->a->edges.remove(edge->b);
		edge->b->edges.remove(edge->a);

		chip.wire_edges.remove(edge);

		unsaved_changes = true;
	}

	void simulate (Input& I);
};
	
} // namespace logic_sim
