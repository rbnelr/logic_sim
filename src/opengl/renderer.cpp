#pragma once
#include "common.hpp"
#include "../game.hpp"
#include "../engine/opengl.hpp"
#include "gl_dbgdraw.hpp"
#include "../engine/opengl_text.hpp"

using namespace logic_sim;

namespace ogl {

struct TriRenderer {
	Shader* shad  = g_shaders.compile("gates");

	struct Vertex {
		float2 pos;
		float2 uv;
		int    gate_type;
		int    gate_state;
		float4 col;

		ATTRIBUTES {
			ATTRIB( idx++, GL_FLOAT,2, Vertex, pos);
			ATTRIB( idx++, GL_FLOAT,2, Vertex, uv);
			ATTRIBI(idx++, GL_INT,  1, Vertex, gate_type);
			ATTRIBI(idx++, GL_INT,  1, Vertex, gate_state);
			ATTRIB( idx++, GL_FLOAT,4, Vertex, col);
		}
	};

	VertexBufferI vbo_tris = vertex_bufferI<Vertex>("TriRenderer.Vertex");

	std::vector<Vertex>   verticies;
	std::vector<uint16_t> indices;

	void update (Input& I) {
		verticies.clear();
		verticies.shrink_to_fit();
		indices  .clear();
		indices  .shrink_to_fit();
	}

	void render (StateManager& state) {
		ZoneScoped;

		if (shad->prog) {
			OGL_TRACE("TriRenderer");

			vbo_tris.stream(verticies, indices);

			if (indices.size() > 0) {
				glUseProgram(shad->prog);

				PipelineState s;
				s.depth_test = false;
				s.depth_write = false;
				s.blend_enable = true;
				s.cull_face = false;
				state.set(s);

				glBindVertexArray(vbo_tris.vao);
				glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_SHORT, (void*)0);
			}
		}

		glBindVertexArray(0);
	}
};
struct LineRenderer {
	Shader* shad  = g_shaders.compile("wires");

	struct LineInstance {
		float2 pos0;
		float2 pos1;
		float2 t; // t0 t1
		float  radius;
		int    states;
		float4 col;

		ATTRIBUTES {
			ATTRIB_INSTANCED( idx++, GL_FLOAT,2, LineInstance, pos0);
			ATTRIB_INSTANCED( idx++, GL_FLOAT,2, LineInstance, pos1);
			ATTRIB_INSTANCED( idx++, GL_FLOAT,2, LineInstance, t);
			ATTRIB_INSTANCED( idx++, GL_FLOAT,1, LineInstance, radius);
			ATTRIBI_INSTANCED(idx++, GL_INT  ,1, LineInstance, states);
			ATTRIB_INSTANCED( idx++, GL_FLOAT,4, LineInstance, col);
		}
	};

	VertexBuffer vbo_lines = vertex_buffer<LineInstance>("LineRenderer.LineInstance");

	std::vector<LineInstance> lines;

	void update (Input& I) {
		lines.clear();
		lines.shrink_to_fit();
	}

	void render (StateManager& state, Game& g) {
		ZoneScoped;

		if (shad->prog) {
			OGL_TRACE("LineRenderer");

			vbo_lines.stream(lines);

			if (lines.size() > 0) {
				glUseProgram(shad->prog);

				shad->set_uniform("sim_t", g.sim_t);

				PipelineState s;
				s.depth_test = false;
				s.depth_write = false;
				s.blend_enable = true;
				s.cull_face = false;
				state.set(s);

				glBindVertexArray(vbo_lines.vao);
				glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)lines.size());
			}
		}

		glBindVertexArray(0);
	}
};

struct Renderer : public RendererBackend {
	virtual void to_json(nlohmann::ordered_json& j) const {}
	virtual void from_json(const nlohmann::ordered_json& j) {}
	
	virtual ~Renderer() {}

	StateManager state;
	
	struct CommonUniforms {
		static constexpr int UBO_BINDING = 0;

		Ubo ubo = {"common_ubo"};

