#include "renderer.hpp"
#include "../game.hpp"
#include "../logic_sim.hpp"

using namespace logic_sim;

namespace ogl {

// Group of line segments and points that are drawn such that they appear connected (outlines appear behind all of the shapes)
struct LineGroup {
	std::vector<LineRenderer::LineInstance> lines;

	LineGroup () {
		lines.reserve(1024);
	}
		
	void draw_wire_segment (float2x3 const& chip2world, float2 a, float2 b, int states, lrgba col) {
		auto* out = push_back(lines, 1);

		float radius = abs(((float2x2)chip2world * float2(wire_radius)).x);

		float2 p0 = chip2world * a;
		float2 p1 = chip2world * b;

		*out++ = { p0, p1, radius, 0, states, col };
	}

	void draw_wire_point (float2x3 const& chip2world, float2 pos, float radius, int num_wires, int states, lrgba col) {
		if (num_wires == 0 || num_wires > 2)
			radius *= wire_node_radius_fac;

		auto* out = push_back(lines, 1);

		float r = abs(((float2x2)chip2world * float2(radius)).x);

		float2 p = chip2world * pos;

		*out++ = { p, p, r, 2, states, col * lrgba(.8f,.8f,.8f, 1.0f) };
	}
};

void add_line_group (LineRenderer& lines, LineGroup& group) {
	size_t count = group.lines.size();
	if (count == 0) return;

	LineRenderer::LineInstance* out = push_back(lines.lines, count*2);

	// BG elements
	memcpy(out, group.lines.data(), count * sizeof(group.lines[0]));
	out += count;
	
	// FG elements
	memcpy(out, group.lines.data(), count * sizeof(group.lines[0]));
	for (size_t i=0; i<count; ++i) {
		out[i].type += 1;
	}
}

void Renderer::draw_gate (float2x3 const& mat, float2 size, int type, int state, lrgba col) {
	//if (type < 2)
	//	return; // TEST: don't draw INP/OUT_PINs

	if (type >= AND3_GATE)
		type = type - AND3_GATE + AND_GATE;

	uint16_t idx = (uint16_t)tri_renderer.verticies.size();
		
	constexpr float2 verts[] = {
		float2(-0.5f, -0.5f),
		float2(+0.5f, -0.5f),
		float2(+0.5f, +0.5f),
		float2(-0.5f, +0.5f),
	};

	auto* pv = push_back(tri_renderer.verticies, 4);
	pv[0] = { mat * (verts[0] * size), (verts[0] * size) + 0.5f, type, state, col };
	pv[1] = { mat * (verts[1] * size), (verts[1] * size) + 0.5f, type, state, col };
	pv[2] = { mat * (verts[2] * size), (verts[2] * size) + 0.5f, type, state, col };
	pv[3] = { mat * (verts[3] * size), (verts[3] * size) + 0.5f, type, state, col };
		
	auto* pi = push_back(tri_renderer.indices, 6);
	ogl::push_quad(pi, idx+0, idx+1, idx+2, idx+3);
}

constexpr lrgba line_col = lrgba(0.8f, 0.01f, 0.025f, 1);
constexpr lrgba preview_line_col = line_col * lrgba(1,1,1, 0.75f);

void Renderer::draw_chip (Game& g, Chip* chip, float2x3 const& chip2world, int chip_state, lrgba col) {
	auto& editor = g.editor;

	uint8_t* prev = g.sim.state[g.sim.cur_state^1].data();
	uint8_t* cur  = g.sim.state[g.sim.cur_state  ].data();

	lrgba lcol = line_col * col;

	if (is_gate(chip)) {
		auto type = gate_type(chip);
		uint8_t state = chip_state >= 0 ? cur[chip_state] : 1;
		
		draw_gate(chip2world, chip->size, type, state, lrgba(chip->col, 1) * col);
	}
	else {
		LineGroup lines;

		{ // TODO: make this look nicer, rounded thick outline? color the background inside chip differently?
			float2 center = chip2world * float2(0);
			float2 size = abs( (float2x2)chip2world * chip->size ) - 1.0f/16;
			dbgdraw.wire_quad(float3(center - size*0.5f, 0.0f), size, lrgba(0.001f, 0.001f, 0.001f, 1));
		}

		for (auto& e : chip->wire_edges) {
			lines.draw_wire_segment(chip2world, e->a->pos, e->b->pos, 0, lcol);
		}
		for (auto& n : chip->wire_nodes) {
			lines.draw_wire_point(chip2world, n->pos, wire_radius*1.1f, n->num_wires(), 0, lcol);
		}
		
		auto draw_part_wires = [&] (Part* part) {
			uint8_t* prev = g.sim.state[g.sim.cur_state^1].data();
			uint8_t* cur  = g.sim.state[g.sim.cur_state  ].data();
		
			auto part2chip = part->pos.calc_matrix();

			if (part->chip != &gates[INP_PIN]) {
				for (int i=0; i<(int)part->chip->inputs.size(); ++i) {
					auto& inp = part->chip->inputs[i];
				
					float2 a = part2chip * get_inp_pos(*inp);
					float2 b = part2chip * inp->pos.pos;

					lines.draw_wire_segment(chip2world, a,b, 0, lcol);
					
					auto& pin = part->pins[i];
					lines.draw_wire_point(chip2world, pin.node->pos, wire_radius*1.1f, pin.node->num_wires(), 0, lcol);
				}
			}
			if (part->chip != &gates[OUT_PIN]) {
				for (int i=0; i<(int)part->chip->outputs.size(); ++i) {
					auto& out = part->chip->outputs[i];
				
					float2 a = part2chip * get_out_pos(*out);
					float2 b = part2chip * out->pos.pos;

					lines.draw_wire_segment(chip2world, a,b, 0, lcol);

					auto& pin = part->pins[i + (int)part->chip->inputs.size()];
					lines.draw_wire_point(chip2world, pin.node->pos, wire_radius*1.1f, pin.node->num_wires(), 0, lcol);
				}
			}
		};
		for_each_part(*chip, draw_part_wires);
		
		add_line_group(line_renderer, lines);

		//
		auto draw_part = [&] (Part* part) {
			auto part2chip = part->pos.calc_matrix();
			auto part2world = chip2world * part2chip;

			draw_chip(g, part->chip, part2world, chip_state >= 0 ? chip_state + part->sid : -1, col);
		};
		for_each_part(*chip, draw_part);
	}
}
	
void Renderer::begin (Window& window, Game& g, int2 window_size) {
	dbgdraw.clear();
	text_renderer.begin();

	tri_renderer.update(window.input);
	line_renderer.update(window.input);
}

void Renderer::end (Window& window, Game& g, int2 window_size) {
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
			
		draw_chip(g, g.sim.viewed_chip.get(), float2x3::identity(), 0, lrgba(1));
	}

