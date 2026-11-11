// Green phosphor: a monochrome green terminal screen.
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
  vec3 col = mix(vec3(0.0, 0.04, 0.01), vec3(0.35, 1.0, 0.45), l) * scan;
  gl_FragColor = vec4(col, 1.0);
}
