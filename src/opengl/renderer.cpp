#include "renderer.hpp"
#include "../app.hpp"
#include "../logic_sim.hpp"

using namespace logic_sim;

namespace ogl {

struct CircuitMeshBuilder {
	Circuit& circuit;

	std::vector<CircuitDraw::WireVertex> wires_mesh;

	std::vector<CircuitDraw::GateVertex> gates_mesh;
	std::vector<uint16_t>                gates_mesh_indices;
	
	std::vector< std::vector<CircuitDraw::WireVertex> > line_groups;

	std::vector<CircuitDraw::ChipDraw> chips;

	int gate_count = 0;

	void draw_wire_edge (float2 a, float2 b, int state_id, lrgba col) {
		auto* out = push_back(line_groups[state_id], 1);

		*out++ = { a, b, wire_radius, 0, state_id, col };
	}
	void draw_wire_point (float2 pos, int num_wires, int state_id, lrgba col) {
		float radius = wire_radius;
		if (num_wires > 2)
			radius *= wire_node_junction;
		if (num_wires <= 1)
			radius *= wire_node_dead_end;

		auto* out = push_back(line_groups[state_id], 1);

		*out++ = { pos, pos, radius, 2, state_id, col * lrgba(.8f,.8f,.8f, 1.0f) };
	}
	
	void finish_wires () {
		ZoneScoped;
		
		for (auto& lines : line_groups) {
			size_t count = lines.size();
			if (count == 0) return;

			auto* out = push_back(wires_mesh, count*2);

			// BG elements
			memcpy(out, lines.data(), count * sizeof(lines[0]));
			out += count;
	
			// FG elements
			memcpy(out, lines.data(), count * sizeof(lines[0]));
			for (size_t i=0; i<count; ++i) {
				out[i].type += 1;
			}
		}
	}
	
	void draw_gate (float2x3 const& mat, float2 size, int type, int state_id, lrgba col) {
		//if (type < 2)
		//	return; // TEST: don't draw INP/OUT_PINs

		uint16_t idx = (uint16_t)gates_mesh.size();
		
		constexpr float2 verts[] = {
			float2(-0.5f, -0.5f),
			float2(+0.5f, -0.5f),
			float2(+0.5f, +0.5f),
			float2(-0.5f, +0.5f),
		};
		float2 uv_scale = size * 0.125f;

		auto* pv = push_back(gates_mesh, 4);
		for (int i=0; i<4; ++i)
			pv[i] = { mat * (verts[i] * size), (verts[i] * uv_scale) + 0.5f, type, state_id, col };
		
		auto* pi = push_back(gates_mesh_indices, 6);
		ogl::push_quad(pi, idx+0, idx+1, idx+2, idx+3);
	}

	Circuit::NodeMapEntry get_node_map (float2 pos, int dummy_state) {
		return dummy_state ? Circuit::NodeMapEntry{dummy_state,0} : circuit.node_map[roundi(pos)];
	}

	void draw_chip (Chip* chip, float2x3 const& chip2world, int dummy_state, lrgba tint) {
		
		if (is_gate(chip)) {
			
			{
				// Could also be gotten through circuit.node_map
				int state_id = dummy_state ? dummy_state : circuit.gates[gate_count++].pins[0];
				draw_gate(chip2world, chip->size, gate_type(chip), state_id, lrgba(chip->col, 1) * tint);
			}

			for (auto& pin : chip->pins) {
				float2 a = chip2world * pin.pos;
				float2 b = chip2world * (pin.pos + ROT[pin.rot] * float2(pin.len, 0));
				
				// TODO: improve?
				auto nmap = get_node_map(a, dummy_state); // NOTE: for preview stuff this actually creates entries
				draw_wire_edge(a, b, nmap.state_id, line_col * tint);

				draw_wire_point(a, nmap.num_wires, nmap.state_id, line_col * tint);
			}
		}
		else {
			// TODO: make this look nicer, rounded thick outline? color the background inside chip differently?
			float2 center = chip2world * float2(0);
			float2 size = abs( (float2x2)chip2world * chip->size ) - 1.0f/16;
			chips.push_back({ center, size, lrgba(0.001f, 0.001f, 0.001f, 1) * tint });
		}

		for (auto& edge : chip->wire_edges) {
			float2 a = chip2world * edge->a->pos;
			float2 b = chip2world * edge->b->pos;
			auto nmap = get_node_map(a, dummy_state);
			draw_wire_edge(a, b, nmap.state_id, line_col);
		}

		for (auto& part : chip->parts) {
			auto part2world = chip2world * part->pos.calc_matrix();

			draw_chip(part->chip, part2world, dummy_state, tint);
		}

		for (auto& node : chip->wire_nodes) {
			float2 pos = chip2world * node->pos;
			auto nmap = get_node_map(pos, dummy_state);
			draw_wire_point(pos, nmap.num_wires, nmap.state_id, line_col * tint);
		}
	}

