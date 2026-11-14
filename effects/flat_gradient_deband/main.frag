#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float smoothing;
uniform float edgeProtect;
uniform float dither;
vec4 src(vec2 uv){return texture(u_currentTexture,clamp(uv,vec2(0.0),vec2(1.0)));}
float lum(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
void main(){
 vec2 uv=v_texCoord; vec4 orig=src(uv);vec2 d=1.0/max(u_resolution,vec2(1.0));
 vec3 left=src(uv-6.*vec2(d.x,0.)).rgb,right=src(uv+6.*vec2(d.x,0.)).rgb;
 vec3 top=src(uv-6.*vec2(0.,d.y)).rgb,bot=src(uv+6.*vec2(0.,d.y)).rgb;
 float variation=max(max(length(left-right),length(top-bot)),0.0);
 float protect=1.0-smoothstep(edgeProtect*.35,edgeProtect,variation);
 vec3 soft=(orig.rgb*3.0+left+right+top+bot)/7.0;
 vec3 rgb=mix(orig.rgb,soft,protect*smoothing);
 // Deterministic 4x4 spatial Bayer-like dither; stable when scrubbing or exporting.
 vec2 cell=mod(floor(uv*u_resolution),4.0);
 float ordered=fract(cell.x*.5+cell.y*.25+cell.x*cell.y*.1875)-.5;
 rgb+=vec3(ordered*(dither/255.0)*protect);
 fragColor=vec4(clamp(rgb,vec3(0.0),vec3(1.0)),orig.a);
}
