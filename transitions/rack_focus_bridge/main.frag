#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float blurPixels;
uniform float handoffWidth;
uniform float focusAsymmetry;
vec4 pmMix(vec4 a,vec4 b,float t){
    float alpha=mix(a.a,b.a,t);
    vec3 premul=mix(a.rgb*a.a,b.rgb*b.a,t);
    return vec4(alpha>1e-6?premul/alpha:vec3(0.0),alpha);
}
vec4 blurSource(sampler2D tex,vec2 uv,float radius){
    // 13 taps including the centre, symmetric disk offsets. All samples use straight alpha.
    vec2 texel=1.0/max(u_resolution,vec2(1.0));
    const vec2 coords[12]=vec2[12](
        vec2(1.0,0.0),vec2(-1.0,0.0),vec2(0.0,1.0),vec2(0.0,-1.0),
        vec2(.7071,.7071),vec2(-.7071,.7071),vec2(.7071,-.7071),vec2(-.7071,-.7071),
        vec2(.43,.21),vec2(-.43,-.21),vec2(.21,-.43),vec2(-.21,.43));
    vec4 base=texture(tex,uv);
    vec3 premul=base.rgb*base.a*2.0;
    float a=base.a*2.0;
    float weight=2.0;
    for(int i=0;i<12;i++){
        vec2 q=clamp(uv+coords[i]*texel*radius,vec2(0.0),vec2(1.0));
        vec4 s=texture(tex,q);
        float w=i<8?1.0:1.3;
        premul+=s.rgb*s.a*w;
        a+=s.a*w;
        weight+=w;
    }
    a/=weight;
    return vec4(a>1e-6?premul/max(a*weight,1e-6):vec3(0.0),a);
}
void main(){
    float p=clamp(u_progress,0.0,1.0);
    vec2 uv=v_texCoord;
    if(p<=0.0){fragColor=texture(u_fromTexture,uv);return;}
    if(p>=1.0){fragColor=texture(u_toTexture,uv);return;}
    float pivot=clamp(.5+focusAsymmetry,.32,.68);
    float phaseA=smoothstep(0.0,pivot,p);
    float phaseB=1.0-smoothstep(pivot,1.0,p);
    vec4 a=blurSource(u_fromTexture,uv,blurPixels*phaseA);
    vec4 b=blurSource(u_toTexture,uv,blurPixels*phaseB);
    float t=smoothstep(pivot-handoffWidth*.5,pivot+handoffWidth*.5,p);
    fragColor=pmMix(a,b,t);
}

