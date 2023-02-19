#version 430
#include "common.glsl"

struct Vertex {
	vec2 pos_world;
};
VS2FS Vertex v;

#ifdef _VERTEX
	layout(location = 0) out vec2 vs_uv;

	void main () {
		// same as fullscreen triangle
		// 2
		// | \
		// |  \
		// 0---1
		vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1);
		
		// triangle covers [-1, 3]
		// such that the result is a quad that fully covers [-1,+1]
		vec4 clip = vec4(p * 4.0 - 1.0, 0.0, 1.0);
		gl_Position = clip;
		
		v.pos_world = (view.clip2world * clip).xy;
	}
#endif
#ifdef _FRAGMENT
	out vec4 frag_col;
	void main () {
		vec2 xy = fract(v.pos_world * 0.125);
		
		vec3 col = vec3(0.05, 0.06, 0.07);
		if ((xy.x > 0.5) != (xy.y > 0.5))
			col *= 1.2;
		
		frag_col = vec4(col, 1.0);
	}
#endif
