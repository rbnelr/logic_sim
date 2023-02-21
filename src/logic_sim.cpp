#include "common.hpp"
#include "logic_sim.hpp"
#include "app.hpp"
#include "opengl/renderer.hpp"

namespace logic_sim {
	
////

struct BuildSim {
	Circuit& circuit;
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

	static int2 snap (float2 pos) {
		return roundi(pos);
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
		
		for (auto& n : chip->wire_nodes) {
			touch_node(chip2world * n->pos);
		}

		if (is_gate(chip)) {
			
			auto& sim_gate = circuit.gates.emplace_back(gate_type(chip));

			int i = 0;
			for (auto& pin : chip->pins) {
				int a = touch_node(chip2world * pin.pos);

				nodes[a].edges.push_back(-1); // dummy id to mark connection to gate

				if (i < ARRLEN(sim_gate.pins))
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
		ZoneScoped;

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
		
		for (auto& gate : circuit.gates) {
			for (int i=0; i<ARRLEN(gate.pins); ++i) {
				auto& id = gate.pins[i];
				id = id >= 0 ? wire_ids[ id ] : -1;
			}
		}

		for (auto& state_vec : circuit.states) {
			state_vec.gate_state.assign(circuit.gates.size(), 0);
			state_vec.wire_state.assign(num_wire_ids, 0);
		}
	}

	void draw_chip (Chip* chip, float2x3 const& chip2world, int& i) {
		
		lrgba lcol = line_col * lrgba(1);

		if (is_gate(chip)) {
			
			mesh_build.draw_gate(chip2world, chip->size, gate_type(chip), i++, lrgba(chip->col, 1) * lrgba(1));
			
			for (auto& pin : chip->pins) {
				float2 a = chip2world * pin.pos;
				float2 b = chip2world * (pin.pos + ROT[pin.rot] * float2(pin.len, 0));
				
				// TODO: improve?
				int wire_id = wire_ids[ get_node(a) ];

				mesh_build.draw_wire_segment(a, b, wire_id, lcol);
			}
		}
		else {
			//{ // TODO: make this look nicer, rounded thick outline? color the background inside chip differently?
			//	float2 center = chip2world * float2(0);
			//	float2 size = abs( (float2x2)chip2world * chip->size ) - 1.0f/16;
			//	r.dbgdraw.wire_quad(float3(center - size*0.5f, 0.0f), size, lrgba(0.001f, 0.001f, 0.001f, 1));
			//}
		}

		for (auto& part : chip->parts) {
			auto part2world = chip2world * part->pos.calc_matrix();

			draw_chip(part->chip, part2world, i);
		}
	}

	void draw (Chip* viewed_chip) {
		ZoneScoped;
		
		lrgba lcol = line_col * lrgba(1);

		mesh_build.line_groups.resize( num_wire_ids );
		
		for (auto& e : edges) {
			float2 a = (float2)nodes[e.a].pos;
			float2 b = (float2)nodes[e.b].pos;
			
			int wire_id = wire_ids[ get_node(a) ];

			mesh_build.draw_wire_segment(a, b, wire_id, lcol);
		}

		int i = 0;
		draw_chip(viewed_chip, float2x3::identity(), i);

		for (int i=0; i<(int)nodes.size(); ++i) {
			auto& n = nodes[i];

			float2 pos = (float2)n.pos;
			int wire_id = wire_ids[i];
			
			mesh_build.draw_wire_point(pos, wire_radius, (int)n.edges.size(), wire_id, lcol);
		}
		
		mesh_build.finish_wires();
	}
};

void LogicSim::recreate_simulator () {
	ZoneScoped;
	
	circuit = {};

	BuildSim build{circuit, {circuit.mesh}};

	{
		ZoneScopedN("flatten_graph");
		build.flatten_graph(viewed_chip.get(), float2x3::identity());

		char buf[128];
		int size = snprintf(buf, 128, "sim_gates: %d  node: %d  edges: %d",
			(int)circuit.gates.size(),
			(int)build.nodes.size(),
			(int)build.edges.size());
		if (size < 128) {
			ZoneText(buf, size);
		}
	}
	build.find_graphs();
	build.draw(viewed_chip.get());
}

void Circuit::simulate () {
	ZoneScoped;

	auto& cur  = states[cur_state  ];
	auto& next = states[cur_state^1];

	for (int i=0; i<(int)gates.size(); ++i) {
		auto& gate = gates[i];

	#if 1
		int inputs = (int)gate_chips[gate.type].pins.size() - 1;
	
		assert(inputs >= 1 && inputs <= 4);
		
		if (inputs >= 1) assert(gate.pins[0] >= 0 && gate.pins[0] < (int)cur.wire_state.size());
		if (inputs >= 2) assert(gate.pins[1] >= 0 && gate.pins[1] < (int)cur.wire_state.size());
		if (inputs >= 3) assert(gate.pins[2] >= 0 && gate.pins[2] < (int)cur.wire_state.size());
		if (inputs >= 4) assert(gate.pins[3] >= 0 && gate.pins[3] < (int)cur.wire_state.size());

		int in_a = gate.pins[0];
		int in_b = gate.pins[1];
		int in_c = gate.pins[2];
		int in_d = gate.pins[3];

		int out  = gate.pins[inputs];
		assert(out >= 0 && out < (int)cur.wire_state.size());
		
		bool a = in_a >= 0 ? (bool)cur.wire_state[in_a] : false;
		bool b = in_b >= 0 ? (bool)cur.wire_state[in_b] : false;
		bool c = in_c >= 0 ? (bool)cur.wire_state[in_c] : false;
		bool d = in_d >= 0 ? (bool)cur.wire_state[in_d] : false;
		
		bool new_state;
		switch (gate.type) {
			case BUF_GATE : new_state =  a;     break;
			case NOT_GATE : new_state = !a;     break;
					
			case AND_GATE : new_state =   a && b;    break;
			case NAND_GATE: new_state = !(a && b);   break;
					
			case OR_GATE  : new_state =   a || b;    break;
			case NOR_GATE : new_state = !(a || b);   break;
					
			case XOR_GATE : new_state =   a != b;    break;
	
			case AND3_GATE : new_state =   a && b && c;    break;
			case NAND3_GATE: new_state = !(a && b && c);   break;
					
			case OR3_GATE  : new_state =   a || b || c;    break;
			case NOR3_GATE : new_state = !(a || b || c);   break;

			case AND4_GATE : new_state =   a && b && c && d;    break;
			case NAND4_GATE: new_state = !(a && b && c && d);   break;
					
			case OR4_GATE  : new_state =   a || b || c || d;    break;
			case NOR4_GATE : new_state = !(a || b || c || d);   break;
	
			default: assert(false);
		}
		
		next.gate_state[i]   = (uint8_t)new_state;
		next.wire_state[out] = (uint8_t)new_state;
	#elif 0
		int inputs = (int)gate_chips[gate.type].pins.size() - 1;
	
		assert(inputs >= 1 && inputs <= 4);
		
		if (inputs >= 1) assert(gate.pins[0] >= 0 && gate.pins[0] < (int)cur.wire_state.size());
		if (inputs >= 2) assert(gate.pins[1] >= 0 && gate.pins[1] < (int)cur.wire_state.size());
		if (inputs >= 3) assert(gate.pins[2] >= 0 && gate.pins[2] < (int)cur.wire_state.size());
		if (inputs >= 4) assert(gate.pins[3] >= 0 && gate.pins[3] < (int)cur.wire_state.size());

		int in_a = gate.pins[0];
		int in_b = gate.pins[1];
		int in_c = gate.pins[2];
		int in_d = gate.pins[3];

		int out  = gate.pins[inputs];
		assert(out >= 0 && out < (int)cur.wire_state.size());
		
		uint8_t a = in_a >= 0 ? cur.wire_state[in_a] : 0;
		uint8_t b = in_b >= 0 ? cur.wire_state[in_b] : 0;
		uint8_t c = in_c >= 0 ? cur.wire_state[in_c] : 0;
		uint8_t d = in_d >= 0 ? cur.wire_state[in_d] : 0;
		
		uint8_t res = a;
		switch (gate.type) {

			case AND4_GATE : case NAND4_GATE: res &= d; // fallthrough
			case AND3_GATE : case NAND3_GATE: res &= c; // fallthrough
			case AND_GATE  : case NAND_GATE:  res &= b; break;

			case OR4_GATE  : case NOR4_GATE : res |= d; // fallthrough
			case OR3_GATE  : case NOR3_GATE : res |= c; // fallthrough
			case OR_GATE   : case NOR_GATE :  res |= b; break;

			case XOR_GATE  :                  res ^= b; break;
			
			default: break;
		}

		switch (gate.type) {
			case NOT_GATE  :
			case NAND_GATE :
			case NOR_GATE  :
			case NAND3_GATE:
			case NOR3_GATE :
			case NAND4_GATE:
			case NOR4_GATE :
				res ^= 1;
				break;
	
			default:
				break;
		}
		
		next.gate_state[i]   = res;
		next.wire_state[out] = res;
	#else
		int inputs = (int)gate_chips[gate.type].pins.size() - 1;
		
		if (inputs >= 1) assert(gate.pins[0] >= 0 && gate.pins[0] < (int)cur.wire_state.size());
		if (inputs >= 2) assert(gate.pins[1] >= 0 && gate.pins[1] < (int)cur.wire_state.size());
		if (inputs >= 3) assert(gate.pins[2] >= 0 && gate.pins[2] < (int)cur.wire_state.size());
		if (inputs >= 4) assert(gate.pins[3] >= 0 && gate.pins[3] < (int)cur.wire_state.size());

		uint8_t a = cur.wire_state[ gate.pins[0] ];

		switch (gate.type) {
			case BUF_GATE  : case NOT_GATE  : {
				assert(inputs == 1);

				int out  = gate.pins[1];
				assert(out >= 0 && out < (int)cur.wire_state.size());
		
				uint8_t res = gate.type == NOT_GATE ? a^1 : a;

				next.gate_state[i]   = res;
				next.wire_state[out] = res;

			} break;
			
			case AND_GATE  : case NAND_GATE: 
			case OR_GATE   : case NOR_GATE : 
			case XOR_GATE  : {
				assert(inputs == 2);

				int out  = gate.pins[2];
				assert(out >= 0 && out < (int)cur.wire_state.size());
				
				uint8_t b = cur.wire_state[ gate.pins[1] ];
		
				uint8_t res = a;
				switch (gate.type) {
					case AND_GATE  : case NAND_GATE:  res &= b; break;
					case OR_GATE   : case NOR_GATE :  res |= b; break;
					case XOR_GATE  :                  res ^= b; break;
					INVALID_DEFAULT;
				}

				switch (gate.type) {
					case NAND_GATE :
					case NOR_GATE  :
						res ^= 1;
						break;
					default:
						break;
				}
		
				next.gate_state[i]   = res;
				next.wire_state[out] = res;

			} break;
				
			case AND3_GATE : case NAND3_GATE:
			case OR3_GATE  : case NOR3_GATE : {
				assert(inputs == 3);

				int out  = gate.pins[3];
				assert(out >= 0 && out < (int)cur.wire_state.size());
				
				uint8_t b = cur.wire_state[ gate.pins[1] ];
				uint8_t c = cur.wire_state[ gate.pins[2] ];
		
				uint8_t res = a;
				switch (gate.type) {
					case AND3_GATE : case NAND3_GATE: res = res & b & c; break;
					case OR3_GATE  : case NOR3_GATE : res = res | b | c; break;
					INVALID_DEFAULT;
				}

				switch (gate.type) {
					case NAND3_GATE:
					case NOR3_GATE :
						res ^= 1;
						break;
					INVALID_DEFAULT;
				}
		
				next.gate_state[i]   = res;
				next.wire_state[out] = res;

			} break;

			case AND4_GATE : case NAND4_GATE:
			case OR4_GATE  : case NOR4_GATE : {
				assert(inputs == 4);

				int out  = gate.pins[4];
				assert(out >= 0 && out < (int)cur.wire_state.size());
				
				uint8_t b = cur.wire_state[ gate.pins[1] ];
				uint8_t c = cur.wire_state[ gate.pins[2] ];
				uint8_t d = cur.wire_state[ gate.pins[3] ];
		
				uint8_t res = a;
				switch (gate.type) {
					case AND4_GATE : case NAND4_GATE: res = res & b & c & d; break;
					case OR4_GATE  : case NOR4_GATE : res = res | b | c | d; break;
					INVALID_DEFAULT;
				}

				switch (gate.type) {
					case NAND4_GATE:
					case NOR4_GATE :
						res ^= 1;
						break;
					INVALID_DEFAULT;
				}
		
				next.gate_state[i]   = res;
				next.wire_state[out] = res;

			} break;

			INVALID_DEFAULT;
		}

	#endif
	}
	
	cur_state ^= 1;


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
			part_chip = &gate_chips[chip_id];
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

	sim.recompute = true;
}

} // namespace logic_sim
