// Dreamy glow: a soft bloom around bright areas and a little extra color.
void main() {
  vec2 px = 3.0 / u_resolution;
  vec3 base = screen(v_texcoord).rgb;
  vec3 blur = vec3(0.0);
  for (int i = -2; i <= 2; i++)
    for (int j = -2; j <= 2; j++) blur += screen(v_texcoord + vec2(float(i), float(j)) * px).rgb;
  blur /= 25.0;
  vec3 col = base * 0.78 + blur * 0.42;
  float l = dot(col, vec3(0.299, 0.587, 0.114));
  col = mix(vec3(l), col, 1.15);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