		struct Common {
			View3D view;
		};

		void set (View3D const& view) {
			Common common = {};
			common.view = view;
			stream_buffer(GL_UNIFORM_BUFFER, ubo, sizeof(common), &common, GL_STREAM_DRAW);

			glBindBuffer(GL_UNIFORM_BUFFER, ubo);
			glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING, ubo);
			glBindBuffer(GL_UNIFORM_BUFFER, 0);
		}
	};
	CommonUniforms common_ubo;

	glDebugDraw debug_draw;

	TextRenderer text = TextRenderer("fonts/AsimovExtraWide-veG4.ttf", 64, true);
	
	TriRenderer tri_renderer;
	LineRenderer line_renderer;

	Vao dummy_vao = {"dummy_vao"};

	Shader* shad_background  = g_shaders.compile("background");

	float text_scale = 1.0f;
	
	virtual void imgui (Input& I) {
		if (imgui_Header("Renderer", false)) {

			ImGui::SliderFloat("text_scale", &text_scale, 0.1f, 20);
			text.imgui();

		#if OGL_USE_REVERSE_DEPTH
			ImGui::Checkbox("reverse_depth", &ogl::reverse_depth);
		#endif

			debug_draw.imgui();

			ImGui::PopID();
		}
	}

	void draw_background () {
		glUseProgram(shad_background->prog);

		// draw_fullscreen_triangle
		PipelineState s;
		s.depth_test  = false;
		s.depth_write  = false;
		s.blend_enable = false;
		state.set_no_override(s);

		glBindVertexArray(dummy_vao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}

	Renderer () {
		
	}
	
	void build_line (float2x3 const& chip2world,
			float2 start0, float2 start1, std::vector<float2> const& points, float2 end0, float2 end1,
			int states, lrgba col) {
		float2 prev = chip2world * end1;
		float dist = 0;
		
		size_t count = 3 + points.size();
		auto* lines = push_back(line_renderer.lines, count);

		auto* out = lines;

		float radius = abs(((float2x2)chip2world * float2(0.06f)).x);

		auto line_seg = [&] (float2 p) {
			float2 cur = chip2world * p;

			float dist0 = dist;
			dist += distance(prev, cur);

			*out++ = { prev, cur, float2(dist0, dist), radius, states, col };

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
	void build_line (float2x3 const& chip2world, float2 a, float2 b, int states, lrgba col) {
		
		auto* out = push_back(line_renderer.lines, 1);

		float radius = abs(((float2x2)chip2world * float2(0.06f)).x);

		{
			float2 p0 = chip2world * a;
			float2 p1 = chip2world * b;

			*out++ = { p0, p1, float2(0, 1), radius, states, col };
		}
	}

	void draw_gate (float2x3 const& mat, float2 size, int type, int state, lrgba col) {
		if (type < 2)
			return; // TEST: don't draw INP/OUT_PINs

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
	
	void draw_highlight (Game& g, std::string_view name, float2 size, float2x3 const& mat, lrgba col, bool draw_text) {
		size  = abs((float2x2)mat * size);
		float2 center = mat * float2(0);
		
		g.dbgdraw.wire_quad(float3(center - size*0.5f, 5.0f), size, col);
		
		if (draw_text)
			text.draw_text(name, 20*text_scale, 1,
				TextRenderer::map_text(center + size*float2(-0.5f, 0.5f), g.view), float2(0,1));
	}
	
	void highlight (Game& g, Part* part, Chip* chip, float2x3 const& chip2world, lrgba col, bool draw_text=true) {
		std::string_view name = part->chip->name;
		if (part->chip == &gates[INP_PIN])
			name = chip->inputs[indexof_part(chip->parts, part) - (int)chip->outputs.size()].name;
		if (part->chip == &gates[OUT_PIN])
			name = chip->outputs[indexof_part(chip->parts, part)].name;

		auto mat = chip2world * part->pos.calc_matrix();
		
		draw_highlight(g, name, part->chip->size, mat, col, draw_text);
	}
	void highlight (Game& g, Editor::Hover& h, float2x3 const& chip2world, lrgba col, bool draw_text=true) {
		if (h.type == Editor::Hover::PART) {
			highlight(g, h.part, h.chip, chip2world, col, draw_text);
		}
		else {
			
			bool inp = h.type == Editor::Hover::PIN_INP;
			auto& io = inp ? h.part->chip->inputs [h.pin] :
			                 h.part->chip->outputs[h.pin];
			
			std::string_view name = "<unnamed io>"; // NOTE: ternary operator and implicit casting is bug prone -_-
			if (!io.name.empty()) name = io.name;

			auto pos = inp ?
				get_inp_pos(h.part->chip->get_input(h.pin)) :
				get_out_pos(h.part->chip->get_output(h.pin));
			
			auto mat = chip2world * h.part->pos.calc_matrix();
			
			draw_highlight(g, name, PIN_SIZE, mat * translate(pos), col * lrgba(1,1, 0.6f, 1), draw_text);
		}
	}

	void draw_chip (Game& g, Chip* chip, float2x3 const& chip2world, int chip_state, lrgba col) {
		auto& editor = g.editor;

		uint8_t* prev = g.sim.state[g.sim.cur_state^1].data();
		uint8_t* cur  = g.sim.state[g.sim.cur_state  ].data();
		
		if (is_gate(chip)) {
			auto type = gate_type(chip);
			uint8_t state = chip_state >= 0 ? cur[chip_state] : 1;
			
			draw_gate(chip2world, chip->size, type, state, lrgba(chip->col, 1) * col);
		}
		else {
			{
				float2 center = chip2world * float2(0);
				float2 size = abs( (float2x2)chip2world * chip->size );
				g.dbgdraw.wire_quad(float3(center - size*0.5f, 0.0f), size, lrgba(0.9f, 0.9f, 0.3f, 1));
			}

			for (auto& part : chip->parts) {
				
				auto part2chip = part.pos.calc_matrix();
				auto part2world = chip2world * part2chip;

				if (g.editor.hover.is_part(&part)) {
					highlight(g, g.editor.hover, chip2world, lrgba(0.25f,0.25f,0.25f,1));
				}
				if (g.editor.in_mode<Editor::EditMode>()) {
					auto& e = std::get<Editor::EditMode>(g.editor.mode);
					
					if (e.sel.has_part(chip, &part))
						highlight(g, &part, chip, chip2world, lrgba(1,1,1,1),
							(int)e.sel.items.size() == 1); // only show text for single-part selections as to not spam too much text
				}

				draw_chip(g, part.chip, part2world, chip_state >= 0 ? chip_state + part.state_idx : -1, col);

				constexpr lrgba col = lrgba(0.8f, 0.01f, 0.025f, 1);

				for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
					
					// center position input
					auto& dpart = part.chip->get_input(i);
					float2 dst0 = part2chip * get_inp_pos(dpart);
					float2 dst1 = part2chip * dpart.pos.pos;

					auto& inp = part.inputs[i];
					if (inp.part_idx < 0) {
						
						auto& dpart = part.chip->get_input(i);
						float2 dst0 = part2chip * get_inp_pos(dpart);
						float2 dst1 = part2chip * dpart.pos.pos;

						build_line(chip2world, dst0, dst1, 0, col);
					}
					else {
						// get connected part
						auto& src_part = chip->parts[inp.part_idx];
					
						// center position of connected output
						auto smat = src_part.pos.calc_matrix();
						auto& spart = src_part.chip->get_output(inp.pin_idx);
						float2 src0 = smat * spart.pos.pos;
						float2 src1 = smat * get_out_pos(spart);

						uint8_t prev_state = chip_state >= 0 ? prev[chip_state + src_part.state_idx + inp.pin_idx] : 1;
						uint8_t  cur_state = chip_state >= 0 ? cur [chip_state + src_part.state_idx + inp.pin_idx] : 1;
						int state = (prev_state << 1) | cur_state;
					
						build_line(chip2world,
							src0, src1, inp.wire_points, dst0, dst1,
							state, col);
					}
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
			}

			if (g.editor.in_mode<Editor::WireMode>()) { // Wire preview
				auto& w = std::get<Editor::WireMode>(g.editor.mode);
			
				if ((w.dst.part || w.src.part) && w.chip == chip) {
					ZoneScopedN("push wire_preview");
					
					auto& out = w.dir ? w.dst : w.src;
					auto& inp = w.dir ? w.src : w.dst;

					float2 out0 = w.unconn_pos, out1 = w.unconn_pos;
					float2 inp0 = w.unconn_pos, inp1 = w.unconn_pos;
			
					if (out.part) {
						auto  mat  = out.part->pos.calc_matrix();
						auto& part = out.part->chip->get_output(out.pin);
						out0 = mat * part.pos.pos;
						out1 = mat * get_out_pos(part);
					}
					if (inp.part) {
						auto  mat  = inp.part->pos.calc_matrix();
						auto& part = inp.part->chip->get_input(inp.pin);
						inp0 = mat * get_inp_pos(part);
						inp1 = mat * part.pos.pos;
					}
			
					build_line(chip2world, out0, out1, w.points, inp0, inp1, 3, lrgba(0.8f, 0.01f, 0.025f, 0.75f));
				}
			}
		}

	}

	virtual void render (Window& window, Game& g, int2 window_size) {
		ZoneScoped;
		
		{
			OGL_TRACE("setup");

			{
				//OGL_TRACE("set state defaults");

				state.wireframe          = debug_draw.wireframe;
				state.wireframe_no_cull  = debug_draw.wireframe_no_cull;
				state.wireframe_no_blend = debug_draw.wireframe_no_blend;

				state.set_default();

				glEnable(GL_LINE_SMOOTH);
				glLineWidth(debug_draw.line_width);
			}

			{
				common_ubo.set(g.view);

				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, debug_draw.indirect_vbo);

				debug_draw.update(window.input);
			}
		}
		
		{
			glViewport(0,0, window.input.window_size.x, window.input.window_size.y);
			glClearColor(0.01f, 0.012f, 0.014f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		
		text.begin();

		draw_background();

		tri_renderer.update(window.input);
		line_renderer.update(window.input);

		{ // Gates and wires
			ZoneScopedN("push gates");
			
			draw_chip(g, g.sim.viewed_chip.get(), float2x3::identity(), 0, lrgba(1));
		}
		
		{ // Gate preview
			if (g.editor.in_mode<Editor::PlaceMode>()) {
				auto& preview = std::get<Editor::PlaceMode>(g.editor.mode).preview_part;
				if (preview.chip && g.editor._cursor_valid) {
					auto part2chip = preview.pos.calc_matrix();

					draw_chip(g, preview.chip, part2chip, -1, lrgba(1,1,1,0.5f));
					
					constexpr lrgba col = lrgba(0.8f, 0.01f, 0.025f, 0.5f);
					
					for (int i=0; i<(int)preview.chip->inputs.size(); ++i) {
						auto& dpart = preview.chip->get_input(i);
						float2 dst0 = part2chip * get_inp_pos(dpart);
						float2 dst1 = part2chip * dpart.pos.pos;

						build_line(float2x3::identity(), dst0, dst1, 0, col);
					}
				}
			}
		}
		
		line_renderer.render(state, g);
		tri_renderer.render(state);

		debug_draw.render(state, g.dbgdraw);
		
		text.render(state);

		{
			OGL_TRACE("draw ui");
		
			if (window.trigger_screenshot && !window.screenshot_hud) take_screenshot(window_size);
		
			// draw HUD

			window.draw_imgui();

			if (window.trigger_screenshot && window.screenshot_hud)  take_screenshot(window_size);
			window.trigger_screenshot = false;
		}
	}
};

} // namespace ogl

std::unique_ptr<RendererBackend> make_ogl_renderer () {
	return std::unique_ptr<RendererBackend>( new ogl::Renderer() );
}
