// 64 colors: four levels per channel with ordered dithering, like an old graphics card.
float bayer2(vec2 p) { return mod(2.0 * p.x + 3.0 * p.y, 4.0); }
float bayer4(vec2 p) {
  p = mod(p, 4.0);
  return (4.0 * bayer2(mod(p, 2.0)) + bayer2(floor(p / 2.0))) / 16.0;
}
void main() {
  vec2 cell = floor(gl_FragCoord.xy / 2.0);                 // 2x2 screen pixels per dot
  vec3 c = screen((cell * 2.0 + 1.0) / u_resolution).rgb;
  float d = bayer4(cell) - 0.5;
  vec3 q = floor(c * 3.0 + 0.5 + d * 0.9) / 3.0;
  gl_FragColor = vec4(clamp(q, 0.0, 1.0), 1.0);
}
