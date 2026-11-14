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

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);if(u_hasClipMask<.5){fragColor=s;return;}float m=driftMask(uv);float yy=lum(s.rgb);float groove=.5+.5*sin(dot(uv,vec2(311.,217.)));vec3 etch=mix(vec3(.27,.43,.47),vec3(.81,.91,.89),yy*.8+groove*.2);fragColor=vec4(mix(s.rgb,etch,m*strength),s.a);}
