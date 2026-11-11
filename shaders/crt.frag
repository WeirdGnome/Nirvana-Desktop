// CRT monitor: curved glass, scanlines, an RGB phosphor mask and a vignette.
vec2 curve(vec2 uv) {
  uv = uv * 2.0 - 1.0;
  vec2 bend = abs(uv.yx) / vec2(6.0, 5.0);
  uv += uv * bend * bend;
  return uv * 0.5 + 0.5;
}
void main() {
  vec2 uv = curve(v_texcoord);
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
  float px = 1.0 / u_resolution.x;
  vec3 col = vec3(screen(uv + vec2(px * 0.7, 0.0)).r, screen(uv).g, screen(uv - vec2(px * 0.7, 0.0)).b);
  col *= 0.80 + 0.20 * sin(uv.y * u_resolution.y * 3.14159);          // scanlines
  float m = mod(gl_FragCoord.x, 3.0);                                  // phosphor triads
  col *= m < 1.0 ? vec3(1.0, 0.82, 0.82) : m < 2.0 ? vec3(0.82, 1.0, 0.82) : vec3(0.82, 0.82, 1.0);
  vec2 v = uv * (1.0 - uv.yx);
  col *= pow(clamp(v.x * v.y * 16.0, 0.0, 1.0), 0.22);                 // vignette
  gl_FragColor = vec4(col * 1.28, 1.0);
}
