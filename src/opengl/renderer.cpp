#include "renderer.hpp"
#include "../app.hpp"
#include "../logic_sim.hpp"

using namespace logic_sim;

namespace ogl {

void CircuitMeshBuilder::draw_wire_segment (int wire_id, float2 a, float2 b, int states, lrgba col) {
	auto* out = push_back(line_groups[wire_id], 1);

	*out++ = { a, b, wire_radius, 0, states, col };
}
void CircuitMeshBuilder::draw_wire_point (int wire_id, float2 pos, float radius, int num_wires, int states, lrgba col) {
	if (num_wires > 2)
		radius *= wire_node_junction;
	if (num_wires == 1)
		radius *= wire_node_dead_end;

	auto* out = push_back(line_groups[wire_id], 1);

	*out++ = { pos, pos, radius, 2, states, col * lrgba(.8f,.8f,.8f, 1.0f) };
}

void CircuitMeshBuilder::finish_wires () {
		
	for (auto& lines : line_groups) {
		size_t count = lines.size();
		if (count == 0) return;

		auto* out = push_back(mesh.wires_mesh, count*2);

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

void CircuitMeshBuilder::draw_gate (float2x3 const& mat, float2 size, int type, int state, lrgba col) {
	//if (type < 2)
	//	return; // TEST: don't draw INP/OUT_PINs

	if (type >= AND3_GATE)
		type = type - AND3_GATE + AND_GATE;

	uint16_t idx = (uint16_t)mesh.gates_mesh.size();
		
	constexpr float2 verts[] = {
		float2(-0.5f, -0.5f),
		float2(+0.5f, -0.5f),
		float2(+0.5f, +0.5f),
		float2(-0.5f, +0.5f),
	};

	auto* pv = push_back(mesh.gates_mesh, 4);
	pv[0] = { mat * (verts[0] * size), (verts[0] * size) + 0.5f, type, state, col };
	pv[1] = { mat * (verts[1] * size), (verts[1] * size) + 0.5f, type, state, col };
	pv[2] = { mat * (verts[2] * size), (verts[2] * size) + 0.5f, type, state, col };
	pv[3] = { mat * (verts[3] * size), (verts[3] * size) + 0.5f, type, state, col };
		
	auto* pi = push_back(mesh.gates_mesh_indices, 6);
	ogl::push_quad(pi, idx+0, idx+1, idx+2, idx+3);
}

	
void Renderer::begin (Window& window, App& app, int2 window_size) {
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


	{ // Gates and wires
		ZoneScopedN("push gates");
		
		//draw_chip(app, app.sim.viewed_chip.get(), float2x3::identity(), 0, lrgba(1));
	}

	{
		//if (app.editor.in_mode<Editor::WireMode>()) {
		//	auto& w = std::get<Editor::WireMode>(app.editor.mode);
		//	
		//	LineGroup lines;
		//	
		//	// w.prev is always a real existing point, don't draw preview
		//	// w.cur can be a real existing point unless it points to w.node
		//	if (w.cur && w.cur == &w.node) {
		//		int wires = w.cur->num_wires() + (!w.prev || w.cur->edges.contains(w.prev) ? 0 : 1);
		//		lines.draw_wire_point(w.cur->pos, wire_radius*1.12f, wires, 0, preview_line_col);
		//	}
		//	
		//	if (w.cur && w.prev) {
		//		lines.draw_wire_segment(w.prev->pos, w.cur->pos, 0, preview_line_col);
		//	}
		//
		//	add_line_group(line_renderer, lines);
		//}
	}

	{ // Gate preview
		if (app.editor.in_mode<Editor::PlaceMode>() && app.editor._cursor_valid) {
			auto& pl = std::get<Editor::PlaceMode>(app.editor.mode);
			
			assert(pl.place_chip);
			auto part2chip = pl.place_pos.calc_matrix();

			//draw_chip(app, pl.place_chip, part2chip, -1, lrgba(1,1,1,0.75f));
			
			//LineGroup lines;
			//
			//for (auto& pin : pl.place_chip->inputs) {
			//	float2 a = part2chip * get_inp_pos(*pin);
			//	float2 b = part2chip * pin->pos.pos;
			//
			//	lines.draw_wire_segment(float2x3::identity(), a, b, 0, preview_line_col);
			//	lines.draw_wire_point(float2x3::identity(), a, wire_radius, 1, 0, preview_line_col);
			//}
			//
			//add_line_group(line_renderer, lines);
		}
	}

	update_mesh(app.sim.circuit.mesh);
	
	draw_wires(state, app.sim_t);
	draw_gates(state);

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
