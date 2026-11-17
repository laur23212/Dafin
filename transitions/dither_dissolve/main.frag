#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_fromTexture;    // outgoing clip layer
uniform sampler2D u_toTexture;      // incoming clip layer
uniform vec2 u_resolution;
uniform float u_progress;           // 0..1 across the transition window
uniform float cell;

void main() {
    vec4 a = texture(u_fromTexture, v_texCoord);
    vec4 b = texture(u_toTexture, v_texCoord);
    ivec2 c = ivec2(v_texCoord * u_resolution / max(cell, 1.0)) & 3;
    float bayer4[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    float t = (bayer4[c.y * 4 + c.x] + 0.5) / 16.0;
    float m = smoothstep(t - 0.04, t + 0.04, u_progress * 1.08 - 0.04);
    fragColor = mix(a, b, m);
}
