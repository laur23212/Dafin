#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float hold;
uniform float backgroundSpeed;
uniform float edgeSoftness;
uniform float subjectLift;
// The engine injects u_hasClipMask and driftMask(uv) when requires:"mask".
vec4 pmMix(vec4 a,vec4 b,float t){
    float alpha=mix(a.a,b.a,t);
    vec3 premul=mix(a.rgb*a.a,b.rgb*b.a,t);
    return vec4(alpha>1e-6?premul/alpha:vec3(0.0),alpha);
}
vec4 overStraight(vec4 fg,vec4 bg){
    float alpha=fg.a+bg.a*(1.0-fg.a);
    vec3 premul=fg.rgb*fg.a+bg.rgb*bg.a*(1.0-fg.a);
    return vec4(alpha>1e-6?premul/alpha:vec3(0.0),alpha);
}
float filteredMask(vec2 uv){
    float c=driftMask(uv);
    vec2 px=1.0/max(u_resolution,vec2(1.0));
    float f=(driftMask(clamp(uv+vec2(px.x,0),0.0,1.0))
          +driftMask(clamp(uv-vec2(px.x,0),0.0,1.0))
          +driftMask(clamp(uv+vec2(0,px.y),0.0,1.0))
          +driftMask(clamp(uv-vec2(0,px.y),0.0,1.0)))*0.25;
    return clamp(mix(c,f,edgeSoftness),0.0,1.0);
}
void main(){
    float p=clamp(u_progress,0.0,1.0);
    vec2 uv=v_texCoord;
    vec4 a=texture(u_fromTexture,uv);
    vec4 b=texture(u_toTexture,uv);
    if(p<=0.0){fragColor=a;return;}
    if(p>=1.0){fragColor=b;return;}
    if(u_hasClipMask<0.5){fragColor=pmMix(a,b,smoothstep(0.0,1.0,p));return;}
    float bgPhase=smoothstep(0.0,backgroundSpeed,p);
    vec4 background=pmMix(a,b,bgPhase);
    float drift=subjectLift*sin(3.14159265359*p);
    vec2 q=clamp(uv+vec2(0.0,drift),vec2(0.0),vec2(1.0));
    float m=filteredMask(q);
    float visibility=1.0-smoothstep(hold,1.0,p);
    vec4 subject=texture(u_fromTexture,q);
    subject.a*=m*visibility;
    fragColor=overStraight(subject,background);
}