	{
		if (g.editor.in_mode<Editor::WireMode>()) {
			auto& w = std::get<Editor::WireMode>(g.editor.mode);
			
			LineGroup lines;
			
			// w.prev is always a real existing point, don't draw preview
			// w.cur can be a real existing point unless it points to w.node
			if (w.cur && w.cur == &w.node) {
				int wires = w.cur->num_wires() + (!w.prev || w.cur->edges.contains(w.prev) ? 0 : 1);
				lines.draw_wire_point(float2x3::identity(), w.cur->pos, wire_radius*1.12f, wires, 0, preview_line_col);
			}
			
			if (w.cur && w.prev) {
				lines.draw_wire_segment(float2x3::identity(), w.prev->pos, w.cur->pos, 0, preview_line_col);
			}

			add_line_group(line_renderer, lines);
		}
	}

	{ // Gate preview
		if (g.editor.in_mode<Editor::PlaceMode>() && g.editor._cursor_valid) {
			auto& pl = std::get<Editor::PlaceMode>(g.editor.mode);
			
			assert(pl.place_chip);
			auto part2chip = pl.place_pos.calc_matrix();

			draw_chip(g, pl.place_chip, part2chip, -1, lrgba(1,1,1,0.75f));
			
			LineGroup lines;

			for (auto& pin : pl.place_chip->inputs) {
				float2 a = part2chip * get_inp_pos(*pin);
				float2 b = part2chip * pin->pos.pos;

				lines.draw_wire_segment(float2x3::identity(), a, b, 0, preview_line_col);
				lines.draw_wire_point(float2x3::identity(), a, wire_radius, 1, 0, preview_line_col);
			}
			for (auto& pin : pl.place_chip->outputs) {
				float2 a = part2chip * pin->pos.pos;
				float2 b = part2chip * get_out_pos(*pin);

				lines.draw_wire_segment(float2x3::identity(), a, b, 0, preview_line_col);
				lines.draw_wire_point(float2x3::identity(), b, wire_radius, 1, 0, preview_line_col);
			}

			add_line_group(line_renderer, lines);
		}
	}
		
	line_renderer.render(state, g.sim_t);
	tri_renderer.render(state);

	gl_dbgdraw.render(state, dbgdraw);
	
	if (g.sim_paused) {
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
