#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform vec2 u_resolution;
uniform float softness;

void main(){
 if(u_progress <= 0.0){fragColor=texture(u_fromTexture,v_texCoord);return;}
 if(u_progress >= 1.0){fragColor=texture(u_toTexture,v_texCoord);return;}
float f=smoothstep(u_progress-softness,u_progress+softness,v_texCoord.x);fragColor=mix(texture(u_toTexture,v_texCoord),texture(u_fromTexture,v_texCoord),f);
}
