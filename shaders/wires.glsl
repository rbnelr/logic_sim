#version 430
#include "common.glsl"

struct Vertex {
	float t;
	vec2 coord;
	float len;
};
VS2FS Vertex v;

flat VS2FS vec4 col_a;
flat VS2FS vec4 col_b;

const float radius = 0.05;

#ifdef _VERTEX
	layout(location = 0) in vec2  pos0;
	layout(location = 1) in vec2  pos1;
	layout(location = 2) in vec2  t;
	layout(location = 3) in int   states;
	layout(location = 4) in vec4  col;
	
	vec2 uvs[6] = {
		vec2(+1.0, -1.0),
		vec2(+1.0, +1.0),
		vec2(-1.0, -1.0),
		vec2(-1.0, -1.0),
		vec2(+1.0, +1.0),
		vec2(-1.0, +1.0),
	};
	
	void main () {
		float aa = view.frust_near_size.x * view.inv_viewport_size.x; // pixel size in world units
		
		vec2 uv = uvs[gl_VertexID % 6];
		
		// compute line coord space
		vec2 dir = pos1 - pos0;
		v.len = length(dir);
		dir = v.len > 0.001 ? normalize(dir) : vec2(1.0,0.0);
		
		vec2 norm = vec2(-dir.y, dir.x);
		
		float r = radius + aa;
		
		// interpolation parameters for actual line outlines etc. in frag shader
		v.coord = vec2(uv.x == -1.0 ? -r : r + v.len, uv.y * r);
		
		v.t = uv.x == -1.0 ? t.x : t.y;
		
		col_a = vec4(col.rgb * vec3((states & 1) != 0 ? 1.0 : 0.008), col.a);
		col_b = vec4(col.rgb * vec3((states & 2) != 0 ? 1.0 : 0.008), col.a);
		
		// line vertices
		vec2 pos = uv.x == -1.0 ? pos0 : pos1;
		pos += norm * r * uv.y;
		pos += dir  * r * uv.x;
		
		gl_Position = view.world2clip * vec4(pos, 0.0, 1.0);
	}
#endif
#ifdef _FRAGMENT
	out vec4 frag_col;
	
	uniform float sim_t;
	
	float slider_anim (float x) {
		//return clamp((x - sim_t) * 4.0 + 0.5, 0.0, 1.0);
		//return clamp((x - sim_t) * 4.0 + (1.0 - x), 0.0, 1.0);
		
		//return smoothstep(0.0, 1.0, (v.t - sim_t) * 4.0 + 0.5);
		return smoothstep(0.0, 1.0, (x - sim_t) * 4.0 + (1.0 - x));
	}
	
	void main () {
		float aa = view.frust_near_size.x * view.inv_viewport_size.x * 1.0; // pixel size in world units
		
		// compute end cap circle
		vec2 offs = v.coord - vec2(clamp(v.coord.x, 0.0, v.len), 0.0);
		float r = length(offs) - radius;
		//float rbox = abs(offs.y) - radius;
		
		// compute wire state animation color
		float t = slider_anim(v.t);
		
		vec4 col = mix(col_a, col_b, t);
		
		float outline = clamp(aa * 3.0, 0.01, 0.02);
		
		float aa_alpha   = map_clamp(r, -aa/2.0, +aa/2.0, 1.0, 0.0);
		float outl_alpha = map_clamp(r + outline, -aa/2.0, +aa/2.0, 1.0, 0.2);
		
		col.rgb *= outl_alpha * 0.99;
		col.a *= aa_alpha;
		
		frag_col = col;
	}
#endif
