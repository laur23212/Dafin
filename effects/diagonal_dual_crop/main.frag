#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float tilt;
uniform float separation;
uniform float zoomNear;
uniform vec3 seamColor;
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));
 float edge=uv.x-.5-tilt*(uv.y-.5);
 vec2 a=uv;
 vec2 b=clamp(vec2(.56,.5)+(uv-.5)/max(zoomNear,1.0),vec2(0.0),vec2(1.0));
 vec3 broad=texture(u_currentTexture,a).rgb;
 vec3 detail=texture(u_currentTexture,b).rgb;
 float t=smoothstep(-.002,.002,edge);
 vec3 rgb=mix(broad,detail,t);
 float seam=1.0-smoothstep(separation*.3,separation,abs(edge));
 rgb=mix(rgb,seamColor,seam);
 fragColor=vec4(rgb,texture(u_currentTexture,uv).a);
}