#include "renderer.hpp"
#include "../game.hpp"
#include "../logic_sim.hpp"

using namespace logic_sim;

namespace ogl {

void Renderer::build_line (float2x3 const& chip2world,
		float2 start0, float2 start1, std::vector<float2> const& points, float2 end0, float2 end1,
		int states, lrgba col) {
	float2 prev = chip2world * end1;
	float dist = 0;
		
	size_t count = 3 + points.size();
	auto* lines = push_back(line_renderer.lines, count);

	auto* out = lines;

	float radius = abs(((float2x2)chip2world * float2(0.05f)).x);

	auto line_seg = [&] (float2 p) {
		float2 cur = chip2world * p;

		float dist0 = dist;
		dist += distance(prev, cur);

		*out++ = { prev, cur, float2(dist0, dist), radius, states, col, wire_id };

		prev = cur;
	};

	// output lines in reverse so that earlier segments appear on top
	// (because I think it looks nicer with my weird outlines)
	line_seg(end0);

	for (int i=(int)points.size()-1; i>=0; --i) {
		line_seg(points[i]);
	}

	line_seg(start1);
	line_seg(start0);

	// flip and normalize t to [0,1]
	float norm = 1.0f / dist;
	for (size_t i=0; i<count; ++i) {
		lines[i].t = 1.0f - (lines[i].t * norm);
	}
}
void Renderer::build_line (float2x3 const& chip2world, float2 a, float2 b, int states, lrgba col) {
	auto* out = push_back(line_renderer.lines, 1);

	float radius = abs(((float2x2)chip2world * float2(0.05f)).x);

	{
		float2 p0 = chip2world * a;
		float2 p1 = chip2world * b;

		*out++ = { p0, p1, float2(0, 1), radius, states, col, wire_id };
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

void Renderer::draw_chip (Game& g, Chip* chip, float2x3 const& chip2world, int chip_state, lrgba col) {
	auto& editor = g.editor;

	uint8_t* prev = g.sim.state[g.sim.cur_state^1].data();
	uint8_t* cur  = g.sim.state[g.sim.cur_state  ].data();

	//auto chip_id = ChipInstanceID{ chip, chip_state };
	
	if (is_gate(chip)) {
		auto type = gate_type(chip);
		uint8_t state = chip_state >= 0 ? cur[chip_state] : 1;
			
		draw_gate(chip2world, chip->size, type, state, lrgba(chip->col, 1) * col);
	}
	else {
		{ // TODO: make this look nicer, rounded thick outline? color the background inside chip differently?
			float2 center = chip2world * float2(0);
			float2 size = abs( (float2x2)chip2world * chip->size );
			dbgdraw.wire_quad(float3(center - size*0.5f, 0.0f), size, lrgba(0.001f, 0.001f, 0.001f, 1));
		}
		
		auto draw_part = [&] (Part* part) {
			uint8_t* prev = g.sim.state[g.sim.cur_state^1].data();
			uint8_t* cur  = g.sim.state[g.sim.cur_state  ].data();
		
			auto part2chip = part->pos.calc_matrix();
			auto part2world = chip2world * part2chip;

			auto* sel = g.editor.in_mode<Editor::EditMode>() ?
				&std::get<Editor::EditMode>(g.editor.mode).sel : nullptr;
		
			draw_chip(g, part->chip, part2world, chip_state >= 0 ? chip_state + part->sid : -1, col);
		
			constexpr lrgba line_col = lrgba(0.8f, 0.01f, 0.025f, 1);
				
			for (int i=0; i<(int)part->chip->inputs.size(); ++i) {
				auto& inp = part->chip->inputs[i];
					
				// center position input
				float2 dst0 = part2chip * get_inp_pos(*inp);
				float2 dst1 = part2chip * inp->pos.pos;

				auto& inp_wire = part->inputs[i];
				if (!inp_wire.part) {
					build_line(chip2world, dst0, dst1, 0, line_col);
				}
				else {
					// get connected part
					auto& src_part = *inp_wire.part;
					
					// center position of connected output
					auto smat = src_part.pos.calc_matrix();
					auto& spart = *src_part.chip->outputs[inp_wire.pin];
					float2 src0 = smat * spart.pos.pos;
					float2 src1 = smat * get_out_pos(spart);

					uint8_t prev_state = chip_state >= 0 ? prev[chip_state + src_part.sid + inp_wire.pin] : 1;
					uint8_t  cur_state = chip_state >= 0 ? cur [chip_state + src_part.sid + inp_wire.pin] : 1;
					int state = (prev_state << 1) | cur_state;
					
					build_line(chip2world,
						src0, src1, inp_wire.wire_points, dst0, dst1,
						state, line_col);
				}

				wire_id++;
			}
				
			//for (int i=0; i<(int)part.chip->outputs.size(); ++i) {
			//	{ // TODO: always draw output wire for now
			//		// center position output
			//		auto& spart = part.chip->get_output(i);
			//		float2 dst0 = part2chip * spart.pos.pos;
			//		float2 dst1 = part2chip * get_out_pos(spart);
			//	
			//		uint8_t prev_state = chip_state >= 0 ? prev[chip_state + part.state_idx + i] : 1;
			//		uint8_t  cur_state = chip_state >= 0 ? cur [chip_state + part.state_idx + i] : 1;
			//		int state = (prev_state << 1) | cur_state;
			//	
			//		build_line(chip2world, dst0, dst1, state, col);
			//	}
			//}
		};

		for (auto& part : chip->inputs) {
			draw_part(part.get());
		}
		for (auto& part : chip->outputs) {
			draw_part(part.get());
		}
		for (auto& part : chip->parts) {
			draw_part(part.get());
		}

		if (g.editor.in_mode<Editor::WireMode>()) { // Wire preview
			auto& w = std::get<Editor::WireMode>(g.editor.mode);
			
			if ((w.dst.part || w.src.part) && w.chip.ptr == chip) {
				ZoneScopedN("push wire_preview");
					
				auto& out = w.dir ? w.dst : w.src;
				auto& inp = w.dir ? w.src : w.dst;

				float2 out0 = w.unconn_pos, out1 = w.unconn_pos;
				float2 inp0 = w.unconn_pos, inp1 = w.unconn_pos;
			
				if (out.part) {
					auto  mat  = out.part->pos.calc_matrix();
					auto& part = *out.part->chip->outputs[out.pin];
					out0 = mat * part.pos.pos;
					out1 = mat * get_out_pos(part);
				}
				if (inp.part) {
					auto  mat  = inp.part->pos.calc_matrix();
					auto& part = *inp.part->chip->inputs[inp.pin];
					inp0 = mat * get_inp_pos(part);
					inp1 = mat * part.pos.pos;
				}
			
				build_line(chip2world, out0, out1, w.points, inp0, inp1, 3, lrgba(0.8f, 0.01f, 0.025f, 0.75f));
				
				wire_id++;
			}
		}
	}

}
	
void Renderer::begin (Window& window, Game& g, int2 window_size) {
	dbgdraw.clear();
	text_renderer.begin();

	tri_renderer.update(window.input);
	line_renderer.update(window.input);

	wire_id = 0;
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
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	draw_background();


	{ // Gates and wires
		ZoneScopedN("push gates");
			
		draw_chip(g, g.sim.viewed_chip.get(), float2x3::identity(), 0, lrgba(1));
	}
		
	{ // Gate preview
		if (g.editor.in_mode<Editor::PlaceMode>() && g.editor._cursor_valid) {
			auto& preview = std::get<Editor::PlaceMode>(g.editor.mode).preview_part;

			assert(preview.chip);
			auto part2chip = preview.pos.calc_matrix();

			draw_chip(g, preview.chip, part2chip, -1, lrgba(1,1,1,0.5f));
					
			constexpr lrgba col = lrgba(0.8f, 0.01f, 0.025f, 0.5f);
					
			for (auto& inp : preview.chip->inputs) {
				float2 dst0 = part2chip * get_inp_pos(*inp);
				float2 dst1 = part2chip * inp->pos.pos;

				build_line(float2x3::identity(), dst0, dst1, 0, col);
					
				wire_id++;
			}
		}
	}
		
	line_renderer.render(state, g.sim_t, wire_id);
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
