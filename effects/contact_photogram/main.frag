#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
float lum(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
float hashv(vec2 v){return fract(sin(dot(v,vec2(127.1,311.7)))*43758.5453);}
vec2 reflectUV(vec2 uv){return 1.0-abs(mod(uv,2.0)-1.0);}
vec4 inputAt(vec2 uv){return texture(u_currentTexture,clamp(uv,vec2(0.001),vec2(.999)));}
float safeTime(){return float(u_frameIndex)/max(u_frameRate,1.0);}
uniform float strength;

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);float y=lum(s.rgb);vec2 px=1./max(u_resolution,vec2(1.));float e=abs(lum(inputAt(uv+vec2(px.x,0.)).rgb)-lum(inputAt(uv-vec2(px.x,0.)).rgb))+abs(lum(inputAt(uv+vec2(0.,px.y)).rgb)-lum(inputAt(uv-vec2(0.,px.y)).rgb));vec3 paper=mix(vec3(.015,.09,.17),vec3(.72,.88,.83),1.-y);paper+=e*vec3(.25,.39,.28);fragColor=vec4(clamp(mix(s.rgb,paper,strength),0.,1.),s.a);}
