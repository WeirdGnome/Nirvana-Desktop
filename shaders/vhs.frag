// VHS tape: color bleeding, a shaky picture, noise and a rolling tracking band.
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
  vec2 uv = v_texcoord;
  float t = u_time;
  float row = floor(gl_FragCoord.y / 2.0);
  uv.x += (hash(vec2(row, floor(t * 15.0))) - 0.5) * 0.0008;                   // shaky lines
  float band = 1.0 - smoothstep(0.0, 0.025, abs(fract(uv.y * 0.6 - t * 0.12) - 0.5));
  uv.x += band * 0.006;                                                         // tracking band
  vec3 col = vec3(screen(uv + vec2(0.0015, 0.0)).r, screen(uv).g, screen(uv - vec2(0.0015, 0.0)).b);
  float l = dot(col, vec3(0.299, 0.587, 0.114));
  col = mix(vec3(l), col, 0.8);                                                 // tired colors
  col += (hash(gl_FragCoord.xy + fract(t) * 91.0) - 0.5) * 0.06;                // noise
  col += band * 0.08 * hash(vec2(gl_FragCoord.x, t));
  col *= 0.93 + 0.07 * sin(gl_FragCoord.y * 1.57);
  gl_FragColor = vec4(col, 1.0);
}
