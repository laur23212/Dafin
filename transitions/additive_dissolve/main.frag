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
    float p = u_progress;
    vec4 c = mix(a, b, p);
    c.rgb += min(a.rgb, b.rgb) * 0.6 * sin(3.14159265 * p); // brightens through the middle
    fragColor = vec4(min(c.rgb, vec3(1.0)), c.a);
}
