#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float strength;
uniform float bands;
uniform float relief;
uniform vec3 nearTint;
uniform vec3 farTint;
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));
 vec4 src=texture(u_currentTexture,uv);
 if(u_hasDepth<0.5){fragColor=src;return;}
 float d=clamp(driftDepthGuided(uv,u_currentTexture),0.0,1.0);
 float n=max(3.0,floor(bands+0.5));
 float layer=floor(d*(n-0.0001))/max(n-1.0,1.0);
 vec3 palette=mix(farTint,nearTint,layer);
 float luma=dot(src.rgb,vec3(0.2126,0.7152,0.0722));
 vec3 ink=mix(palette,palette*(0.56+0.44*luma),0.6);
 vec2 stepUV=1.5/max(u_resolution,vec2(1.0));
 float nx=clamp(driftDepth(uv+vec2(stepUV.x,0.0)),0.0,1.0);
 float ny=clamp(driftDepth(uv+vec2(0.0,stepUV.y)),0.0,1.0);
 float edge=clamp((abs(nx-d)+abs(ny-d))*relief*4.0,0.0,0.5);
 ink*=1.0-edge;
 fragColor=vec4(clamp(mix(src.rgb,ink,strength),vec3(0.0),vec3(1.0)),src.a);
}
