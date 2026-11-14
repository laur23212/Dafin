#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform vec2 u_resolution;
uniform float grain;
uniform float feather;

void main(){
 if(u_progress <= 0.0){fragColor=texture(u_fromTexture,v_texCoord);return;}
 if(u_progress >= 1.0){fragColor=texture(u_toTexture,v_texCoord);return;}
vec2 c=floor(v_texCoord*u_resolution/max(grain,1.));float n=fract(sin(dot(c,vec2(12.9898,78.233)))*43758.5453);float m=1.-smoothstep(u_progress-feather,u_progress+feather,n);fragColor=mix(texture(u_fromTexture,v_texCoord),texture(u_toTexture,v_texCoord),m);
}
