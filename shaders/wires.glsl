#version 430
#include "common.glsl"

struct Vertex {
	float t;
	vec2 coord;
	float len;
	float radius;
};
VS2FS Vertex v;

flat VS2FS vec4 col_a;
flat VS2FS vec4 col_b;

uniform float num_wires;

#ifdef _VERTEX
	layout(location = 0) in vec2  pos0;
	layout(location = 1) in vec2  pos1;
	layout(location = 2) in vec2  t;
	layout(location = 3) in float radius;
	layout(location = 4) in int   states;
	layout(location = 5) in vec4  col;
	layout(location = 6) in int   wire_id;
	
	vec2 uvs[6] = {
		vec2(+1.0, -1.0),
		vec2(+1.0, +1.0),
		vec2(-1.0, -1.0),
		vec2(-1.0, -1.0),
		vec2(+1.0, +1.0),
		vec2(-1.0, +1.0),
	};
	
	void main () {
		float aa = view.frust_near_size.x * view.inv_viewport_size.x * 1.0; // pixel size in world units
		
		// 0 = outline  1 = wire
		float layer   = float((gl_VertexID / 6) % 2);
		float line_id = float(gl_VertexID / 12);
		vec2 uv = uvs[gl_VertexID % 6];
		
		// compute line coord space
		vec2 dir = pos1 - pos0;
		v.len = length(dir);
		dir = v.len > 0.001 ? normalize(dir) : vec2(1.0,0.0);
		
		v.radius = radius + clamp(aa * 3.0, 0.01, 0.02) * (0.5 - layer);
		
		vec2 norm = vec2(-dir.y, dir.x);
		
		float r = v.radius + aa;
		
		// interpolation parameters for actual line outlines etc. in frag shader
		v.coord = vec2(uv.x == -1.0 ? -r : r + v.len, uv.y * r);
		
		v.t = mix(t.x, t.y, v.coord.x / v.len);
		
		col_a = vec4(col.rgb * vec3((states & 1) != 0 ? 1.0 : 0.03), col.a);
		col_b = vec4(col.rgb * vec3((states & 2) != 0 ? 1.0 : 0.03), col.a);
		
		col_a.rgb = mix(0.02 * col.rgb, col_a.rgb, layer);
		col_b.rgb = mix(0.02 * col.rgb, col_b.rgb, layer);
		
		// line vertices
		vec2 pos = uv.x == -1.0 ? pos0 : pos1;
		pos += norm * r * uv.y;
		pos += dir  * r * uv.x;
		
		
		gl_Position.xy = (view.world2clip * vec4(pos, 0.0, 1.0)).xy;
		
		float depth = 1.0 - (float(wire_id) + layer*0.5) / num_wires;
		
		gl_Position.z = depth;
		gl_Position.w = 1.0;
	}
#endif
#ifdef _FRAGMENT
	out vec4 frag_col;
	
	uniform float sim_t;
	uniform float anim_fade = 0.5;
	
	float slider_anim (float x) {
		//return clamp((x - sim_t) * 4.0 + 0.5, 0.0, 1.0);
		//return clamp((x - sim_t) * 4.0 + (1.0 - x), 0.0, 1.0);
		
		//return smoothstep(0.0, 1.0, (v.t - sim_t) * 4.0 + 0.5);
		return smoothstep(0.0, 1.0,
			(x - sim_t) * (1.0 + 1.0 / anim_fade) + (1.0 - x));
	}
	
	void main () {
		float aa = view.frust_near_size.x * view.inv_viewport_size.x * 1.0; // pixel size in world units
		
		// compute end cap circle
		vec2 offs = v.coord - vec2(clamp(v.coord.x, 0.0, v.len), 0.0);
		float r = length(offs) - v.radius;
		
		// compute wire state animation color
		float t = slider_anim(v.t);
		
		vec4 col = mix(col_a, col_b, t);
		
		float aa_alpha = map_clamp(r, -aa/2.0, +aa/2.0, 1.0, 0.0);
		
		col.a *= aa_alpha;
		
		frag_col = col;
		
		if (col.a < 0.001)
			discard;
	}
#endif
