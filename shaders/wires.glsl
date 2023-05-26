#version 430
#include "common.glsl"

struct Vertex {
	vec2 coord;
	float len;
	float radius;
	vec4 col;
};
VS2FS Vertex v;

//flat VS2FS vec4 col_a;
//flat VS2FS vec4 col_b;

uniform float num_wires;

uniform usampler1DArray state_tex;
uniform float sim_t;

#ifdef _VERTEX
	layout(location = 0) in vec2  pos0;
	layout(location = 1) in vec2  pos1;
	layout(location = 2) in float radius;
	layout(location = 3) in int   type;
	layout(location = 4) in int   state_id;
	layout(location = 5) in vec4  col;
	
	vec2 uvs[6] = {
		vec2(1.0, 0.0),
		vec2(1.0, 1.0),
		vec2(0.0, 0.0),
		vec2(0.0, 0.0),
		vec2(1.0, 1.0),
		vec2(0.0, 1.0),
	};
	
	void main () {
		float aa = view.frust_near_size.x * view.inv_viewport_size.x * 1.0; // pixel size in world units
		
		float layer = (type % 2) == 0 ? 0.0 : 1.0;
		bool circle = type >= 2;
		
		vec2 uv = uvs[gl_VertexID % 6];
		vec2 p = uv * 2.0 - 1.0;
		
		//v.t = mix(t.x, t.y, v.coord.x / v.len);
		
		uint prev_state = texelFetch(state_tex, ivec2(state_id, 1), 0).x;
		uint cur_state  = texelFetch(state_tex, ivec2(state_id, 0), 0).x;
		
		//vec4 col_a, col_b;
		//col_a = vec4(col.rgb * vec3(prev_state != 0u ? 1.0 : 0.03), col.a);
		//col_b = vec4(col.rgb * vec3( cur_state != 0u ? 1.0 : 0.03), col.a);
		
		vec4 col0 = vec4(col.rgb * 0.03, col.a);
		vec4 col1 = col;
		
		//vec3 cols = vec3(1.0, 0.98, 0.8);
		//vec3 cols_bg = vec3(0.8, 0.6, 0.04);
		
		vec4 col_bg = vec4(col.rgb * 0.0, col.a);
		
		vec4 col_a, col_b;
		col_a = prev_state != 0u ? col1 : col0;
		col_b =  cur_state != 0u ? col1 : col0;
		
		col_a = mix(col_bg, col_a, layer);
		col_b = mix(col_bg, col_b, layer);
		
		v.col = mix(col_a, col_b, sim_t);
		
		// compute line coord space
		vec2 dir = pos1 - pos0;
		v.len = length(dir);
		dir = v.len > 0.001 ? normalize(dir) : vec2(1.0,0.0);
		
		v.radius = radius + clamp(aa * 3.0, 0.08, 0.16) * (0.5 - layer);
		
		vec2 norm = vec2(-dir.y, dir.x);
		
		float r = v.radius + aa;
		
		vec2 pos;
		if (circle) {
			pos = mix(pos0, pos1, uv.x);
			pos += norm * r * p.y;
			pos += dir * r * p.x;
			v.coord = p * r;
		}
		else {
			pos = mix(pos0, pos1, uv.x);
			pos += norm * r * p.y;
			v.coord = vec2(v.len * uv.x, p.y * r);
		}
		
		gl_Position = view.world2clip * vec4(pos, 0.0, 1.0);
	}
#endif
#ifdef _FRAGMENT
	//uniform float anim_fade = 0.5;
	//
	//float slider_anim (float x) {
	//	//return clamp((x - sim_t) * 4.0 + 0.5, 0.0, 1.0);
	//	//return clamp((x - sim_t) * 4.0 + (1.0 - x), 0.0, 1.0);
	//	
	//	//return smoothstep(0.0, 1.0, (v.t - sim_t) * 4.0 + 0.5);
	//	return smoothstep(0.0, 1.0,
	//		(x - sim_t) * (1.0 + 1.0 / anim_fade) + (1.0 - x));
	//}
	
	void main () {
		float aa = view.frust_near_size.x * view.inv_viewport_size.x * 1.0; // pixel size in world units
		
		// compute end cap circle
		vec2 offs = v.coord - vec2(clamp(v.coord.x, 0.0, v.len), 0.0);
		float r = length(offs) - v.radius;
		
		// compute wire state animation color
		//float t = slider_anim(v.t);
		
		//vec4 col = mix(col_a, col_b, t);
		
		float aa_alpha = map_clamp(r, -aa/2.0, +aa/2.0, 1.0, 0.0);
		
		vec4 col = v.col;
		col.a *= aa_alpha;
		
	//#ifndef _WIREFRAME
	//	if (col.a < 0.001)
	//		discard;
	//#endif
		FRAG_COL(col);
	}
#endif
