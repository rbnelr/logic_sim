#include "common.hpp"
#include "logic_sim.hpp"
#include "app.hpp"
#include "opengl/renderer.hpp"

namespace logic_sim {
	
////

struct BuildSim {
	Circuit& circuit;

	struct WireNode {
		int2             pos;
		bool             visited = false;
		std::vector<int> edges;
	};
	struct WireEdge {
		int              a, b;
	};

	// pos -> id map to merge wire edge end points into a graph
	std::unordered_map<int2, Circuit::NodeMapEntry> node_map;
	// flattened wire graph
	std::vector<WireNode>         flat_nodes;
	std::vector<WireEdge>         flat_edges;

	std::vector<int> wire_ids;
	int num_states;

	Circuit::NodeMapEntry& touch_node (float2 pos) {
		int2 p = roundi(pos);

		auto res = node_map.try_emplace(p);
		if (res.second) {
			res.first->second = {
				(int)flat_nodes.size(),
				0,
			};
			flat_nodes.emplace_back(p);
		}

		return res.first->second;
	}
	
	void flatten_graph (Chip* chip, float2x3 const& chip2world) {
		
		for (auto& n : chip->wire_nodes) {
			touch_node(chip2world * n->pos);
		}

		if (is_gate(chip)) {
			
			auto& sim_gate = circuit.gates.emplace_back(gate_type(chip));

			int i = 0;
			for (auto& pin : chip->pins) {
				auto& a = touch_node(chip2world * pin.pos);
				a.num_wires++;

				flat_nodes[a.state_id].edges.push_back(-1); // dummy id to mark connection to gate

				if (i < ARRLEN(sim_gate.pins))
					sim_gate.pins[i++] = a.state_id;
			}
		}

		for (auto& e : chip->wire_edges) {
			auto& a = touch_node(chip2world * e->a->pos);
			auto& b = touch_node(chip2world * e->b->pos);
			a.num_wires++;
			b.num_wires++;
			
			flat_edges.emplace_back(a.state_id, b.state_id);

			flat_nodes[a.state_id].edges.push_back(b.state_id);
			flat_nodes[b.state_id].edges.push_back(a.state_id);
		}

		for (auto& part : chip->parts) {
			auto part2world = chip2world * part->pos.calc_matrix();

			flatten_graph(part->chip, part2world);
		}
	}

	void find_graphs () {
		ZoneScoped;

		wire_ids.assign((int)flat_nodes.size(), -1);

		int wid = 0;

		for (int i=0; i<(int)flat_nodes.size(); ++i) {
			if (!flat_nodes[i].visited) {
				int wire_id = wid++;
		
				// could use a queue as well, but order does not matter
				// TODO: profile difference
				std::vector<int> stk;
				stk.push_back(i);
		
				while (!stk.empty()) {
					int cur = stk.back();
					stk.pop_back();
				
					flat_nodes[cur].visited = true;
					wire_ids[cur] = wire_id;
				
					for (int link : flat_nodes[cur].edges) {
						if (link >= 0 && !flat_nodes[link].visited)
							stk.push_back(link);
					}
				}
			}
		}

		num_states = wid;
		
		for (auto& gate : circuit.gates) {
			for (int i=0; i<ARRLEN(gate.pins); ++i) {
				auto& id = gate.pins[i];
				id = id >= 0 ? wire_ids[ id ] : -1;
			}
		}

		for (auto& state_vec : circuit.states) {
			state_vec.state.assign(num_states, 0);
		}

		// turn node map into node pos -> state id map
		circuit.node_map = std::move(node_map);
		for (auto& kv : circuit.node_map) {
			kv.second.state_id = wire_ids[ kv.second.state_id ];
		}
	}
};

void LogicSim::recreate_simulator () {
	ZoneScoped;
	
	circuit = {};

	BuildSim build{circuit};

	{
		ZoneScopedN("flatten_graph");
		build.flatten_graph(viewed_chip.get(), float2x3::identity());

		char buf[128];
		int size = snprintf(buf, 128, "sim_gates: %d  node: %d  edges: %d",
			(int)circuit.gates.size(),
			(int)build.flat_nodes.size(),
			(int)build.flat_edges.size());
		if (size < 128) {
			ZoneText(buf, size);
		}
	}
	build.find_graphs();
}

void Circuit::simulate () {
	ZoneScoped;

	auto& cur  = states[cur_state  ];
	auto& next = states[cur_state^1];

	for (int i=0; i<(int)gates.size(); ++i) {
		auto& gate = gates[i];

		int pin_count = (int)gate_chips[gate.type].pins.size();
		
		for (int i=0; i<pin_count; ++i) {
			int state_id = gate.pins[i];
			assert(state_id >= 0 && state_id < (int)cur.state.size());
		}

		switch (gate.type) {

			case DMUX_GATE: {
				assert(pin_count == 4);

				bool in = (bool)cur.state[gate.pins[2]];
				bool sel = (bool)cur.state[gate.pins[3]];
				
				next.state[gate.pins[0]] = (uint8_t)(sel == 0 && in);
				next.state[gate.pins[1]] = (uint8_t)(sel == 1 && in);
			} continue; // skip

			case DMUX4_GATE: {
				assert(pin_count == 7);

				bool in   = (bool)cur.state[gate.pins[4]];
				bool sel0 = (bool)cur.state[gate.pins[5]];
				bool sel1 = (bool)cur.state[gate.pins[6]];

				next.state[gate.pins[0]] = (uint8_t)(!sel0 && !sel1 && in);
				next.state[gate.pins[1]] = (uint8_t)( sel0 && !sel1 && in);
				next.state[gate.pins[2]] = (uint8_t)(!sel0 &&  sel1 && in);
				next.state[gate.pins[3]] = (uint8_t)( sel0 &&  sel1 && in);
			} continue; // skip
		}

		int out  = gate.pins[0];
		assert(out >= 0 && out < (int)cur.state.size());

		int inputs = pin_count - 1;
	
		assert(inputs >= 1 && inputs <= ARRLEN(Circuit::Gate::pins)-1);
		
		bool args[ARRLEN(Circuit::Gate::pins)];

		for (int i=0; i<inputs; ++i) {
			//args[i-1] = state_id ? (bool)cur.state[state_id] : false;
			args[i] = (bool)cur.state[gate.pins[i+1]];
		}

		bool a = args[0];
		bool b = args[1];
		bool c = args[2];
		bool d = args[3];
		
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

			case MUX_GATE: new_state = c ? b : a;   break;
			case MUX4_GATE:
				//new_state = args[5] ? (args[4] ? args[3] : args[2]) : (args[4] ? args[1] : args[0]);
				new_state = args[ ((int)args[5]<<1) | (int)args[4]];
				break;
	
			default: assert(false);
		}
		
		next.state[out] = (uint8_t)new_state;
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
