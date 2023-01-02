#version 430

#ifdef _VERTEX
	layout(location = 0) in vec2  pos;
	
	void main () {
		gl_Position = vec4(pos, 0.0, 1.0);
	}
#endif
#ifdef _FRAGMENT
	uniform vec4 col;
	
	out vec4 frag_col;
	void main () {
		frag_col = col;
	}
#endif
