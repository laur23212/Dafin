#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float paperWhite;
uniform float inkDepth;
uniform float neutralize;
uniform float threshold;
void main(){
 vec4 s=texture(u_currentTexture,clamp(v_texCoord,vec2(0.0),vec2(1.0)));
 float lum=dot(s.rgb,vec3(.2126,.7152,.0722));
 float paper=smoothstep(threshold-.25,threshold+.23,lum);
 float dark=1.0-smoothstep(threshold-.32,threshold+.05,lum);
 vec3 c=mix(s.rgb,vec3(lum),neutralize);
 c=mix(c,vec3(1.0),paper*paperWhite);
 c=mix(c,c*(1.0-inkDepth),dark);
 fragColor=vec4(clamp(c,vec3(0.0),vec3(1.0)),s.a);
}