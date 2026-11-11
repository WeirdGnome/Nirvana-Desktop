// Amber monitor: a monochrome amber phosphor screen with a slight glow.
void main() {
  vec2 px = 1.0 / u_resolution;
  vec3 w = vec3(0.299, 0.587, 0.114);
  float l = dot(screen(v_texcoord).rgb, w);
  float glow = 0.0;                                   // phosphor glow from the neighbours
  glow += dot(screen(v_texcoord + vec2(px.x * 2.0, 0.0)).rgb, w);
  glow += dot(screen(v_texcoord - vec2(px.x * 2.0, 0.0)).rgb, w);
  glow += dot(screen(v_texcoord + vec2(0.0, px.y * 2.0)).rgb, w);
  glow += dot(screen(v_texcoord - vec2(0.0, px.y * 2.0)).rgb, w);
  l = max(l, glow * 0.25 * 0.9);
  float scan = 0.85 + 0.15 * sin(gl_FragCoord.y * 3.14159);
  vec3 col = mix(vec3(0.05, 0.02, 0.0), vec3(1.0, 0.70, 0.18), l) * scan;
  gl_FragColor = vec4(col, 1.0);
}
