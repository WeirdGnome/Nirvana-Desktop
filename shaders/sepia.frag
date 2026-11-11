// Sepia: the brown tint of an old photograph.
void main() {
  vec3 c = screen(v_texcoord).rgb;
  vec3 s = vec3(dot(c, vec3(0.393, 0.769, 0.189)), dot(c, vec3(0.349, 0.686, 0.168)), dot(c, vec3(0.272, 0.534, 0.131)));
  gl_FragColor = vec4(min(s, vec3(1.0)), 1.0);
}
