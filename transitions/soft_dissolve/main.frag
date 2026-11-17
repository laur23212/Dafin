#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_fromTexture;    // outgoing clip layer
uniform sampler2D u_toTexture;      // incoming clip layer
uniform vec2 u_resolution;
uniform float u_progress;           // 0..1 across the transition window

void main() {
    vec4 a = texture(u_fromTexture, v_texCoord);
    vec4 b = texture(u_toTexture, v_texCoord);
    float p = u_progress * u_progress * (3.0 - 2.0 * u_progress); // eased in and out
    fragColor = mix(a, b, p);
}
