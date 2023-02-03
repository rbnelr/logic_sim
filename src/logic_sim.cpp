#include "common.hpp"
#include "logic_sim.hpp"
#include "app.hpp"
#include "opengl/renderer.hpp"

namespace logic_sim {
	
////

struct BuildSim {
	Circuit& circuit;
	ogl::Renderer& r;
	ogl::CircuitMeshBuilder mesh_build;

	struct WireNode {
		int2             pos;
		bool             visited = false;
		std::vector<int> edges;
	};
	struct WireEdge {
		int              a, b;
	};

	// pos -> id map to merge wire edge end points into a graph
	std::unordered_map<int2, int> node_map;
	// flattened wire graph
	std::vector<WireNode>         nodes;
	std::vector<WireEdge>         edges;

	std::vector<int> wire_ids;
	int num_wire_ids;
	
	static constexpr float snapping_size = 0.125f;

	static int2 snap (float2 pos) {
		return roundi(pos / snapping_size);
	};
	int touch_node (float2 pos) {
		int2 p = snap(pos);

		auto res = node_map.try_emplace(p);
		if (res.second) {
			res.first->second = (int)nodes.size();
			nodes.emplace_back(p);
		}

		return res.first->second;
	}
	int get_node (float2 pos) {
		int2 p = snap(pos);

		auto res = node_map.find(p);
		if (res != node_map.end()) {
			return res->second;
		}

		assert(false);
		return 0;
	}
	
	void flatten_graph (Chip* chip, float2x3 const& chip2world) {
		if (is_gate(chip)) {
			
			auto& sim_gate = circuit.gates.emplace_back(gate_type(chip));

			int i = 0;
			for (auto& pin : chip->pins) {
				int a = touch_node(chip2world * pin.pos);

				nodes[a].edges.push_back(-1); // dummy id to mark connection to gate

				if (i < 4)
					sim_gate.pins[i++] = a;
			}
		}

		for (auto& e : chip->wire_edges) {
			int a = touch_node(chip2world * e->a->pos);
			int b = touch_node(chip2world * e->b->pos);
			
			edges.emplace_back(a, b);

			nodes[a].edges.push_back(b);
			nodes[b].edges.push_back(a);
		}

		for (auto& part : chip->parts) {
			auto part2world = chip2world * part->pos.calc_matrix();

			flatten_graph(part->chip, part2world);
		}
	}

	void find_graphs () {

		wire_ids.assign((int)nodes.size(), -1);

		int wid = 0;

		for (int i=0; i<(int)nodes.size(); ++i) {
			if (!nodes[i].visited) {
				int wire_id = wid++;
		
				// could use a queue as well, but order does not matter
				// TODO: profile difference
				std::vector<int> stk;
				stk.push_back(i);
		
				while (!stk.empty()) {
					int cur = stk.back();
					stk.pop_back();
				
					nodes[cur].visited = true;
					wire_ids[cur] = wire_id;
				
					for (int link : nodes[cur].edges) {
						if (link >= 0 && !nodes[link].visited)
							stk.push_back(link);
					}
				}
			}
		}

		num_wire_ids = wid;

		for (auto& state_vec : circuit.state)
			state_vec.assign(num_wire_ids, 0);
	}

	void draw_chip (Chip* chip, float2x3 const& chip2world) {
		
		lrgba lcol = line_col * lrgba(1);

		if (is_gate(chip)) {
			mesh_build.draw_gate(chip2world, chip->size, gate_type(chip), 0, lrgba(chip->col, 1) * lrgba(1));
			
			for (auto& pin : chip->pins) {
				float2 a = chip2world * pin.pos;
				float2 b = chip2world * (pin.pos + ROT[pin.rot] * float2(pin.len, 0));
				
				int wire_id = wire_ids[ get_node(a) ];

				mesh_build.draw_wire_segment(wire_id, a, b, 0, lcol);
			}
		}
		else {
			{ // TODO: make this look nicer, rounded thick outline? color the background inside chip differently?
				float2 center = chip2world * float2(0);
				float2 size = abs( (float2x2)chip2world * chip->size ) - 1.0f/16;
				r.dbgdraw.wire_quad(float3(center - size*0.5f, 0.0f), size, lrgba(0.001f, 0.001f, 0.001f, 1));
			}
		}

		for (auto& part : chip->parts) {
			auto part2world = chip2world * part->pos.calc_matrix();

			draw_chip(part->chip, part2world);
		}
	}

