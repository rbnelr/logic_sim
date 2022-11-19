#version 430
#include "common.glsl"

struct Vertex {
	vec2 uv;
	vec4 col;
};
VS2FS Vertex v;

flat VS2FS int v_gate_type;
flat VS2FS int v_gate_state;

#define GT_BUF  0
#define GT_NOT  1
#define GT_AND  2
#define GT_NAND 3
#define GT_OR   4
#define GT_NOR  5
#define GT_XOR  6

#ifdef _VERTEX
	layout(location = 0) in vec2  pos;
	layout(location = 1) in vec2  uv;
	layout(location = 2) in int   gate_type;
	layout(location = 3) in int   gate_state;
	layout(location = 4) in vec4  col;
	
	void main () {
		gl_Position = view.world2clip * vec4(pos, 0.0, 1.0);
		v.uv  = uv;
		v.col = col;
		
		v_gate_type  = gate_type;
		v_gate_state  = gate_state;
	}
#endif
#ifdef _FRAGMENT
	out vec4 frag_col;
	
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
	
	float buf_gate (vec2 uv) {
		
		vec2 uv_mirror = vec2(uv.x, abs(uv.y - 0.5));
		
		float d   = SDF_line(uv, vec2(0.14,0.0), vec2(0.14,1.0));
		d = max(d, SDF_line(uv_mirror, vec2(0.0,0.3), vec2(0.83, 0.0)));
		
		return d;
	}
	float and_gate (vec2 uv) {
		float a = 0.3;
		
		float d;
		if (uv.x < a)
			d = SDF_box(uv - 0.5, vec2(0.36,0.40));
		else
			d = SDF_ellipse(uv - vec2(a,0.5), vec2(0.8-a, 0.40));
		
		return d;
	}
	float or_gate (vec2 uv) {
		
		vec2 uv_mirror = vec2(uv.x, abs(uv.y - 0.5));
		
		float d = SDF_ellipse(uv_mirror - vec2(0.02,-0.1), vec2(0.8, 0.5));
		d = max(d, -SDF_ellipse(uv - vec2(-0.33,0.5), vec2(0.5, 0.6)));
		
		return d;
	}
	float xor_gate (vec2 uv) {
		vec2 uv_mirror = vec2(uv.x, abs(uv.y - 0.5));
		
		float d = SDF_ellipse(uv_mirror - vec2(0.02,-0.1), vec2(0.8, 0.5));
		d = max(d, -SDF_ellipse(uv - vec2(-0.33,0.5), vec2(0.5, 0.6)));
		
		float d2 = SDF_ellipse(uv - vec2(-0.15,0.5), vec2(0.5, 0.6));
		d2 = max(d2, -SDF_ellipse(uv - vec2(-0.19,0.5), vec2(0.5, 0.6)));
		
		return max(d, -d2);
	}
	
	float not_gate (vec2 uv) {
		float d = buf_gate(uv);
		return min(d, SDF_circ(uv - vec2(0.88,0.5), 0.10));
	}
	float nand_gate (vec2 uv) {
		float d = and_gate(uv);
		return min(d, SDF_circ(uv - vec2(0.88,0.5), 0.10));
	}
	float nor_gate (vec2 uv) {
		float d = or_gate(uv);
		return min(d, SDF_circ(uv - vec2(0.88,0.5), 0.10));
	}
	
	void main () {
		
		float du = fwidth(v.uv.x);
		
		float outline = clamp(du * 4.0, 0.02, 0.1);
		float aa = du * 1.0;
		
		float d;
		
		if      (v_gate_type == GT_BUF ) d =  buf_gate(v.uv);
		else if (v_gate_type == GT_NOT ) d =  not_gate(v.uv);
		else if (v_gate_type == GT_AND ) d =  and_gate(v.uv);
		else if (v_gate_type == GT_NAND) d = nand_gate(v.uv);
		else if (v_gate_type == GT_OR  ) d =   or_gate(v.uv);
		else if (v_gate_type == GT_NOR ) d =  nor_gate(v.uv);
		else /* v_gate_type == GT_XOR */ d =  xor_gate(v.uv);
		
		// TODO: Can optimize by merging AND & NAND etc. with just a not-dot if cond
		// Or just seperate out shader?
		
		float alpha      = clamp(-d / aa + 0.5, 0.0, 1.0);
		float outl_alpha = clamp((d + outline) / aa + 0.5, 0.0, 1.0);
		//float outl       = clamp(-d / outline, 0.0, 1.0);
		
		vec4 c = v.col;
		c.rgb *= v_gate_state != 0 ? vec3(1) : vec3(0.05);
		
		frag_col.rgb = c.rgb * (1.0 - outl_alpha * 0.99);
		frag_col.a   = c.a * alpha;
	}
#endif
