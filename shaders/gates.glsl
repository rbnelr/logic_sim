#version 430
#include "common.glsl"

struct Vertex {
	vec2 uv;
	vec4 col;
};
VS2FS Vertex v;

flat VS2FS int v_gate_type;
flat VS2FS int v_state_id;

uniform usampler1DArray gate_state_tex;
uniform float sim_t;

#define BUF_GATE   0 //  0 / 10
#define AND_GATE   1 // 10 / 10
#define OR_GATE    2 // 20 / 10
#define XOR_GATE   3 // 30 / 10

#ifdef _VERTEX
	layout(location = 0) in vec2  pos;
	layout(location = 1) in vec2  uv;
	layout(location = 2) in int   gate_type;
	layout(location = 3) in int   state_id;
	layout(location = 4) in vec4  col;
	
	void main () {
		gl_Position = view.world2clip * vec4(pos, 0.0, 1.0);
		v.uv  = uv;
		v.col = col;
		
		v_gate_type  = gate_type;
		v_state_id  = state_id;
	}
#endif
#ifdef _FRAGMENT
	// with help from (those are 3d) https://iquilezles.org/articles/distfunctions/
	
	float SDF_line (vec2 p, vec2 a, vec2 b) {
		vec2 dir = normalize(b - a);
		vec2 norm = vec2(-dir.y, dir.x);
		return dot(p - a, norm);
	}
	float SDF_circ (vec2 p, float r) {
		return length(p) - r;
	}
	float SDF_box (vec2 p, vec2 b) {
		vec2 q = abs(p) - b;
		return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
	}
	// Not exact, error for squashed ellipses
	// TODO: use a few iterations of this? https://stackoverflow.com/questions/22959698/distance-from-given-point-to-given-ellipse
	float SDF_ellipse (vec2 p, vec2 r) {
		float k0 = length(p / r);
		float k1 = length(p / (r*r));
		return k0 * (k0-1.0) / k1;
	}
	
	//float d = SDF_box(uv -0.5, vec2(0.25, 0.25));
	//float d = SDF_circ(uv -0.5, 0.25);
	//float d = SDF_ellipse(uv -0.5, vec2(0.2, 0.3));
	//float d = SDF_line(uv -0.5, vec2(0,0.5), vec2(0.5,-0.2));
	
	//float pin_gate (vec2 uv) {
	//	vec2 uv_mirr = vec2(uv.x, abs(uv.y - 0.5));
	//	
	//	float d  = SDF_line(uv, vec2(0.375, 0.0), vec2(0.375, 1.0));
	//	d = max(d, SDF_line(uv, vec2(0.625, 1.0), vec2(0.625, 0.0)));
	//	
	//	d = max(d, SDF_line(uv_mirr, vec2(0.375,0.10), vec2(0.625, 0.01)));
	//	
	//	return d;
	//}
	float buf_gate (vec2 uv) {
		vec2 uv_mirr = vec2(uv.x, abs(uv.y - 0.5));
		
		float d  = SDF_line(uv, vec2(0.2, 0.0), vec2(0.2, 1.0));
		d = max(d, SDF_line(uv, vec2(0.8, 1.0), vec2(0.8, 0.0)));
		
		d = max(d, SDF_line(uv_mirr, vec2(0.0,0.25), vec2(0.8, 0.01)));
		
		return d;
	}
	float and_gate (vec2 uv, float yscale) {
		vec2 uv_mirr = vec2(uv.x, abs(uv.y - 0.5));
		
		float a = 0.3;
		
		float d;
		if (uv.x < a)
			d = SDF_box(uv - 0.5, vec2(0.36,0.40*yscale));
		else
			d = SDF_ellipse(uv - vec2(a,0.5), vec2(0.8-a, 0.40*yscale));
		
		return d;
	}
	float or_gate (vec2 uv, float yscale) {
		vec2 uv_mirr = vec2(uv.x, abs(uv.y - 0.5));
		
		float a = 0.2;
		
		float d;
		if (uv.x < a)
			d = SDF_line(uv_mirr, vec2(0.0, 0.4*yscale), vec2(1.0,0.4*yscale));
		else
			d = SDF_ellipse(uv_mirr - vec2(a,-0.1*yscale), vec2(0.8-a, 0.5*yscale));
			
		d = max(d, -SDF_ellipse(uv - vec2(-0.3,0.5), vec2(0.5, 0.6)));
		
		return d;
	}
	float xor_gate (vec2 uv) {
		float d = or_gate(uv, 1.0);
		
		float d2 = SDF_ellipse(uv - vec2(-0.12,0.5), vec2(0.5, 0.6));
		d2 = max(d2, -SDF_ellipse(uv - vec2(-0.17,0.5), vec2(0.5, 0.6)));
		
		return max(d, -d2);
	}
	
	//float not_gate (vec2 uv) {
	//	float d = buf_gate(uv);
	//	return min(d, SDF_circ(uv - vec2(0.88,0.5), 0.10));
	//}
	//float nand_gate (vec2 uv) {
	//	float d = and_gate(uv);
	//	return min(d, SDF_circ(uv - vec2(0.88,0.5), 0.10));
	//}
	//float nor_gate (vec2 uv) {
	//	float d = or_gate(uv);
	//	return min(d, SDF_circ(uv - vec2(0.88,0.5), 0.10));
	//}
	float invert_circ (vec2 uv) {
		return SDF_circ(uv - vec2(0.84,0.5), 0.10);
	}
	
	void main () {
		
		int  ty  = v_gate_type/10;
		bool inv = v_gate_type%10 >= 5;
		float yscale = v_gate_type%5 >= 2 ? 1.25 : 1.0;
		
		//uint prev_state = texelFetch(gate_state_tex, ivec2(state_id, 1), 0).x;
		uint cur_state  = texelFetch(gate_state_tex, ivec2(v_state_id, 0), 0).x;
		
		bool base_state = cur_state != 0;
		bool inv_state  = cur_state != 0;
		if (inv) base_state = !base_state;
		
		float du = fwidth(v.uv.x);
		
		float outline = clamp(du * 4.0, 0.02, 0.06);
		float aa = du * 1.0;
		
		vec4 frag_col;
		{ // draw base gate symbol
			float d;
			
			//if      (ty == INP_PIN ) d =  pin_gate(v.uv);
			if      (ty == BUF_GATE ) d =  buf_gate(v.uv);
			else if (ty == AND_GATE ) d =  and_gate(v.uv, yscale);
			else if (ty == OR_GATE  ) d =   or_gate(v.uv, yscale);
			else /* ty == XOR_GATE */ d =  xor_gate(v.uv);
			
			float alpha      = map_clamp(d, -aa/2.0, +aa/2.0, 1.0, 0.0);
			float outl_alpha = map_clamp(d + outline, -aa/2.0, +aa/2.0, 0.0, 1.0);
			//float outl       = clamp(-d / outline, 0.0, 1.0);
			
			vec4 c = v.col;
			c.rgb *= base_state ? vec3(1) : vec3(0.05);
			
			c.rgb *= (1.0 - outl_alpha * 0.99);
			c.a *= alpha;
			
			frag_col = c;
		}
		if (inv) { // draw invert circ
			float d = invert_circ(v.uv);
			
			float alpha      = map_clamp(d, -aa/2.0, +aa/2.0, 1.0, 0.0);
			float outl_alpha = map_clamp(d + outline, -aa/2.0, +aa/2.0, 1.0, 0.0);
			
			vec4 c = v.col;
			c.rgb *= inv_state ? vec3(1) : vec3(0.05);
			
			c.rgb *= outl_alpha * 0.99;
			c.a *= alpha;
			
			frag_col = alpha_blend(frag_col, c);
		}
		
		FRAG_COL(frag_col);
	}
#endif