	void draw (Chip* viewed_chip) {
		lrgba lcol = line_col * lrgba(1);

		mesh_build.line_groups.resize( num_wire_ids );
		
		for (auto& e : edges) {
			float2 a = (float2)nodes[e.a].pos * snapping_size;
			float2 b = (float2)nodes[e.b].pos * snapping_size;
			
			int wire_id = wire_ids[ get_node(a) ];

			mesh_build.draw_wire_segment(wire_id, a, b, 0, lcol);
		}

		draw_chip(viewed_chip, float2x3::identity());

		for (int i=0; i<(int)nodes.size(); ++i) {
			auto& n = nodes[i];

			float2 pos = (float2)n.pos * snapping_size;
			int wire_id = wire_ids[i];
			
			r.draw_text(prints("%d", wire_id), pos, 10, 1);
			mesh_build.draw_wire_point(wire_id, pos, wire_radius, (int)n.edges.size(), 0, lcol);
		}
		
		mesh_build.finish_wires();
	}
};

void LogicSim::recreate_simulator (ogl::Renderer& r) {
	
	circuit = {};

	BuildSim build{circuit, r, {circuit.mesh}};

	build.flatten_graph(viewed_chip.get(), float2x3::identity());
	build.find_graphs();
	build.draw(viewed_chip.get());
}

void simulate_chip (Chip& chip, int state_base, uint8_t* cur, uint8_t* next) {
	
	//int parts_sid = state_base + chip.wire_states;
	//
	//auto get = [&] (Part::Pin& pin) -> uint8_t* {
	//	// Note: in this case sid is still valid, could just read it anyway
		// TODO: How should gate toggling work?
	//	if (pin.node->edges.empty())
	//		return nullptr;
	//	return &cur[state_base + pin.node->sid];
	//};
	//
	//for (auto& part : chip.outputs) {
	//	assert(part->chip == &gates[OUT_PIN]);
	//	assert(part->pins.size() == 2);
	//	
	//	uint8_t* pa = get(part->pins[0]);
	//
	//	uint8_t* res0 = &next[state_base + part->pins.back().node->sid];
	//
	//	if (!pa) {
	//		// keep prev state (needed to toggle gates via LMB)
	//		*res0 = cur[state_base + part->pins.back().node->sid];
	//	}
	//	else {
	//		*res0 = *pa;
	//	}
	//}
	//
	////// skip inputs which were updated by caller
	//sid += (int)chip.inputs.size();
	//
	//for (auto& part : chip.parts) {
	//	int input_count = (int)part->chip->inputs.size();
	//
	//	if (!is_gate(part->chip)) {
	//		int output_count = (int)part->chip->outputs.size();
	//
	//		for (int i=0; i<input_count; ++i) {
	//			Part* src = part->inputs[i].part;
	//				
	//			uint8_t new_state;
	//
	//			if (!src) {
	//				// keep prev state (needed to toggle gates via LMB)
	//				new_state = cur[sid + output_count + i] != 0;
	//			}
	//			else {
	//				// read input connection
	//				new_state = cur[state_base + src->sid + part->inputs[i].pin] != 0;
	//				// write input part state
	//			}
	//
	//			next[sid + output_count + i] = new_state;
	//		}
	//
	//		simulate_chip(*part->chip, sid, cur, next);
	//	}
	//	else {
	//		auto type = gate_type(part->chip);
	//
	//		assert(type != INP_PIN && type != OUT_PIN);
	//		
	//		assert(part->chip->state_count == 1);
	//		
	//		// TODO: cache part_idx in input as well to avoid indirection
	//		Part* src_a = input_count >= 1 ? part->inputs[0].part : nullptr;
	//		Part* src_b = input_count >= 2 ? part->inputs[1].part : nullptr;
	//		Part* src_c = input_count >= 3 ? part->inputs[2].part : nullptr;
	//
	//		if (!src_a && !src_b) {
	//			// keep prev state (needed to toggle gates via LMB)
	//			next[sid] = cur[sid];
	//		}
	//		else {
	//			bool a = src_a && cur[state_base + src_a->sid + part->inputs[0].pin] != 0;
	//			bool b = src_b && cur[state_base + src_b->sid + part->inputs[1].pin] != 0;
	//			bool c = src_c && cur[state_base + src_c->sid + part->inputs[2].pin] != 0;
	//				
	//			uint8_t new_state;
	//			switch (type) {
	//				case BUF_GATE : new_state =  a;     break;
	//				case NOT_GATE : new_state = !a;     break;
	//				
	//				case AND_GATE : new_state =   a && b;    break;
	//				case NAND_GATE: new_state = !(a && b);   break;
	//				
	//				case OR_GATE  : new_state =   a || b;    break;
	//				case NOR_GATE : new_state = !(a || b);   break;
	//				
	//				case XOR_GATE : new_state =   a != b;    break;
	//
	//				case AND3_GATE : new_state =   a && b && c;    break;
	//				case NAND3_GATE: new_state = !(a && b && c);   break;
	//				
	//				case OR3_GATE  : new_state =   a || b || c;    break;
	//				case NOR3_GATE : new_state = !(a || b || c);   break;
	//
	//				default: assert(false);
	//			}
	//
	//			next[sid] = new_state;
	//		}
	//
	//		// don't recurse
	//	}
	//
	//	sid += part->chip->state_count;
	//}
	//
	//for (auto& part : chip.parts) {
	//
	//	if (!is_gate(part->chip)) {
	//		simulate_chip(*part->chip, parts_sid, cur, next);
	//		parts_sid += part->chip->state_count;
	//		continue;
	//	}
	//
	//	auto type = gate_type(part->chip);
	//	
	//	if (type == INP_PIN || type == OUT_PIN)
	//		continue;
	//	
	//	int num_pins = (int)part->pins.size();
	//	assert(num_pins == (int)part->chip->inputs.size() + (int)part->chip->outputs.size());
	//	
	//	uint8_t* pa = num_pins >= 2 ? get(part->pins[0]) : nullptr;
	//	uint8_t* pb = num_pins >= 3 ? get(part->pins[1]) : nullptr;
	//	uint8_t* pc = num_pins >= 4 ? get(part->pins[2]) : nullptr;
	//
	//	uint8_t* res0 = &next[state_base + part->pins.back().node->sid];
	//	
	//	if (!pa && !pb && !pc) {
	//		// keep state if input is not connected
			// TODO: does this make sense? maybe rather just create a 'button' gate?
	//		*res0 = cur[state_base + part->pins.back().node->sid];
	//		continue;
	//	}
	//
	//	uint8_t a = pa ? *pa : 0;
	//	uint8_t b = pb ? *pb : 0;
	//	uint8_t c = pc ? *pc : 0;
	//	
	//	if (res0) {
	//		switch (type) {
	//			case BUF_GATE  : *res0 = (uint8_t)(  a );  break;
	//			case NOT_GATE  : *res0 = (uint8_t)( !a );  break;
	//		
	//			case AND_GATE  : *res0 = (uint8_t)(   a && b  ); break;
	//			case NAND_GATE : *res0 = (uint8_t)( !(a && b) ); break;
	//		
	//			case OR_GATE   : *res0 = (uint8_t)(   a || b  ); break;
	//			case NOR_GATE  : *res0 = (uint8_t)( !(a || b) ); break;
	//		
	//			case XOR_GATE  : *res0 = (uint8_t)(   a != b ); break;
	//		
	//			case AND3_GATE : *res0 = (uint8_t)(   a && b && c  ); break;
	//			case NAND3_GATE: *res0 = (uint8_t)( !(a && b && c) ); break;
	//		
	//			case OR3_GATE  : *res0 = (uint8_t)(   a || b || c  ); break;
	//			case NOR3_GATE : *res0 = (uint8_t)( !(a || b || c) ); break;
	//		
	//			default: assert(false);
	//		}
	//	}
	//}
	//
	//assert(chip.state_count >= 0); // state_count stale!
	//assert(parts_sid - state_base == chip.state_count); // state_count invalid!
}

void LogicSim::simulate (Input& I) {
	ZoneScoped;

	//uint8_t* cur  = state[cur_state  ].data();
	//uint8_t* next = state[cur_state^1].data();

	//for (int i=0; i<(int)viewed_chip->inputs.size(); ++i) {
	//	// keep prev state (needed to toggle gates via LMB)
	//	next[sid + output_count + i] = cur[sid + output_count + i] != 0;
	//}

	//simulate_chip(*viewed_chip, 0, cur, next);
	//
	//cur_state ^= 1;
}
	
Chip Chip::deep_copy () const {
	Chip c;
	c.name = name;
	c.col  = col;
	c.size = size;
	
	c.parts.reserve(parts  .size());

	for (auto& p : parts)
		c.parts.add( std::make_unique<Part>(p->chip, std::string(p->name), p->pos) );

	return c;
}

////
void _add_user_to_chip_deps (Chip* chip, Chip* user) {
	for (auto& p : chip->parts) {
		if (!is_gate(p->chip)) {
			// iterate unique recursive dependencies
			if (p->chip->users.try_add(user)) {
				_add_user_to_chip_deps(p->chip, user);
			}
		}
	}
}
void LogicSim::recompute_chip_users () {
	for (auto& chip : saved_chips) {
		chip->users.clear();
	}
	for (auto& chip : saved_chips) {
		_add_user_to_chip_deps(chip.get(), chip.get()); // add chip to all its recursive dependencies
	}
}

void LogicSim::update_chip_state () {
	unsaved_changes = true;

	recompute_chip_users();
	
	//update_all_chip_state_indices(*this);
	
	//// TODO
	//for (int i=0; i<2; ++i)
	//	state[i].assign(viewed_chip->state_count, 0);
}

////
json part2json (LogicSim const& sim, Part& part, std::unordered_map<Part*, int>& part2idx) {
	json j;
	j["chip"] = is_gate(part.chip) ? gate_type(part.chip) : sim.indexof_chip(part.chip) + GATE_COUNT;
	
	if (!part.name.empty())
		j["name"] = part.name;

	j["pos"] = part.pos;

	return j;
}
json chip2json (const Chip& chip, LogicSim const& sim) {
	std::unordered_map<Part*, int> part2idx;
	part2idx.reserve(chip.parts.size());
	
	std::unordered_map<WireNode*, int> node2idx;
	node2idx.reserve(chip.wire_nodes.size() + chip.parts.size()*4); // estimate

	int idx = 0;
	int node_idx = 0;

	for (auto& wire : chip.wire_nodes) {
		node2idx[wire.get()] = node_idx++;
	}

	for (auto& part : chip.parts) {
		part2idx[part.get()] = idx++;
	}

	json jparts = json::array();
	json jnodes = json::array();
	json jedges = json::array();
	
	// just serialize the free node positions
	for (auto& node : chip.wire_nodes) {
		jnodes.emplace_back(json{
			{"pos", node->pos}
		});
	}
	// instead of serializing the nodes with their connections
	// serialize just the edges, because it's simpler and avoids the doubly linked graph
	for (auto& edge : chip.wire_edges) {
		jedges.emplace_back(json{
			{"a", node2idx[edge->a]},
			{"b", node2idx[edge->b]},
		});
	}

	for (auto& part : chip.parts) {
		jparts.emplace_back( part2json(sim, *part, part2idx) );
	}
	
	json j = {
		{"name",       chip.name},
		{"col",        chip.col},
		{"size",       chip.size},
		{"parts",      std::move(jparts)},
		{"wire_nodes", std::move(jnodes)},
		{"wire_edges", std::move(jedges)},
	};
	return j;
}
void to_json (json& j, LogicSim const& sim) {

	j["viewed_chip"] = sim.indexof_chip(sim.viewed_chip.get());

	json& jchips = j["chips"];
	for (auto& chip : sim.saved_chips) {
		jchips.emplace_back( chip2json(*chip, sim) );
	}

	// HACK: const-cast, only way around this would be not using const in json lib
	//  or using global bool, which I want to avoid
	((LogicSim&)sim).unsaved_changes = false;
}

Part* json2part (const json& j, LogicSim const& sim, std::vector<WireNode*>& idx2node) {
	
	int chip_id      = j.at("chip");
	std::string name = j.contains("name") ? j.at("name") : "";
	Placement   pos  = j.at("pos");
		
	assert(chip_id >= 0);
	Chip* part_chip = nullptr;
	if (chip_id >= 0) {
		if (chip_id >= 0 && chip_id < GATE_COUNT)
			part_chip = &gates[chip_id];
		else if (chip_id >= 0)
			part_chip = sim.saved_chips[chip_id - GATE_COUNT].get();
	}

	auto* ptr = new Part(part_chip, std::move(name), pos);
	return ptr;
}
void json2chip (const json& j, Chip& chip, LogicSim& sim) {
	
	auto& jparts = j.at("parts");
	auto& jnodes = j.at("wire_nodes");
	auto& jedges = j.at("wire_edges");
	
	chip.parts.reserve((int)jparts.size());

	chip.wire_nodes.reserve((int)jnodes.size());
	chip.wire_edges.reserve((int)jedges.size());

	// first pass to create pointers
	std::vector<Part*> idx2part((int)jparts.size(), nullptr);
	
	std::vector<WireNode*> idx2node;
	idx2node.reserve(chip.wire_nodes.size() + chip.parts.size()*4);

	for (auto& j : jnodes) {
		float2 pos = j.at("pos");
		auto node = std::make_unique<WireNode>(pos, nullptr);
		idx2node.emplace_back(node.get());
		chip.wire_nodes.add(std::move(node));
	}

	int idx = 0;
	for (auto& j : jparts) {
		auto* ptr = idx2part[idx++] = json2part(j, sim, idx2node);
		chip.parts.add(ptr);
	}

	for (auto& j : jedges) {
		auto get = [&] (int idx) -> WireNode* {
			assert(idx >= 0 && idx < (int)idx2node.size());
			return idx2node[idx];
		};
		auto* a = get(j.at("a"));
		auto* b = get(j.at("b"));
		assert(a && b);

		auto edge = std::make_unique<WireEdge>(a, b);
	
		a->edges.add(b);
		b->edges.add(a);
		
		chip.wire_edges.add(std::move(edge));
	}
}
void from_json (const json& j, LogicSim& sim) {
	// create chips without parts
	for (auto& jchip : j["chips"]) {
		auto& chip = sim.saved_chips.emplace_back(std::make_unique<Chip>());
		
		chip->name    = jchip.at("name");
		chip->col     = jchip.at("col");
		chip->size    = jchip.at("size");
	}
	// then convert chip ids references in parts to valid chips
	auto cur = sim.saved_chips.begin();
	for (auto& jchip : j["chips"]) {
		json2chip(jchip, *(*cur++), sim);
	}

	sim.recompute_chip_users();

	int viewed_chip_idx = j["viewed_chip"];
	if (viewed_chip_idx >= 0) {
		sim.switch_to_chip_view( sim.saved_chips[viewed_chip_idx]);
	}
}

} // namespace logic_sim
