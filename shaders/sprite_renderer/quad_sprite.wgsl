struct VertIn {
    @location(0) position: vec2f,
    @location(1) uv: vec2f
}

struct VertOut {
    @builtin(position) position: vec4f, 
    @location(0) uv: vec2f
}

struct Camera {
    proj: mat4x4f,
    view: mat4x4f,
}

struct SpriteData {
    model: mat4x4f,
}

@group(0) @binding(0)
var<uniform> camera: Camera;

@group(1) @binding(0)
var<uniform> sprite_data: SpriteData;

@vertex fn vert_main(input: VertIn) -> VertOut {
    var output: VertOut;

    output.position = camera.proj * camera.view * vec4f(input.position, 1., 1.);
    output.uv    = input.uv;

    return output;
}

struct FragIn {
    @location(0) uv: vec2f
}

struct FragOut {
    @location(0) color: vec4f
}

@fragment fn frag_main(input: FragIn) -> FragOut {
    var output: FragOut;

    output.color = vec4f(input.uv, 1., 1.);

    return output;
}
