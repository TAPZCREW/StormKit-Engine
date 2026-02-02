// module;

// #include <cstddef>

// module stormkit.engine;

// import std;

// import stormkit.core;
// import stormkit.gpu;

// import :sprite_renderer;

// namespace cm = stormkit::monadic;

// namespace stormkit::engine {
//     namespace {
//         constexpr auto QUAD_SPRITE_SHADER = core::into_bytes({ 0x11, 0x22 } // #include <QuadSprite.spv.h>
//         );

//    constexpr auto SPRITE_VERTEX_SIZE                 = sizeof(SpriteVertex);
//    constexpr auto SPRITE_VERTEX_BINDING_DESCRIPTIONS = std::array {
//        gpu::VertexBindingDescription {
//                                       .binding = 0,
//                                       .stride  = SPRITE_VERTEX_SIZE,
//                                       },
//    };
//    constexpr auto SPRITE_VERTEX_ATTRIBUTE_DESCRIPTIONS = std::array {
//        gpu::VertexInputAttributeDescription {
//                                              .location = 0,
//                                              .binding  = 0,
//                                              .format   = gpu::PixelFormat::RG32F,
//                                              .offset   = offsetof(SpriteVertex, position),
//                                              },
//        gpu::VertexInputAttributeDescription {
//                                              .location = 1,
//                                              .binding  = 0,
//                                              .format   = gpu::PixelFormat::RG32F,
//                                              .offset   = offsetof(SpriteVertex,                     uv),
//                                              },
//    };

//    constexpr auto SPRITE_VERTEX_BUFFER_SIZE = SPRITE_VERTEX_SIZE * 4;
// } // namespace

//    //////////////////////////////////////
//    //////////////////////////////////////
//    auto SpriteRenderer::do_init() noexcept -> gpu::Expected<void> {
//        m_render_data = core::allocate_unsafe<RenderData>();
//        auto device   = as_ref(m_renderer->device());

//    return gpu::Shader::load_from_bytes(m_renderer->device(), QUAD_SPRITE_SHADER, gpu::ShaderStageFlag::VERTEX)
//      .transform(cm::set(m_render_data->vertex_shader))
//      .and_then(bind_front(gpu::Shader::load_from_bytes, device, QUAD_SPRITE_SHADER, gpu::ShaderStageFlag::FRAGMENT))
//      .transform(cm::set(m_render_data->fragment_shader))
//      .and_then(bind_front(gpu::PipelineLayout::create, device, gpu::RasterPipelineLayout {}))
//      .transform(cm::set(m_render_data->pipeline_layout))
//      .transform([this] noexcept {
//          m_render_data->pipeline_state = gpu::RasterPipelineState {
//               .input_assembly_state = {
//                   .topology = gpu::PrimitiveTopology::TRIANGLE_STRIP,
//               },
//               .viewport_state = {
//                   .viewports = {
//                       gpu::Viewport {
//                           .extent = m_viewport,
//                           .depth  = { 0.f, 1.f },
//                       },
//                   },
//                   .scissors  = {
//                       gpu::Scissor {
//                           .extent = m_viewport.to<u32>()
//                       },
//                   },
//               },
//               .color_blend_state = { .attachments = { {} } },
//               .shader_state      = to_refs(m_render_data->vertex_shader, m_render_data->fragment_shader),
//               .vertex_input_state = {
//                   .binding_descriptions = to_dyn_array(SPRITE_VERTEX_BINDING_DESCRIPTIONS),
//                   .input_attribute_descriptions = to_dyn_array(SPRITE_VERTEX_ATTRIBUTE_DESCRIPTIONS),
//               },
//            };
//      });
// }

//    //////////////////////////////////////
//    //////////////////////////////////////
//    auto SpriteRenderer::update_framegraph(FrameGraphBuilder& graph) noexcept -> void {
//        struct GeometryTransferTask {
//            Ref<GraphBuffer> staging_buffer;
//            Ref<GraphBuffer> vertex_buffer;
//        };

