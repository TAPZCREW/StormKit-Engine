struct VertexOut {
  float3 position: SV_Position;
  float2 uv;
};

struct FragmentOut {
    float4 color: SV_Target<0>;
}

[shader("vertex")]
VertexOut vs_main() {
   VertexOutput out;
   out.color = float4(input.uv, 0., 1.);
   out.uv = input.uv;

   return out;
}

[shader("fragment")]
FragmentOut frag_main(VertexOut in) {
   FragmentOut out;
   out.color = float4(input.uv, 0., 1.);

   return out;
}
