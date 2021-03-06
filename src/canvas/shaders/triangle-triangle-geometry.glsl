#version 330
layout(points) in;
layout(triangle_strip, max_vertices = 3) out;


in vec2 p0_to_geom[1];
in vec2 p1_to_geom[1];
in vec2 p2_to_geom[1];
in int oid_to_geom[1];
in int type_to_geom[1];
in int color_to_geom[1];
in int flags_to_geom[1];
in int lod_to_geom[1];
smooth out vec3 color_to_fragment;
smooth out float striper_to_fragment;

##ubo

int mode = layer_flags;

#define PI 3.1415926535897932384626433832795

vec4 t(vec2 p) {
    return vec4((screenmat*viewmat*vec3(p, 1)), 1);
}

float t2(vec2 p) {
	if(mode==1) //HATCH
		return scale*p.x -scale*p.y;
	return 0.0;
}

vec2 p2r(float phi, float l) {
	return vec2(cos(phi), sin(phi))*l;
}

void main() {
	vec2 p0 = p0_to_geom[0];
	vec2 p1 = p1_to_geom[0];
	vec2 p2 = p2_to_geom[0];
	color_to_fragment = vec3(1,0,0);

	
	int flags = flags_to_geom[0];
	if((flags & (1<<0)) != 0) { //hidden
		return;
	}
	int type = type_to_geom[0];

	if((types_force_outline & uint(1<<type)) != uint(0))
		return;
	
	if((types_visible & uint(1<<type)) == uint(0))
		return;
	
	vec3 color;
	if(color_to_geom[0] == 0) {
		color = layer_color;
	}
	else {
		color = colors[color_to_geom[0]];
	}
	
	bool highlight = (type == 1 || ((flags & (1<<1)) != 0));
	color_to_fragment = apply_highlight(color, highlight, type);
	

	gl_Position = t(p0);
	striper_to_fragment = t2(p0);
	EmitVertex();
	
	gl_Position = t(p1);
	striper_to_fragment = t2(p1);
	EmitVertex();
	
	gl_Position = t(p2);
	striper_to_fragment = t2(p2);
	EmitVertex();
	
	
	EndPrimitive();
	
}