//    auto& vertex_buffer = graph.set_retained_resource("StormKit:VertexBuffer",
//                                                      BufferDescription {
//                                                        .size = SPRITE_VERTEX_BUFFER_SIZE,
//                                                      },
//                                                      *m_vertex_buffer);

//    auto transfer_task_data = OptionalRef<const GeometryTransferTask> {};
//    if (not m_vertex_buffer) {
//        m_vertex_buffer = gpu::Buffer::create(m_renderer->device(),
//                                              {
//                                                .usages   = gpu::BufferUsageFlag::VERTEX | gpu::BufferUsageFlag::TRANSFER_DST,
//                                                .size     = SPRITE_VERTEX_BUFFER_SIZE,
//                                                .property = gpu::MemoryPropertyFlag::DEVICE_LOCAL,
//                                              })
//                            .transform_error(cm::assert())
//                            .value();

//    const auto& task = graph.add_transfer_task<GeometryTransferTask>(
//      "StormKit:SpriteGeometryTranferTask",
//      [&, this](GeometryTransferTask& task_data, GraphTaskBuilder& builder) noexcept {
//          task_data.staging_buffer = as_ref_mut(builder.create("StagingBuffer",
//                                                               BufferDescription {
//                                                                 .size = SPRITE_VERTEX_BUFFER_SIZE,
//                                                               }));
//          task_data.vertex_buffer  = as_ref_mut(builder.write(vertex_buffer));
//      },
//      [](const GeometryTransferTask&  task_data,
//         OptionalRef<gpu::RenderPass> _,
//         gpu::CommandBuffer&          cmb,
//         const BakedFrameGraph::Data& graph_data) static noexcept {
//          const auto& staging_buffer = graph_data.get_actual_resource(*task_data.staging_buffer);
//          auto&       vertex_buffer  = graph_data.get_actual_resource(*task_data.vertex_buffer);

//    cmb.copy_buffer(staging_buffer, vertex_buffer, SPRITE_VERTEX_BUFFER_SIZE);
// });
// transfer_task_data = as_opt_ref(graph.get_task_data<GeometryTransferTask>(task.data_id()));
// m_dirty            = false;
// }

//    struct DrawTask {
//        Ref<GraphBuffer> vertex_buffer;
//        Ref<GraphImage>  backbuffer;
//    };

//    auto&& _ = graph.add_raster_task<DrawTask>(
//      "StormKit:SpriteRenderTask",
//      [&](DrawTask& task_data, GraphTaskBuilder& builder) noexcept {
//          // task_data.backbuffer = as_ref_mut(builder.create("color",
//          //                                                  engine::ImageDescription {
//          //                                                    .extent = m_viewport.to<u32>(),
//          //                                                    .type   = gpu::ImageType::T2D,
//          //                                                    .format = gpu::PixelFormat::BGRA8_UNORM,
//          //                                                  }));

//    // if (transfer_task_data) task_data.vertex_buffer = as_ref_mut(builder.read(*transfer_task_data->vertex_buffer));
//    // else
//    //     task_data.vertex_buffer = as_ref_mut(builder.read(vertex_buffer));

//    // graph.set_final_resource(task_data.backbuffer->id());
// },
// [this](const DrawTask&              task,
//       OptionalRef<gpu::RenderPass> render_pass,
//       gpu::CommandBuffer&          cmb,
//       const BakedFrameGraph::Data& task_data) noexcept {
//    if (not m_render_data->pipeline) {
//        m_render_data
//          ->pipeline = gpu::Pipeline::create(m_renderer->device(),
//                                             m_render_data->pipeline_state,
//                                             m_render_data->pipeline_layout,
//                                             *render_pass)
//                         .transform_error(cm::assert("Failed to create pipeline"))
//                         .value();
//    }
//    auto buffers = as_refs(m_vertex_buffer);

//    cmb.bind_pipeline(m_render_data->pipeline);
//    cmb.bind_vertex_buffers(buffers, {});
//    for (auto&& [_, sprite_data] : m_sprites) cmb.draw(std::size(sprite_data.sprite.vertices));
// },
// true);
// }
// } // namespace stormkit::engine
