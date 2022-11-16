#pragma once
#include "common.hpp"
#include "game.hpp"
#include "engine/opengl.hpp"
#include "gl_dbgdraw.hpp"

namespace ogl {

struct TriRenderer {
	Shader* shad  = g_shaders.compile("gates");

	struct Vertex {
		float2 pos;
		float2 uv;
		int    gate_type;
		float4 col;

		ATTRIBUTES {
			ATTRIB( idx++, GL_FLOAT,2, Vertex, pos);
			ATTRIB( idx++, GL_FLOAT,2, Vertex, uv);
			ATTRIBI(idx++, GL_INT,  1, Vertex, gate_type);
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

	void push_gate (float2 pos, float2 size, GateType type, float4 col) {
		uint16_t idx = (uint16_t)verticies.size();

		auto* pv = push_back(verticies, 4);
		pv[0] = { pos + float2(     0,      0), float2(0,0), type, col };
		pv[1] = { pos + float2(size.x,      0), float2(1,0), type, col };
		pv[2] = { pos + float2(size.x, size.y), float2(1,1), type, col };
		pv[3] = { pos + float2(     0, size.y), float2(0,1), type, col };

		auto* pi = push_back(indices, 6);
		ogl::push_quad(pi, idx+0, idx+1, idx+2, idx+3);
	}

	void render (StateManager& state) {
		OGL_TRACE("TriRenderer");

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

struct Renderer {
	SERIALIZE_NONE(Renderer)
	
	void imgui (Input& I) {
		if (ImGui::Begin("Misc")) {
			if (imgui_Header("Renderer", true)) {

			#if OGL_USE_REVERSE_DEPTH
				ImGui::Checkbox("reverse_depth", &ogl::reverse_depth);
			#endif

				debug_draw.imgui();

				ImGui::PopID();
			}
		}
		ImGui::End();
	}

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
	
	TriRenderer tri_renderer;

	Vao dummy_vao = {"dummy_vao"};

	Shader* shad_background  = g_shaders.compile("background");

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

		draw_background();

		tri_renderer.update(window.input);
		
		{ // Gate to place preview
			auto& place = g.sim.gate_to_place;

			if (place.type > GT_NULL) {
				auto col = gate_info[place.type].color;
				col.w *= 0.5f;
				tri_renderer.push_gate(place.pos, 1, place.type, col);
			}
		}
		{ // Render real gates
			for (int i=0; i<(int)g.sim.gates.size(); ++i) {
				auto& gate = g.sim.gates[i];
				tri_renderer.push_gate(gate.pos, 1, gate.type, gate_info[gate.type].color);
			}
		}

		//tri_renderer.push_gate(float2( 0, 0), 1,    GT_NOT,  float4(   0,    0,    1,1));
		//
		//tri_renderer.push_gate(float2( 0, 1), 1,    GT_AND,  float4(   1,    0,    0,1));
		//tri_renderer.push_gate(float2( 2, 1), 1,    GT_NAND, float4(   1, 0.5f,    0,1));
		//
		//tri_renderer.push_gate(float2( 0, 2), 1,    GT_OR,   float4(   0,    1,    0,1));
		//tri_renderer.push_gate(float2( 2, 2), 1,    GT_NOR,  float4(0.5f,    1,    0,1));
		//
		//tri_renderer.push_gate(float2( 0, 3), 1,    GT_XOR,  float4(   0,    1, 0.5f,1));
		//
		//tri_renderer.push_gate(float2( 0,10), 5.5f, GT_OR,   float4(   0,    1,    0,1));
		//tri_renderer.push_gate(float2(-3, 5), 1.7f, GT_OR,   float4(   0,    1,    0,1));

		tri_renderer.render(state);

		debug_draw.render(state, g.dbgdraw);

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