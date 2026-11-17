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
    // Blend in linear light so the middle of the dissolve keeps its brightness instead of dipping.
    vec3 lin = mix(pow(a.rgb, vec3(2.2)), pow(b.rgb, vec3(2.2)), u_progress);
    fragColor = vec4(pow(lin, vec3(1.0 / 2.2)), mix(a.a, b.a, u_progress));
}
