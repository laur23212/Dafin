#version 330 core
in vec2 v_texCoord; out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
float sat(float x){return clamp(x,0.0,1.0);}
vec3 cap(vec3 x){return clamp(x,vec3(0.0),vec3(1.0));}
float lum(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
float hsh(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float tm(){return float(u_frameIndex)/max(u_frameRate,1.0);}
vec2 texel(){return 1.0/max(u_resolution,vec2(1.0));}
vec2 mirrorUV(vec2 uv){return 1.0-abs(mod(uv,2.0)-1.0);}
vec4 S(vec2 uv){return texture(u_currentTexture,clamp(uv,vec2(.000001),vec2(.999999)));}
float Y(vec2 uv){return lum(S(uv).rgb);}
vec3 around(vec2 uv,float rad){vec2 d=texel()*rad;return (S(uv+vec2(d.x,0.)).rgb+S(uv-vec2(d.x,0.)).rgb+S(uv+vec2(0.,d.y)).rgb+S(uv-vec2(0.,d.y)).rgb)*.25;}
float edgeAt(vec2 uv){vec2 d=texel();return sat(length(vec2(Y(uv+d*vec2(1.,0.))-Y(uv-d*vec2(1.,0.)),Y(uv+d*vec2(0.,1.))-Y(uv-d*vec2(0.,1.))))*3.);}
uniform float strength;
void main(){vec2 u=v_texCoord;vec4 s=S(u);if(u_hasDepth<.5){fragColor=s;return;}vec3 n=driftNormal(u,3.0);float spec=sat(abs(n.x)*.9+abs(n.y)*.35);float streak=abs(sin((u.x+n.y*.018)*u_resolution.x*.22));vec3 c=s.rgb+vec3(.13,.11,.08)*spec*streak*strength;fragColor=vec4(cap(c),s.a);}