	void draw_wire_edit (Editor::WireMode& w) {

		int state_id = (int)line_groups.size();
		line_groups.emplace_back();

		// w.prev is always a real existing point, don't draw preview
		// w.cur can be a real existing point unless it points to w.node
		if (w.cur && w.cur == &w.node) {
			float2 pos = w.cur->pos;
			auto nmap = get_node_map(pos, 0);
			draw_wire_point(pos, nmap.num_wires, state_id, preview_line_col);
		}

		if (w.cur && w.prev) {
			draw_wire_edge(w.prev->pos, w.cur->pos, state_id, preview_line_col);
		}
	}
};

void CircuitDraw::remesh (App& app) {
	CircuitMeshBuilder mesh{app.sim.circuit};

	mesh.line_groups.resize(app.sim.circuit.states[0].state.size());

	mesh.draw_chip(app.sim.viewed_chip.get(), float2x3::identity(), 0, 1);

	if (app.editor.in_mode<Editor::WireMode>() && app.editor._cursor_valid) {
		auto& w = std::get<Editor::WireMode>(app.editor.mode);
		mesh.draw_wire_edit(w);
	}

	if (app.editor.in_mode<Editor::PlaceMode>() && app.editor._cursor_valid) {
		
		int dummy_state_id = (int)mesh.line_groups.size();
		mesh.line_groups.emplace_back();

		auto& pl = std::get<Editor::PlaceMode>(app.editor.mode);
		mesh.draw_chip(pl.place_chip, pl.place_pos.calc_matrix(), dummy_state_id, lrgba(1,1,1, 0.5f));
	}

	mesh.finish_wires();

	//
	vbo_gates.stream(mesh.gates_mesh, mesh.gates_mesh_indices);
	vbo_wires.stream(mesh.wires_mesh);

	vbo_gates_indices = (GLsizei)mesh.gates_mesh_indices.size();
	vbo_wires_vertices = (GLsizei)mesh.wires_mesh.size();

	chips = std::move(mesh.chips);
}

void Renderer::begin (Window& window, App& app, int2 window_size) {
	ZoneScoped;

	dbgdraw.clear();
	text_renderer.begin();
}

void Renderer::end (Window& window, App& app, int2 window_size) {
	ZoneScoped;
		
	{
		OGL_TRACE("setup");

		{
			//OGL_TRACE("set state defaults");

			state.wireframe          = gl_dbgdraw.wireframe;
			state.wireframe_no_cull  = gl_dbgdraw.wireframe_no_cull;
			state.wireframe_no_blend = gl_dbgdraw.wireframe_no_blend;

			state.set_default();

			glEnable(GL_LINE_SMOOTH);
			glLineWidth(gl_dbgdraw.line_width);
		}

		{
			common_ubo.set(view);

			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gl_dbgdraw.indirect_vbo);
			
			gl_dbgdraw.update(window.input);
		}
	}
		
	{
		glViewport(0,0, window.input.window_size.x, window.input.window_size.y);
		glClearColor(0.01f, 0.012f, 0.014f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	draw_background();

	// TODO: Only remesh when needed
	circuit_draw.remesh(app);

	static bool draw_state_ids = false;
	ImGui::Checkbox("draw_state_ids", &draw_state_ids);

	if (draw_state_ids) {
		for (auto& kv : app.sim.circuit.node_map) {
			draw_text(prints("%d", kv.second.num_wires), (float2)kv.first, 10, 1);
		}
		
		auto& circ = app.sim.circuit;
		auto& cur_state = circ.states[circ.cur_state];
		
		std::string str;
		str.resize(cur_state.state.size());
		
		for (int i=0; i<(int)cur_state.state.size(); ++i) {
			str[i] = cur_state.state[i] ? '1' : '0';
			//bool b = cur_state.state[i];
			//ImGui::SameLine(0, 1);
			//ImGui::TextColored(b ? ImVec4(1,0.2f,0.2f,1) : ImVec4(0.01f,0.01f,0.1f,1.0f), b ? "1":"0");
		}
		
		ImGui::Text(str.c_str());
	}

	circuit_draw.draw_wires(state, app.sim_t);
	circuit_draw.draw_gates(state, app.sim_t);

	for (auto& chip : circuit_draw.chips) {
		dbgdraw.wire_quad(float3(chip.center - chip.size*0.5f, 0.0f), chip.size, chip.col);
	}

	gl_dbgdraw.render(state, dbgdraw);
	
	if (app.sim_paused) {
		screen_outline.draw(state, lrgba(0.2f, 0.2f, 1, 1));
	}
	
	glLineWidth(gl_dbgdraw.line_width);

	text_renderer.render(state);

	{
		OGL_TRACE("draw ui");
		
		if (window.trigger_screenshot && !window.screenshot_hud) take_screenshot(window_size);
		
		// draw HUD

		window.draw_imgui();

		if (window.trigger_screenshot && window.screenshot_hud)  take_screenshot(window_size);
		window.trigger_screenshot = false;
	}
}

} // namespace ogl
