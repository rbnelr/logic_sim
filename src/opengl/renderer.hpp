#pragma once
#include "common.hpp"
#include "game.hpp"
#include "engine/opengl.hpp"
#include "gl_dbgdraw.hpp"
#include "engine/opengl_text.hpp"

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

	void push_gate (float2 pos, float2 size, GateType type, bool state, float4 col) {
		uint16_t idx = (uint16_t)verticies.size();

		auto* pv = push_back(verticies, 4);
		pv[0] = { pos + float2(     0,      0), float2(0,0), type, (int)state, col };
		pv[1] = { pos + float2(size.x,      0), float2(1,0), type, (int)state, col };
		pv[2] = { pos + float2(size.x, size.y), float2(1,1), type, (int)state, col };
		pv[3] = { pos + float2(     0, size.y), float2(0,1), type, (int)state, col };

		auto* pi = push_back(indices, 6);
		ogl::push_quad(pi, idx+0, idx+1, idx+2, idx+3);
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

	struct Vertex {
		float2 pos;
		float4 col;
		float  t;
		int    states;

		ATTRIBUTES {
			ATTRIB( idx++, GL_FLOAT,2, Vertex, pos);
			ATTRIB( idx++, GL_FLOAT,4, Vertex, col);
			ATTRIB( idx++, GL_FLOAT,1, Vertex, t);
			ATTRIBI(idx++, GL_INT  ,1, Vertex, states);
		}
	};

	VertexBuffer vbo_lines = vertex_buffer<Vertex>("LineRenderer.Vertex");

	std::vector<Vertex>   verticies;

	void update (Input& I) {
		verticies.clear();
		verticies.shrink_to_fit();
	}

	void render (StateManager& state, Game& g) {
		ZoneScoped;

		if (shad->prog) {
			OGL_TRACE("LineRenderer");

			vbo_lines.stream(verticies);

			if (verticies.size() > 0) {
				glUseProgram(shad->prog);

				shad->set_uniform("sim_anim_t", g.sim_anim_t);

				PipelineState s;
				s.depth_test = false;
				s.depth_write = false;
				s.blend_enable = true;
				state.set(s);

				glBindVertexArray(vbo_lines.vao);
				glDrawArrays(GL_LINES, 0, (GLsizei)verticies.size());
			}
		}

		glBindVertexArray(0);
	}
};

struct Renderer {
	SERIALIZE_NONE(Renderer)
	
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

	TextRenderer text = TextRenderer("fonts/DroidSerif-WmoY.ttf", 64, true);
	
	TriRenderer tri_renderer;
	LineRenderer line_renderer;

	Vao dummy_vao = {"dummy_vao"};

	Shader* shad_background  = g_shaders.compile("background");

	float text_scale = 1.0f;
	
	void imgui (Input& I) {
		if (ImGui::Begin("Misc")) {
			if (imgui_Header("Renderer", true)) {

				ImGui::SliderFloat("text_scale", &text_scale, 0.1f, 20);

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
	
	void highlight (LogicSim::Selection s, lrgba col, Game& g) {
		auto& gate = *s.gate;
		if (s.type == LogicSim::Selection::GATE) {
			g.dbgdraw.wire_quad(float3(gate.pos, 5.0f), 1, col);

			text.draw_text(gate_info[gate.type].name, 24*text_scale, lrgba(1),
				TextRenderer::map_text(gate.pos + float2(0, 1.0f), g.view), float2(0,1));
		}
		else if (s.type == LogicSim::Selection::INPUT) {
			g.dbgdraw.wire_quad(float3(gate.get_input_pos(s.io_idx) - LogicSim::IO_SIZE/2, 5.0f), LogicSim::IO_SIZE, col);
		}
		else {
			g.dbgdraw.wire_quad(float3(gate.get_output_pos(s.io_idx) - LogicSim::IO_SIZE/2, 5.0f), LogicSim::IO_SIZE, col);
		}
	}

	void build_line (float2 start, float2 end, std::vector<float2>& points, lrgba col, int states) {
		float2 prev = start;
		float dist = 0;
		
		size_t idx = line_renderer.verticies.size();

		for (float2& cur : points) {
			auto* pv = push_back(line_renderer.verticies, 2);
			pv[0] = { prev, col, dist, states };
			dist += distance(prev, cur);
			pv[1] = {  cur, col, dist, states };

			prev = cur;
		}
		{
			auto* pv = push_back(line_renderer.verticies, 2);
			pv[0] = { prev, col, dist, states };
			dist += distance(prev, end);
			pv[1] = {  end, col, dist, states };
		}

		// normalize t to [0,1]
		float norm = 1.0f / dist;
		for (size_t i=idx; i<line_renderer.verticies.size(); ++i) {
			line_renderer.verticies[i].t *= norm;
		}
	}

	void render (Window& window, Game& g, int2 window_size) {
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

			uint8_t prev_smask = 1u << (g.sim.gates.cur_buf^1);
			uint8_t cur_smask  = 1u <<  g.sim.gates.cur_buf;

			for (int i=0; i<(int)g.sim.gates.size(); ++i) {
				auto& gate = g.sim.gates[i];
				auto state = (gate.state & cur_smask) != 0;
				tri_renderer.push_gate(gate.pos, 1, gate.type, state, gate_info[gate.type].color);

				int count = gate_info[gate.type].inputs;
				for (int i=0; i<count; ++i) {
					auto& wire = gate.inputs[i];
					if (wire.gate) {
						float2 a = wire.gate->get_output_pos(wire.io_idx);
						float2 b = gate.get_input_pos(i);
						
						bool sa = (wire.gate->state & cur_smask) != 0;
						bool sb = (wire.gate->state & prev_smask) != 0;

						int states = (sa?1:0) | (sb?2:0);

						build_line(a, b, wire.points, lrgba(0.8f, 0.01f, 0.025f, 1), states);
					}
				}
			}
		}

		{ // Gate preview
			auto& place = g.sim.gate_preview;

			if (place.type > GT_NULL) {
				ZoneScopedN("push gate_preview");

				auto col = gate_info[place.type].color;
				col.w *= 0.5f;
				tri_renderer.push_gate(place.pos, 1, place.type, place.state, col);
			}
		}
		{ // Wire preview
			auto& wire = g.sim.wire_preview;

			if (wire.dst.gate || wire.src.gate) {
				ZoneScopedN("push wire_preview");

				float2 a = wire.unconnected_pos;
				float2 b = wire.unconnected_pos;

				if (wire.src.gate) a = wire.src.gate->get_output_pos(wire.src.io_idx);
				if (wire.dst.gate) b = wire.dst.gate->get_input_pos (wire.dst.io_idx);
				
				build_line(a, b, wire.points, lrgba(0.8f, 0.01f, 0.025f, 0.75f), 3);
			}
		}
		
		glLineWidth(10);
		line_renderer.render(state, g);
		glLineWidth(debug_draw.line_width);

		tri_renderer.render(state);


		if (g.sim.hover) highlight(g.sim.hover, lrgba(0.25f,0.25f,0.25f,1), g);
		if (g.sim.sel  ) highlight(g.sim.sel  , lrgba(1,1,1,1), g);

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