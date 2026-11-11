// LCD: a coarse liquid crystal panel with visible red, green and blue stripes.
void main() {
  const float size = 3.0;
  vec2 cell = floor(gl_FragCoord.xy / size);
  vec3 c = screen((cell * size + size * 0.5) / u_resolution).rgb;
  vec2 f = mod(gl_FragCoord.xy, size);
  vec3 stripe = f.x < 1.0 ? vec3(1.0, 0.25, 0.25) : f.x < 2.0 ? vec3(0.25, 1.0, 0.25) : vec3(0.25, 0.25, 1.0);
  float gap = f.y < 1.0 ? 0.65 : 1.0;
  gl_FragColor = vec4(c * stripe * gap * 1.6, 1.0);
}
