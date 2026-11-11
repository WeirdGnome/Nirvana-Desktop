// Film grain: moving grain and a gentle flicker, like an old projector.
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
  vec3 col = screen(v_texcoord).rgb;
  float frame = floor(u_time * 24.0);
  float g = hash(gl_FragCoord.xy + vec2(frame * 17.0, frame * 31.0)) - 0.5;
  col += g * 0.11;
  col *= 0.97 + 0.03 * hash(vec2(frame, 7.0));
  vec2 v = v_texcoord * (1.0 - v_texcoord);
  col *= mix(0.75, 1.0, clamp(v.x * v.y * 18.0, 0.0, 1.0));
  gl_FragColor = vec4(col, 1.0);
}
