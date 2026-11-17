#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_fromTexture;    // outgoing clip layer
uniform sampler2D u_toTexture;      // incoming clip layer
uniform vec2 u_resolution;
uniform float u_progress;           // 0..1 across the transition window

vec4 blurTap(sampler2D t, vec2 uv, float r) {
    vec2 px = r / u_resolution;
    vec4 s = texture(t, uv) * 0.2270270270;
    s += (texture(t, uv + vec2(px.x * 1.3846, 0.0)) + texture(t, uv - vec2(px.x * 1.3846, 0.0))
        + texture(t, uv + vec2(0.0, px.y * 1.3846)) + texture(t, uv - vec2(0.0, px.y * 1.3846))) * 0.1581081081;
    s += (texture(t, uv + vec2(px.x * 3.2308, 0.0)) + texture(t, uv - vec2(px.x * 3.2308, 0.0))
        + texture(t, uv + vec2(0.0, px.y * 3.2308)) + texture(t, uv - vec2(0.0, px.y * 3.2308))) * 0.0351351351;
    return s;
}

void main() {
    float p = u_progress;
    float r = sin(3.14159265 * p) * 14.0 * (u_resolution.y / 720.0); // blur peaks at the middle
    vec4 a = blurTap(u_fromTexture, v_texCoord, r);
    vec4 b = blurTap(u_toTexture, v_texCoord, r);
    fragColor = mix(a, b, p);
}
