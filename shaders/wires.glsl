#version 430
#include "common.glsl"

struct Vertex {
	float t;
	vec4 col_a;
	vec4 col_b;
};
VS2FS Vertex v;

#ifdef _VERTEX
	layout(location = 0) in vec2  pos;
	layout(location = 1) in vec4  col;
	layout(location = 2) in float t;
	layout(location = 3) in int   states;
	
	void main () {
		gl_Position = view.world2clip * vec4(pos, 0.0, 1.0);
		v.t = t;
		v.col_a = vec4(col.rgb * vec3((states & 1) != 0 ? 1:0), col.a);
		v.col_b = vec4(col.rgb * vec3((states & 2) != 0 ? 1:0), col.a);
	}
#endif
#ifdef _FRAGMENT
	out vec4 frag_col;
	
	uniform float sim_anim_t;
	
	void main () {
		frag_col = mix(v.col_a, v.col_b,
			clamp((v.t - sim_anim_t) * 10.0, 0.0, 1.0));
	}
#endif
