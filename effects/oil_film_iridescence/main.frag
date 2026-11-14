#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
float luminance(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
vec2 mirrored(vec2 v){return 1.0-abs(mod(v,2.0)-1.0);}
float hash21(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float secondsNow(){return float(u_frameIndex)/max(u_frameRate,1.0);}
uniform float strength;
uniform float threshold;
uniform float scale;
uniform float direction;
void main(){vec2 uv=v_texCoord;vec4 src=texture(u_currentTexture,uv);
 float lum=luminance(src.rgb);float gate=smoothstep(threshold,min(.99,threshold+.25),lum);
 float theta=direction*6.2831853;vec2 axis=vec2(cos(theta),sin(theta));
 float phase=dot(uv*vec2(u_resolution.x/max(u_resolution.y,1.),1.),axis)*mix(11.,59.,scale)+lum*7.0;
 vec3 spectrum=vec3(.5+.5*cos(phase),.5+.5*cos(phase+2.0944),.5+.5*cos(phase+4.1888));
 float amt=gate*strength*.49;
 fragColor=vec4(clamp(mix(src.rgb,src.rgb*.70+spectrum*.73,amt),0.,1.),src.a);
}
