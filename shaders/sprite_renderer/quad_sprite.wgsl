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

@vertex fn vert_main(@builtin(vertex_index) id: u32) -> VertOut {
    var output: VertOut;

    var vertex = vec2f(0.f, 0.f);
    if(id == 1) {
        vertex = vec2f(1.f, 0.f);
    } else if(id == 2) {
        vertex = vec2f(0.f, 1.f);
    } else if(id == 3) {
        vertex = vec2f(1.f, 1.f);
    }

    output.position = camera.proj * camera.view * sprite_data.model * vec4f(vertex, 1., 1.);
    output.uv    = vertex;

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
