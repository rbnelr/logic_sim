#include "common.hpp"
#include "logic_sim.hpp"
#include "app.hpp"
#include "opengl/renderer.hpp"

namespace logic_sim {
	
////

// TODO: eliminate somehow
template <typename FUNC>
void for_each_wire_node (Chip& chip, FUNC func) {
	for_each_part(chip, [&] (Part* part) {
		for (auto& pin : part->pins) {
			func(pin.node.get());
		}
	});

	for (auto& node : chip.wire_nodes) {
		func(node.get());
	}
}

struct BuildSim {
	Simulator& sim;
	ogl::Renderer& r;

	struct FlatNode {
		std::vector<int> edges;
	};

	std::vector<FlatNode> flat_nodes;
	
	int collect_nodes (Chip* chip, int base_id, float2x3 const& chip2world) {
		
		ogl::LineGroup lines;
		lrgba lcol = line_col * lrgba(1);
		
		for (auto& e : chip->wire_edges) {
			int state = 0;

			lines.draw_wire_segment(chip2world, e->a->pos, e->b->pos, state, lcol);
		}

		// collect all nodes recursively
		int local_ids = 0;
		for (auto& node : chip->wire_nodes) {
			node->local_id = local_ids++;
			//node_grid.add(chip2world, local_ids++);
			flat_nodes.emplace_back();
		}
		for (auto& part : chip->parts) {
			auto part2chip = part->pos.calc_matrix();
			auto part2world = chip2world * part2chip;

			part->local_id = local_ids;
			local_ids += collect_nodes(part->chip, local_ids, part2world);

			
			for (int i=0; i<(int)part->chip->inputs.size(); ++i) {
				auto& inp = part->chip->inputs[i];
				
				float2 a = part2chip * get_inp_pos(*inp);
				float2 b = part2chip * inp->pos.pos;
					
				auto& pin = part->pins[i];
				//auto& spin = gate.pins[i];
				
				int state = 0;

				lines.draw_wire_segment(chip2world, a,b, state, lcol);

				r.draw_text(prints("%d", base_id + pin.node->local_id), chip2world * pin.node->pos, 10, 1);
				lines.draw_wire_point(chip2world, pin.node->pos, wire_radius, pin.node->num_wires(), state, lcol);
			}
			for (int i=0; i<(int)part->chip->outputs.size(); ++i) {
				auto& out = part->chip->outputs[i];
				
				float2 a = part2chip * get_out_pos(*out);
				float2 b = part2chip * out->pos.pos;
					
				auto& pin = part->pins[i + (int)part->chip->inputs.size()];
				//auto& spin = gate.pins[i + (int)part->chip->inputs.size()];
				
				int state = 0;

				lines.draw_wire_segment(chip2world, a,b, state, lcol);
				
				r.draw_text(prints("%d", base_id + pin.node->local_id), chip2world * pin.node->pos, 10, 1);
				lines.draw_wire_point(chip2world, pin.node->pos, wire_radius, pin.node->num_wires(), state, lcol);
			}
		}

		for (auto& n : chip->wire_nodes) {
			int state = 0;

			r.draw_text(prints("%d", base_id + n->local_id), chip2world * n->pos, 10, 1);
			lines.draw_wire_point(chip2world, n->pos, wire_radius, n->num_wires(), state, lcol);
		}
		
		ogl::add_line_group(r.line_renderer, lines);

		if (is_gate(chip)) {
			auto type = gate_type(chip);
			uint8_t state = 0;
		
			r.draw_gate(chip2world, chip->size, type, state, lrgba(chip->col, 1) * lrgba(1));
		}
		
		//// link up all nodes between our chip and any direct children parts
		//for (auto& e : chip->wire_edges) {
		//	auto get_node_sid = [&] (WireNode* node) {
		//		Part* part = node->parent_part;
		//		if (!part)
		//			return node->local_id;
		//
		//		// TODO: expensive indexof, optimize via cached indices or similar
		//		int idx = indexof(part->pins, node, [] (Part::Pin const& pin, WireNode* node) {
		//			return pin.node.get() == node;
		//		});
		//		int sid_in_part = part->chip->wire_nodes[idx]->local_id;
		//		//int sid_in_part = part->chip.wire_nodes[ part.pins.indexof(node) ].local_id;
		//		return part->local_id + sid_in_part;
		//	};
		//	
		//	int a = base_id + get_node_sid(e->a);
		//	int b = base_id + get_node_sid(e->b);
		//	
		//	// link sim nodes
		//	flat_nodes[a].edges.emplace_back(b);
		//	flat_nodes[b].edges.emplace_back(a);
		//}
		
		chip->local_ids = local_ids;
		return local_ids;
	}
};

void LogicSim::recreate_simulator (ogl::Renderer& r) {
	
	sim.gates.clear();

	BuildSim build{sim, r};

	build.collect_nodes(viewed_chip.get(), 0, float2x3::identity());
}

#if 0
int sim_ (Chip& chip) {
	

	for_each_wire_node(chip, [&] (WireNode* node) {
		node->visited = false;
	});
	
	int sid = 0;
	
	for_each_wire_node(chip, [&] (WireNode* node) {
		if (node->visited)
			return;
		
		int wire_sid = sid++;
		
		// could use a queue as well, but order does not matter
		// TODO: profile difference
		std::vector<WireNode*> stk;
		stk.push_back(node);
		
		while (!stk.empty()) {
			WireNode* cur_node = stk.back();
			stk.pop_back();
				
			cur_node->visited = true;
			cur_node->sid = wire_sid;
				
			for (WireNode* link : cur_node->edges) {
				if (!link->visited)
					stk.push_back(link);
			}

			if (cur_node->parent_part) {
				// ?

			}
		}
	});
	chip.wire_states = sid;

	for_each_part(chip, [&] (Part* part) {
		sid += update_state_indices(*part->chip);
	});

}
#endif

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
			
