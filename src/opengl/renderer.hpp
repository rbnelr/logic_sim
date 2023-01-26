#pragma once
#include "common.hpp"
#include "../engine/opengl.hpp"
#include "../engine/opengl_text.hpp"
#include "gl_dbgdraw.hpp"

struct App;
namespace logic_sim {
	struct Chip;
}

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
	Shader* shad = g_shaders.compile("wires");

	struct LineInstance {
		float2 pos0;
		float2 pos1;
		float  radius;
		int    type; // 0: wire segment BG  1: wire segment   2: wire node BG  3: wire node
		int    states;
		float4 col;

		ATTRIBUTES {
			ATTRIB_INSTANCED( idx++, GL_FLOAT,2, LineInstance, pos0);
			ATTRIB_INSTANCED( idx++, GL_FLOAT,2, LineInstance, pos1);
			ATTRIB_INSTANCED( idx++, GL_FLOAT,1, LineInstance, radius);
			ATTRIBI_INSTANCED(idx++, GL_INT  ,1, LineInstance, type);
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

	void render (StateManager& state, float sim_t) {
		ZoneScoped;

		if (shad->prog) {
			OGL_TRACE("WireRenderer");

			vbo_lines.stream(lines);

			if (lines.size() > 0) {
				glUseProgram(shad->prog);

				shad->set_uniform("sim_t", sim_t);

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

struct ScreenOutline {
	Shader* shad = g_shaders.compile("screen_outline");
	
	struct Vertex {
		float2 pos;

		ATTRIBUTES {
			ATTRIB(idx++, GL_FLOAT,2, Vertex, pos);
		}
	};
	VertexBuffer vbo = vertex_buffer<Vertex>("ScreenOutline.Vertex");
	
	static constexpr Vertex verts[] = {
		{ float2(-1,-1) },
		{ float2(+1,-1) },
		
		{ float2(+1,-1) },
		{ float2(+1,+1) },
		
		{ float2(+1,+1) },
		{ float2(-1,+1) },
		
		{ float2(-1,+1) },
		{ float2(-1,-1) },
	};
	ScreenOutline () {
		vbo.upload(verts, ARRLEN(verts));
	}

	void draw (StateManager& state, lrgba col) {
		ZoneScoped;
		OGL_TRACE("draw_paused_outline");

		glUseProgram(shad->prog);

		shad->set_uniform("col", col);

		PipelineState s;
		s.depth_write = false;
		s.depth_test = false;
		s.blend_enable = true;
		s.poly_mode = POLY_LINE;
		state.set_no_override(s);

		glLineWidth(6); // half is off-screen

		{
			glBindVertexArray(vbo.vao);
			glDrawArrays(GL_LINES, 0, ARRLEN(verts));
		}
	}
};


// Group of line segments and points that are drawn such that they appear connected (outlines appear behind all of the shapes)
struct LineGroup {
	std::vector<LineRenderer::LineInstance> lines;

	LineGroup () {
		lines.reserve(1024);
	}
		
	void draw_wire_segment (float2x3 const& chip2world, float2 a, float2 b, int states, lrgba col);
	void draw_wire_point (float2x3 const& chip2world, float2 pos, float radius, int num_wires, int states, lrgba col);
};

void add_line_group (LineRenderer& lines, LineGroup& group);


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

	DebugDraw dbgdraw;
	glDebugDraw gl_dbgdraw = { 2 };

	TextRenderer text_renderer = TextRenderer("fonts/AsimovExtraWide-veG4.ttf", 64, true);
	
	TriRenderer tri_renderer;
	LineRenderer line_renderer;

	Vao dummy_vao = {"dummy_vao"};

	Shader* shad_background  = g_shaders.compile("background");

	float text_scale = 1.0f;
	

	View3D view;

	void imgui (Input& I) {
		if (imgui_Header("Renderer", false)) {

			ImGui::SliderFloat("text_scale", &text_scale, 0.1f, 20);
			text_renderer.imgui();

		#if OGL_USE_REVERSE_DEPTH
			ImGui::Checkbox("reverse_depth", &ogl::reverse_depth);
		#endif

			gl_dbgdraw.imgui();

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
	
	ScreenOutline screen_outline;

	// clamp font size in world-space units
	float clamp_font_size (float size, float min, float max) {
		float scale = view.frust_near_size.y / view.viewport_size.y;

		float x = size * scale;
		x = clamp(x, min, max == 0.0f ? INF : max);
		return x / scale;
	}

	void draw_quad (float2x3 const& mat, float2 size, float4 col) {
		constexpr int indices[] = { 0,1,  1,2,  2,3,  3,0, };
		constexpr float2 vertices[] = {
			float2(-0.5f, -0.5f),
			float2(+0.5f, -0.5f),
			float2(+0.5f, +0.5f),
			float2(-0.5f, +0.5f),
		};

		auto* out = kiss::push_back(dbgdraw.lines, ARRLEN(indices));

		for (auto& idx : indices) {
			auto& v = vertices[idx];

			float2 pos = mat * (v * size);
			
			out->pos.x = pos.x;
			out->pos.y = pos.y;
			out->pos.z = 0;

			out->col = col;

			out++;
		}
	}

	void draw_text (std::string_view text, float2 pos, float font_size, lrgba col,
			float2 align=float2(0,1), float max_sz=0) {
		float sz = clamp_font_size(font_size*text_scale, 0.15f, max_sz);
		text_renderer.draw_text(text, sz, col, TextRenderer::map_text(pos, view), align);
	}
	
	void draw_highlight_box (float2 size, float2x3 const& mat, lrgba col) {
		//size  = abs((float2x2)mat * size);
		//float2 center = mat * float2(0);
		//
		//dbgdraw.wire_quad(float3(center - size*0.5f, 5.0f), size, col);

		draw_quad(mat, size, col);
	}
	void draw_highlight_text (float2 size, float2x3 const& mat, std::string_view name, float font_size, lrgba col=1,
			float2 align=float2(0,1)) {
		size  = abs((float2x2)mat * size);
		float2 center = mat * float2(0);
		
		draw_text(name, center + size*(align - 0.5f), font_size, col, align);
	}

	void draw_gate (float2x3 const& mat, float2 size, int type, int state, lrgba col);
	
	void draw_chip (App& app, logic_sim::Chip* chip, float2x3 const& chip2world, int sim_idx, lrgba col);
	
	void begin (Window& window, App& app, int2 window_size);
	void end (Window& window, App& app, int2 window_size);
};

} // namespace ogl
