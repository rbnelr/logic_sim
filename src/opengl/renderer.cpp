#pragma once
#include "common.hpp"
#include "../game.hpp"
#include "../engine/opengl.hpp"
#include "gl_dbgdraw.hpp"
#include "../engine/opengl_text.hpp"

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
		int    states;
		float4 col;

		ATTRIBUTES {
			ATTRIB_INSTANCED( idx++, GL_FLOAT,2, LineInstance, pos0);
			ATTRIB_INSTANCED( idx++, GL_FLOAT,2, LineInstance, pos1);
			ATTRIB_INSTANCED( idx++, GL_FLOAT,2, LineInstance, t);
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
		if (ImGui::Begin("Misc")) {
			if (imgui_Header("Renderer", true)) {

				ImGui::SliderFloat("text_scale", &text_scale, 0.1f, 20);
				text.imgui();

			#if OGL_USE_REVERSE_DEPTH
				ImGui::Checkbox("reverse_depth", &ogl::reverse_depth);
			#endif

				debug_draw.imgui();

				ImGui::PopID();
			}
		}
		ImGui::End();
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
	
	void build_line (float2x3 const& chip2world, float2 start, float2 end, std::vector<float2> const& points, int states, lrgba col) {
		float2 prev = chip2world * end;
		float dist = 0;
		
		size_t count = 1 + points.size();
		auto* lines = push_back(line_renderer.lines, count);

		auto* out = lines;

		// output lines in reverse so that earlier segments appear on top
		// (because I think it looks nicer with my weird outlines)
		for (int i=(int)points.size(); i>=0; --i) {
			float2 cur = chip2world * (i > 0 ? points[i-1] : start);

			float dist0 = dist;
			dist += distance(prev, cur);

			*out++ = { prev, cur, float2(dist0, dist), states, col };

			prev = cur;
		}

		// flip and normalize t to [0,1]
		float norm = 1.0f / dist;
		for (size_t i=0; i<count; ++i) {
			lines[i].t = 1.0f - (lines[i].t * norm);
		}
	}
	

	void draw_gate (float2x3 const& mat, int type, int state, lrgba col) {
		uint16_t idx = (uint16_t)tri_renderer.verticies.size();
		
		auto* pv = push_back(tri_renderer.verticies, 4);
		pv[0] = { mat * float2(-0.5f, -0.5f), float2(0,0), type, state, col };
		pv[1] = { mat * float2(+0.5f, -0.5f), float2(1,0), type, state, col };
		pv[2] = { mat * float2(+0.5f, +0.5f), float2(1,1), type, state, col };
		pv[3] = { mat * float2(-0.5f, +0.5f), float2(0,1), type, state, col };
	
		auto* pi = push_back(tri_renderer.indices, 6);
		ogl::push_quad(pi, idx+0, idx+1, idx+2, idx+3);
	}
	
	void draw_highlight (Game& g, std::string_view name, Placement& pos, float2 size, float2x3 const& chip2world, lrgba col) {
		size  = abs((float2x2)chip2world * size * pos.scale);
		float2 center = chip2world * pos.pos;
		
		g.dbgdraw.wire_quad(float3(center - size*0.5f, 5.0f), size, col);
				
		text.draw_text(name, 20*text_scale, 1,
			TextRenderer::map_text(center + size*float2(-0.5f, 0.5f), g.view), float2(0,1));
	}

	void highlight (Game& g, LogicSim::ChipEditor::Selection& sel, float2x3 const& chip2world, lrgba col) {
		auto& chip = *sel.part->chip;
		if (sel.type == LogicSim::ChipEditor::Selection::PART)
			draw_highlight(g, chip.name, sel.part->pos, chip.size, chip2world, col);
		else if (sel.type == LogicSim::ChipEditor::Selection::PIN_INP)
			draw_highlight(g, chip.inputs[sel.pin].name, chip.inputs[sel.pin].pos,
				LogicSim::PIN_SIZE, sel.part->pos.calc_matrix() * chip2world, col * lrgba(1,1, 0.6f, 1));
		else
			draw_highlight(g, chip.outputs[sel.pin].name, chip.outputs[sel.pin].pos,
				LogicSim::PIN_SIZE, sel.part->pos.calc_matrix() * chip2world, col * lrgba(1,1, 0.6f, 1));
	}

	void draw_chip (Game& g, LogicSim::Chip* chip, float2x3 const& chip2world, int chip_state, lrgba col) {
		auto& editor = g.sim.editor;

		uint8_t* prev = g.sim.state[g.sim.cur_state^1].data();
		uint8_t* cur  = g.sim.state[g.sim.cur_state  ].data();
		
		if (g.sim.is_gate(chip)) {
			auto type = g.sim.gate_type(chip);
			uint8_t state = chip_state >= 0 ? cur[chip_state] : 1;
			
			draw_gate(chip2world, type, state, lrgba(chip->col, 1) * col);
		}
		else {
			float2 pos = chip2world * (-chip->size * 0.5f);
			float2 sz = (float2x2)chip2world * chip->size;
			g.dbgdraw.wire_quad(float3(pos, 0), sz, lrgba(0.9f, 0.9f, 1, 1));

			for (auto& part : chip->parts) {
			
				if (editor.hover && editor.hover.part == &part) highlight(g, editor.hover, chip2world, lrgba(0.25f,0.25f,0.25f,1));
				if (editor.sel   && editor.sel  .part == &part) highlight(g, editor.sel  , chip2world, lrgba(1,1,1,1));
			
				auto part2chip = part.pos.calc_matrix();
				auto part2world = chip2world * part2chip;

				draw_chip(g, part.chip, part2world, chip_state >= 0 ? chip_state + part.state_idx : -1, col);

				for (int i=0; i<(int)part.chip->inputs.size(); ++i) {
					auto& inp = part.inputs[i];
					if (inp.part_idx < 0) continue;

					// get connected part
					auto& src_part = chip->parts[inp.part_idx];
					// center position of connected output
					float2 src_pos = src_part.pos.calc_matrix() * src_part.chip->outputs[inp.pin_idx].pos.pos;
					// center position input
					float2 dst_pos =                  part2chip * part.chip->inputs[i].pos.pos;

					uint8_t prev_state = chip_state >= 0 ? prev[chip_state + src_part.state_idx] : 1;
					uint8_t  cur_state = chip_state >= 0 ? cur [chip_state + src_part.state_idx] : 1;
					int state = (prev_state << 1) | cur_state;
						
					build_line(chip2world, src_pos, dst_pos, inp.wire_points, state, lrgba(0.8f, 0.01f, 0.025f, 1));
				}
			}

			{ // Wire preview
				auto& wire = g.sim.editor.wire_preview;
		
				if ((wire.dst.part || wire.src.part) && wire.chip == chip) {
					ZoneScopedN("push wire_preview");
				
					float2 a = wire.unconn_pos;
					float2 b = wire.unconn_pos;

					if (wire.src.part) a = wire.src.part->pos.calc_matrix() * wire.src.part->chip->outputs[wire.src.pin].pos.pos;
					if (wire.dst.part) b = wire.dst.part->pos.calc_matrix() * wire.dst.part->chip->inputs [wire.dst.pin].pos.pos;
				
					build_line(chip2world, a, b, wire.points, 3, lrgba(0.8f, 0.01f, 0.025f, 0.75f));
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
			
			draw_chip(g, &g.sim.main_chip, float2x3::identity(), 0, lrgba(1));
		}
		
		{ // Gate preview
			auto& preview = g.sim.editor.preview_part;
		
			if (preview.chip) {
				draw_chip(g, preview.chip, preview.pos.calc_matrix(), -1, lrgba(1,1,1,0.5f));
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

}

std::unique_ptr<RendererBackend> make_ogl_renderer () {
	return std::unique_ptr<RendererBackend>( new ogl::Renderer() );
}
