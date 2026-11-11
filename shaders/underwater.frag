// Underwater: a gently waving picture with a blue-green tint and moving light.
void main() {
  vec2 uv = v_texcoord;
  float t = u_time;
  uv.x += sin(uv.y * 24.0 + t * 1.8) * 0.0025;
  uv.y += cos(uv.x * 19.0 + t * 1.4) * 0.0025;
  vec3 col = screen(uv).rgb * vec3(0.78, 0.95, 1.05);
  float light = sin(uv.x * 40.0 + t * 1.3) * sin(uv.y * 33.0 - t * 1.1);
  col += 0.05 * max(light, 0.0);
  gl_FragColor = vec4(col, 1.0);
}
