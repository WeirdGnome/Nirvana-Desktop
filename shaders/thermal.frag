// Thermal camera: brightness shown as heat - dark blue, violet, red, orange, white.
vec3 heat(float t) {
  vec3 a = vec3(0.0, 0.0, 0.18), b = vec3(0.35, 0.0, 0.6), c = vec3(0.9, 0.1, 0.25);
  vec3 d = vec3(1.0, 0.6, 0.0), e = vec3(1.0, 1.0, 0.75);
  if (t < 0.25) return mix(a, b, t / 0.25);
  if (t < 0.5) return mix(b, c, (t - 0.25) / 0.25);
  if (t < 0.75) return mix(c, d, (t - 0.5) / 0.25);
  return mix(d, e, (t - 0.75) / 0.25);
}
void main() {
  float l = dot(screen(v_texcoord).rgb, vec3(0.299, 0.587, 0.114));
  gl_FragColor = vec4(heat(clamp(l, 0.0, 1.0)), 1.0);
}
