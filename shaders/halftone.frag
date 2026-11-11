// Halftone: newspaper print - black dots on paper, larger where the picture is darker.
void main() {
  const float size = 6.0;
  mat2 rot = mat2(0.7071, 0.7071, -0.7071, 0.7071);
  vec2 q = rot * gl_FragCoord.xy;
  vec2 cell = floor(q / size) * size + size * 0.5;
  vec2 centre = transpose(rot) * cell;
  float l = dot(screen(centre / u_resolution).rgb, vec3(0.299, 0.587, 0.114));
  float r = sqrt(1.0 - l) * size * 0.72;
  float ink = 1.0 - smoothstep(r - 0.8, r + 0.8, distance(q, cell));
  gl_FragColor = vec4(mix(vec3(0.97, 0.95, 0.89), vec3(0.12, 0.11, 0.12), ink), 1.0);
}