	// TODO: Is this really needed? How else to deep copy links correctly?
	// -> Good reason to use indices instead of pointers again?
	std::unordered_map<Part*, Part*> map; // old* -> new*
			
	auto create_part = [&] (Part* old) {
		auto new_ = std::make_unique<Part>(old->chip, std::string(old->name), old->pos);
		map.emplace(old, new_.get());
		return new_;
	};
	auto deep_copy = [&] (Part* old, Part* new_) {
		for (int i=0; i<(int)old->pins.size(); ++i) {
			new_->pins[i].node = std::make_unique<WireNode>(*old->pins[i].node);
		}
	};

	c.outputs.reserve(outputs.size());
	c.inputs .reserve(inputs .size());
	c.parts  .reserve(parts  .size());

	for (auto& p : outputs)
		c.outputs.emplace_back( create_part(p.get()) );
			
	for (auto& p : inputs)
		c.inputs .emplace_back( create_part(p.get()) );

	for (auto& p : parts)
		c.parts  .add( create_part(p.get()) );
			
	for (size_t i=0; i<outputs.size(); ++i) {
		deep_copy(c.outputs[i].get(), outputs[i].get());
	}
	for (size_t i=0; i<inputs.size(); ++i) {
		deep_copy(c.inputs[i].get(), inputs[i].get());
	}
	for (auto& old : parts) {
		deep_copy(old.get(), map[old.get()]);
	}

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

/*
// TODO: eliminate somehow
template <typename FUNC>
void for_each_wire_node (Chip& chip, FUNC func) {
	for_each_part(chip, [&] (Part* part) {
		for (auto& pin : part->pins) {
			func(pin.node.get());
		}
	});

	for (auto& node : chip.wire_nodes) {
		func(node.get());
	}
}
int update_state_indices (Chip& chip) {
	if (chip.state_count >= 0)
		return chip.state_count; // already up to date

	for_each_wire_node(chip, [&] (WireNode* node) {
		node->visited = false;
	});
		
	int sid = 0;
		
	for_each_wire_node(chip, [&] (WireNode* node) {
		if (node->visited)
			return;
		
		int wire_sid = sid++;
		
		// could use a queue as well, but order does not matter
		// TODO: profile difference
		std::vector<WireNode*> stk;
		stk.push_back(node);
			
		while (!stk.empty()) {
			WireNode* cur_node = stk.back();
			stk.pop_back();
				
			cur_node->visited = true;
			cur_node->sid = wire_sid;
				
			for (WireNode* link : cur_node->edges) {
				if (!link->visited)
					stk.push_back(link);
			}
		}
	});
	chip.wire_states = sid;

	for_each_part(chip, [&] (Part* part) {
		sid += update_state_indices(*part->chip);
	});

	chip.state_count = sid;
	return chip.state_count;
}
void update_all_chip_state_indices (LogicSim& sim) {

	// invalidate all chips and recompute state_counts
	for (auto& c : sim.saved_chips) {
		c->wire_states = -1;
		c->state_count = -1;
	}
	sim.viewed_chip->wire_states = -1;
	sim.viewed_chip->state_count = -1;

	for (auto& c : sim.saved_chips)
		update_state_indices(*c);
	update_state_indices(*sim.viewed_chip);
}
*/

void LogicSim::update_chip_state () {
	unsaved_changes = true;

	recompute_chip_users();
	
	//update_all_chip_state_indices(*this);
	
	//// TODO
	//for (int i=0; i<2; ++i)
	//	state[i].assign(viewed_chip->state_count, 0);
}

void LogicSim::add_part (Chip& chip, Chip* part_chip, Placement part_pos) {
	assert(&chip == viewed_chip.get());

	auto* ptr = new Part(part_chip, "", part_pos);

	if (part_chip == &gates[INP_PIN]) {
		
		int idx = (int)chip.inputs.size();
		chip.inputs.emplace_back(ptr);

		for (auto& schip : saved_chips) {
			for (auto& part : schip->parts) {
				if (part->chip == &chip) {
					part->pins.emplace(part->pins.begin() + idx,
						std::make_unique<WireNode>(part->pos.calc_matrix() * get_inp_pos(*ptr), part.get()));
				}
			}
		}
	}
	else if (part_chip == &gates[OUT_PIN]) {
		
		chip.outputs.emplace_back(ptr);

		for (auto& schip : saved_chips) {
			for (auto& part : schip->parts) {
				if (part->chip == &chip) {
					part->pins.emplace_back(
						std::make_unique<WireNode>(part->pos.calc_matrix() * get_out_pos(*ptr), part.get()));
				}
			}
		}
	}
	else {
		// insert part at end of parts list
		chip.parts.add(ptr);
	}
	
	ptr->update_pins_pos();
	
	unsaved_changes = true;
}
void LogicSim::remove_part (Chip& chip, Part* part) {
	assert(&chip == viewed_chip.get());

	for (auto& pin : part->pins)
		disconnect_wire_node(chip, pin.node.get());

	if (part->chip == &gates[INP_PIN]) {
		int idx = indexof(chip.inputs, part, _partptr_equal());
		assert(idx >= 0);

		chip.inputs.erase(chip.inputs.begin() + idx);
	
		int i = idx;
		for (auto& schip : saved_chips) {
			for (auto& part : schip->parts) {
				if (part->chip == &chip) {
					disconnect_wire_node(*schip, part->pins[i].node.get());
					part->pins.erase(part->pins.begin() + i);
				}
			}
		}
	}
	else if (part->chip == &gates[OUT_PIN]) {
		int idx = indexof(chip.outputs, part, _partptr_equal());
		assert(idx >= 0);

		chip.outputs.erase(chip.outputs.begin() + idx);
		
		int i = idx + (int)chip.inputs.size();
		for (auto& schip : saved_chips) {
			for (auto& part : schip->parts) {
				if (part->chip == &chip) {
					disconnect_wire_node(*schip, part->pins[i].node.get());
					part->pins.erase(part->pins.begin() + i);
				}
			}
		}
	}
	else {
		chip.parts.try_remove(part);
	}
	
	unsaved_changes = true;
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
	part2idx.reserve(chip.inputs.size() + chip.outputs.size() + chip.parts.size());
	
	std::unordered_map<WireNode*, int> node2idx;
	node2idx.reserve(chip.wire_nodes.size() + chip.parts.size()*4); // estimate

	int idx = 0;
	int node_idx = 0;

	for (auto& wire : chip.wire_nodes) {
		node2idx[wire.get()] = node_idx++;
	}

	for (auto& part : chip.outputs) {
		part2idx[part.get()] = idx++;

		for (auto& pin : part->pins) {
			node2idx[pin.node.get()] = node_idx++;
		}
	}
	for (auto& part : chip.inputs) {
		part2idx[part.get()] = idx++;

		for (auto& pin : part->pins) {
			node2idx[pin.node.get()] = node_idx++;
		}
	}
	for (auto& part : chip.parts) {
		part2idx[part.get()] = idx++;

		for (auto& pin : part->pins) {
			node2idx[pin.node.get()] = node_idx++;
		}
	}

	json jouts  = json::array();
	json jinps  = json::array();
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

	for (auto& part : chip.outputs) {
		jouts.emplace_back( part2json(sim, *part, part2idx) );
	}

	for (auto& part : chip.inputs) {
		jinps.emplace_back( part2json(sim, *part, part2idx) );
	}

	for (auto& part : chip.parts) {
		jparts.emplace_back( part2json(sim, *part, part2idx) );
	}
	
	json j = {
		{"name",       chip.name},
		{"col",        chip.col},
		{"size",       chip.size},
		{"inputs",     std::move(jinps)},
		{"outputs",    std::move(jouts)},
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
	for (auto& pin : ptr->pins) {
		idx2node.emplace_back(pin.node.get());
	}
	return ptr;
}
void json2chip (const json& j, Chip& chip, LogicSim& sim) {
	
	auto& jouts = j.at("outputs");
	auto& jinps = j.at("inputs");
	auto& jparts = j.at("parts");
	auto& jnodes = j.at("wire_nodes");
	auto& jedges = j.at("wire_edges");
	size_t total = jouts.size() + jinps.size() + jparts.size();
	
	chip.parts.reserve((int)jparts.size());

	chip.wire_nodes.reserve((int)jnodes.size());
	chip.wire_edges.reserve((int)jedges.size());

	// first pass to create pointers
	std::vector<Part*> idx2part(total, nullptr);
	
	std::vector<WireNode*> idx2node;
	idx2node.reserve(chip.wire_nodes.size() + chip.parts.size()*4);

	for (auto& j : jnodes) {
		float2 pos = j.at("pos");
		auto node = std::make_unique<WireNode>(pos, nullptr);
		idx2node.emplace_back(node.get());
		chip.wire_nodes.add(std::move(node));
	}

	int idx = 0;
	int out_idx = 0, inp_idx = 0;
	for (auto& j : jouts) {
		auto* ptr = idx2part[idx++] = json2part(j, sim, idx2node);
		chip.outputs[out_idx++] = std::unique_ptr<Part>(ptr);
	}
	for (auto& j : jinps) {
		auto* ptr = idx2part[idx++] = json2part(j, sim, idx2node);
		chip.inputs[inp_idx++] = std::unique_ptr<Part>(ptr);
	}
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

		chip->outputs.resize(jchip.at("outputs").size());
		chip->inputs .resize(jchip.at("inputs" ).size());
	}
	// then convert chip ids references in parts to valid chips
	auto cur = sim.saved_chips.begin();
	for (auto& jchip : j["chips"]) {
		json2chip(jchip, *(*cur++), sim);
	}
	
	for (auto& chip : sim.saved_chips) {
		for (auto& part : chip->inputs) {
			part->update_pins_pos();
		}
		for (auto& part : chip->outputs) {
			part->update_pins_pos();
		}
		for (auto& part : chip->parts) {
			part->update_pins_pos();
		}
	}

	sim.recompute_chip_users();

	int viewed_chip_idx = j["viewed_chip"];
	if (viewed_chip_idx >= 0) {
		sim.switch_to_chip_view( sim.saved_chips[viewed_chip_idx]);
	}
}

} // namespace logic_sim
