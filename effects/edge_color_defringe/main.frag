#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float strength;
uniform float radius;
uniform float sensitivity;
vec4 src(vec2 p){return texture(u_currentTexture,clamp(p,vec2(0.0),vec2(1.0)));}
float lum(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
float purple(vec3 c){return max(min(c.r,c.b)-c.g,0.0);}
void main(){
 vec2 uv=v_texCoord;
 vec4 s=src(uv);
 // Test chroma across a wider band than a 1px edge to catch fringes several pixels thick.
 vec2 d=radius*3.5/max(u_resolution,vec2(1.0));
 vec3 a=src(uv+vec2(d.x,0.)).rgb,b=src(uv-vec2(d.x,0.)).rgb;
 vec3 c=src(uv+vec2(0.,d.y)).rgb,e=src(uv-vec2(0.,d.y)).rgb;
 float edge=clamp(max(abs(lum(a)-lum(b)),abs(lum(c)-lum(e)))*3.0,0.0,1.0);
 float fringe=purple(s.rgb);
 float gate=smoothstep(sensitivity,sensitivity+.20,fringe)*edge;
 // Pick the less-magenta of each opposite pair to avoid reusing the aberrated pixels.
 vec3 ax=purple(a)<purple(b)?a:b;
 vec3 ay=purple(c)<purple(e)?c:e;
 vec3 reference=purple(ax)<purple(ay)?ax:ay;
 float targetLuma=lum(s.rgb),refLuma=lum(reference);
 vec3 corrected=clamp(reference+vec3(targetLuma-refLuma),vec3(0.0),vec3(1.0));
 fragColor=vec4(mix(s.rgb,corrected,clamp(strength*gate,0.0,1.0)),s.a);
}
