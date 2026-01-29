// Copyright (C) 2024 Arthur LAURENT <arthur.laurent4@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level of this distribution

module stormkit.engine;

import std;

import stormkit.core;
import stormkit.gpu;

import :renderer.framegraph;

namespace stdr = std::ranges;
namespace stdv = std::views;

namespace sm = stormkit::monadic;

namespace stormkit::engine {
    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameGraphBuilder::bake() -> void {
        expects(not m_baked);

        for (auto& task : m_tasks) task.m_ref_count = std::size(task.creates()) + std::size(task.writes());

        for (auto& resource : m_resources)
            std::visit([](auto& resource) { resource.m_ref_count = std::size(resource.readers()); }, resource);

        cull_unreferenced_resources();
        build_physical_descriptions();

        m_baked = true;
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameGraphBuilder::create_framegraph(const gpu::Device&      device,
                                              const gpu::CommandPool& command_pool,
                                              BakedFrameGraph*        old) -> BakedFrameGraph {
        auto&& [backbuffer, data] = allocate_physical_resources(command_pool, device);

        return BakedFrameGraph { backbuffer, std::move(data), old };
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameGraphBuilder::allocate_framegraph(const gpu::Device&      device,
                                                const gpu::CommandPool& command_pool,
                                                BakedFrameGraph*        old) -> Heap<BakedFrameGraph> {
        auto&& [backbuffer, data] = allocate_physical_resources(command_pool, device);

        return allocate_unsafe<BakedFrameGraph>(backbuffer, std::move(data), old);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameGraphBuilder::prepare_task(GraphTask& task) noexcept -> void {
        auto task_builder = GraphTaskBuilder { task, *this };
        auto data         = m_datas[task.data_id()];
        task.on_setup(*std::bit_cast<Byte*>(&data), task_builder);
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameGraphBuilder::cull_unreferenced_resources() noexcept -> void {
        auto unreferenced_resources = std::stack<Ref<GraphResourceVariant>> {};

        constexpr auto decrementRefcount = [](auto& value) noexcept {
            if (value.m_ref_count > 0) --value.m_ref_count;
        };

        constexpr auto isUnreferenced = [](const auto& resource) noexcept {
            return resource.ref_count() == 0 and resource.transient();
        };

        constexpr auto shouldCull = [](const auto& task) noexcept { return task.ref_count() == 0 and not task.cull_imune(); };

        const auto cull = [&decrementRefcount, &isUnreferenced, &unreferenced_resources, this](auto& task) noexcept {
            for (const auto id : task.reads()) {
                auto& resource = get_resource(id);

                if (std::visit(
                      [&decrementRefcount, &isUnreferenced](auto& value) {
                          decrementRefcount(value);
                          return isUnreferenced(value);
                      },
                      resource))
                    unreferenced_resources.push(as_ref_mut(resource));
            }
        };

        for (auto& resource : m_resources)
            if (std::visit(isUnreferenced, resource)) unreferenced_resources.push(as_ref_mut(resource));

        while (!std::empty(unreferenced_resources)) {
            auto resource = unreferenced_resources.top();
            unreferenced_resources.pop();

            auto& creator = get_task(std::visit([](auto& resource) noexcept { return resource.creator(); }, *resource));
            decrementRefcount(creator);

            if (shouldCull(creator)) cull(creator);

            for (const auto id : creator.writes()) {
                auto& writer = get_task(id);

                decrementRefcount(writer);
                if (shouldCull(writer)) cull(writer);
            }
        }
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameGraphBuilder::build_physical_descriptions() noexcept -> void {
        auto layouts              = HashMap<GraphID, gpu::ImageLayout> {};
        m_preprocessed_framegraph = m_tasks
                                    | stdv::filter([](const auto& task) noexcept {
                                          return not(task.ref_count() == 0 and not task.cull_imune());
                                      })
                                    | stdv::transform([&layouts, this](const auto& task) noexcept -> decltype(auto) {
                                          return Pass { .id         = task.id(),
                                                        .type       = task.type(),
                                                        .renderpass = build_renderpass_physical_description(task, layouts),
                                                        .name       = task.name(),
                                                        .buffers    = build_buffer_physical_descriptions(task),
                                                        .images     = build_image_physical_descriptions(task) };
                                      })
                                    | stdr::to<std::vector>();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameGraphBuilder::build_image_physical_descriptions(const GraphTask& task) noexcept -> std::vector<ImageInfo> {
        return task.creates()
               | stdv::filter([this](const auto& id) noexcept {
                     const auto& resource = get_resource(id);

                     return is<GraphImage>(resource) and get_resource<ImageDescription>(id).transient();
                 })
               | stdv::transform([this](const auto& id) noexcept -> decltype(auto) {
                     const auto& resource    = get_resource<ImageDescription>(id);
                     const auto& description = resource.description();

                     const auto usages = [&description] noexcept {
                         if (gpu::is_depth_stencil_format(description.format))
                             return gpu::ImageUsageFlag::DEPTH_STENCIL_ATTACHMENT | gpu::ImageUsageFlag::TRANSFER_SRC;

                         return gpu::ImageUsageFlag::COLOR_ATTACHMENT | gpu::ImageUsageFlag::TRANSFER_SRC;
                     }();

                     const auto clear_value = [&description] noexcept -> gpu::ClearValue {
                         if (gpu::is_depth_stencil_format(description.format)) return gpu::ClearDepthStencil {};

                         return gpu::ClearColor {};
                     }();

                     const auto& name = resource.name();

                     return ImageInfo { .id = id,
                                      .create_info =
                                          gpu::Image::CreateInfo {
                                              .extent = description.extent.template to<3>(),
                                              .format = description.format,
                                              .layers = description.layers,
                                              .type   = description.type,
                                              .usages = usages,
                                          },
                                      .clear_value = clear_value,
                                      .name        = name };
                 })
               | stdr::to<std::vector>();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameGraphBuilder::build_buffer_physical_descriptions(const GraphTask& task) noexcept -> std::vector<BufferInfo> {
        return task.creates()
               | stdv::filter([this](const auto& id) noexcept {
                     const auto& resource = get_resource(id);

                     return is<GraphBuffer>(resource) and get_resource<BufferDescription>(id).transient();
                 })
               | stdv::transform([this](const auto& id) noexcept -> decltype(auto) {
                     const auto& resource    = get_resource<BufferDescription>(id);
                     const auto& description = resource.description();

                     const auto usages = gpu::BufferUsageFlag::TRANSFER_SRC | gpu::BufferUsageFlag::STORAGE;

                     const auto& name = resource.name();

                     return BufferInfo {
                         .id          = id,
                         .create_info = gpu::Buffer::CreateInfo { .usages = usages, .size = description.size },
                         //.setMemoryProperty(gpu::MemoryPropertyFlag::eDeviceLocal),
                         .name = name
                     };
                 })
               | stdr::to<std::vector>();
    }

    /////////////////////////////////////
    /////////////////////////////////////
    auto FrameGraphBuilder::build_renderpass_physical_description(const GraphTask&                    task,
                                                                  HashMap<GraphID, gpu::ImageLayout>& layouts) noexcept
      -> RenderPassData {
        auto to_remove = std::vector<GraphID> {};

        const auto creates = task.creates()
                             | stdv::filter([this](const auto resource_id) noexcept {
                                   const auto& resource = get_resource(resource_id);
                                   return is<GraphImage>(resource);
                               })
                             | stdv::transform([&, this](const auto id) noexcept {
                                   const auto& resource    = get_resource<ImageDescription>(id);
                                   const auto& description = resource.description();

                                   auto attachment_description = gpu::AttachmentDescription {
                                       .format             = description.format,
                                       .load_op            = gpu::AttachmentLoadOperation::CLEAR,
                                       .store_op           = gpu::AttachmentStoreOperation::STORE,
                                       .stencil_load_op    = gpu::AttachmentLoadOperation::DONT_CARE,
                                       .stencil_store_op   = gpu::AttachmentStoreOperation::DONT_CARE,
                                       .source_layout      = gpu::ImageLayout::UNDEFINED,
                                       .destination_layout = gpu::ImageLayout::COLOR_ATTACHMENT_OPTIMAL
                                   };

                                   if (is_depth_stencil_format(description.format)) [[unlikely]] {
                                       std::swap(attachment_description.load_op, attachment_description.stencil_load_op);
                                       std::swap(attachment_description.store_op, attachment_description.stencil_store_op);
                                       attachment_description.destination_layout = gpu::ImageLayout::
                                         DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                                   }

                                   layouts[id] = attachment_description.destination_layout;

                                   return attachment_description;
                               })
                             | stdr::to<std::vector>();

        const auto writes = task.writes()
                            | stdv::filter([this](const auto id) noexcept {
                                  const auto& resource = get_resource(id);
                                  return is<GraphImage>(resource);
                              })
                            | stdv::transform([&, this](const auto id) {
                                  const auto& resource    = get_resource<ImageDescription>(id);
                                  const auto& description = resource.description();

                                  auto attachment_description = gpu::AttachmentDescription {
                                      .format             = description.format,
                                      .load_op            = gpu::AttachmentLoadOperation::CLEAR,
                                      .store_op           = gpu::AttachmentStoreOperation::STORE,
                                      .stencil_load_op    = gpu::AttachmentLoadOperation::DONT_CARE,
                                      .stencil_store_op   = gpu::AttachmentStoreOperation::DONT_CARE,
                                      .source_layout      = layouts.at(id),
                                      .destination_layout = layouts.at(id)
                                  };

                                  if (is_depth_stencil_format(description.format)) [[unlikely]] {
                                      std::swap(attachment_description.load_op, attachment_description.stencil_load_op);
                                      std::swap(attachment_description.store_op, attachment_description.stencil_store_op);
                                      attachment_description.destination_layout = gpu::ImageLayout::
                                        DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                                  }

                                  layouts[id] = attachment_description.destination_layout;

                                  return attachment_description;
                              })
                            | stdr::to<std::vector>();

        const auto reads = task.reads()
                           | stdv::filter([this](const auto id) noexcept {
                                 const auto& resource = get_resource(id);
                                 return is<GraphImage>(resource);
                             })
                           | stdv::transform([&, this](const auto id) {
                                 const auto& resource    = get_resource<ImageDescription>(id);
                                 const auto& description = resource.description();

                                 auto attachment_description = gpu::AttachmentDescription {
                                     .format             = description.format,
                                     .load_op            = gpu::AttachmentLoadOperation::LOAD,
                                     .store_op           = gpu::AttachmentStoreOperation::DONT_CARE,
                                     .stencil_load_op    = gpu::AttachmentLoadOperation::DONT_CARE,
                                     .stencil_store_op   = gpu::AttachmentStoreOperation::DONT_CARE,
                                     .source_layout      = layouts.at(id),
                                     .destination_layout = layouts.at(id)
                                 };

                                 if (stdr::contains(task.writes(), id)) {
                                     to_remove.emplace_back(id);
                                     attachment_description.store_op = gpu::AttachmentStoreOperation::STORE;
                                 }

                                 if (is_depth_stencil_format(description.format)) [[unlikely]] {
                                     std::swap(attachment_description.load_op, attachment_description.stencil_load_op);
                                     std::swap(attachment_description.store_op, attachment_description.stencil_store_op);
                                     attachment_description.destination_layout = gpu::ImageLayout::
                                       DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                                 }

                                 layouts[id] = attachment_description.destination_layout;

                                 return attachment_description;
                             })
                           | stdr::to<std::vector>();

        auto output                    = RenderPassData {};
        output.description.attachments = move_and_concat(std::move(creates), std::move(reads), std::move(writes));

        auto color_refs = std::vector<gpu::Subpass::Ref> {};
        color_refs.reserve(std::size(output.description.attachments));

        auto depth_attachment_ref = std::optional<gpu::Subpass::Ref> {};
        // for (auto&& [i, attachment] : output.description.attachments | stdv::enumerate) {
        auto i = 0u;
        for (auto&& attachment : output.description.attachments) {
            if (is_depth_format(attachment.format))
                depth_attachment_ref = gpu::Subpass::Ref { .attachment_id = as<u32>(i), .layout = attachment.destination_layout };
            else
                color_refs
                  .emplace_back(gpu::Subpass::Ref { .attachment_id = as<u32>(i), .layout = attachment.destination_layout });

            ++i;
        }

        // TODO support multiple subpasses
        output.description.subpasses.emplace_back(gpu::Subpass {
          .bind_point            = task.type() == GraphTask::Type::RASTER ? gpu::PipelineBindPoint::GRAPHICS
                                                                          : gpu::PipelineBindPoint::COMPUTE,
          .color_attachment_refs = std::move(color_refs),
          .depth_attachment_ref  = std::move(depth_attachment_ref) });

        return output;
    }

    auto FrameGraphBuilder::allocate_physical_resources(const gpu::CommandPool& command_pool, const gpu::Device& device)
      -> std::pair<Ref<const gpu::Image>, BakedFrameGraph::Data> {
        using Data = BakedFrameGraph::Data;

        auto output = Data {};
        output.cmb  = *command_pool.create_command_buffer();
        device.set_object_name(*output.cmb, "FrameGraph:CommandBuffer:Main");

        output.semaphore = *gpu::Semaphore::create(device);
        device.set_object_name(*output.semaphore, "FrameGraph:Semaphore:Main");

        output.fence = *gpu::Fence::create_signaled(device);
        device.set_object_name(*output.fence, "FrameGraph:Fence:Main");

        output.tasks.reserve(std::size(m_preprocessed_framegraph));

        using ImagePtr  = const gpu::Image*;
        auto backbuffer = ImagePtr { nullptr };

        // TODO support of async Compute and Transfert queue
        for (auto&& pass : m_preprocessed_framegraph) {
            output.buffers.reserve(std::size(output.buffers) + std::size(pass.buffers));
            output.images.reserve(std::size(output.images) + std::size(pass.images));
            output.image_views.reserve(std::size(output.image_views) + std::size(pass.images));

            for (auto&& buffer : pass.buffers) {
                auto& gpu_buffer = output.buffers.emplace_back(gpu::Buffer::create(device, buffer.create_info)
                                                                 .transform_error(sm::assert())
                                                                 .value());
                device.set_object_name(gpu_buffer, std::format("FrameGraph:Buffer:{}", buffer.name));
            }

            auto extent       = math::Extent2<u32> {};
            auto clear_values = std::vector<gpu::ClearValue> {};
            auto attachments  = std::vector<Ref<const gpu::ImageView>> {};
            for (const auto& image : pass.images) {
                extent.width  = std::max(image.create_info.extent.width, extent.width);
                extent.height = std::max(image.create_info.extent.height, extent.height);

                clear_values.emplace_back(image.clear_value);
                auto& gpu_image = output.images.emplace_back(gpu::Image::create(device, image.create_info)
                                                               .transform_error(sm::assert())
                                                               .value());
                device.set_object_name(gpu_image, std::format("FrameGraph:Image:{}", image.name));

                if (image.id == m_final_resource) backbuffer = &gpu_image;

                auto& gpu_image_view = output.image_views.emplace_back(gpu::ImageView::create(device, gpu_image)
                                                                         .transform_error(sm::assert())
                                                                         .value());
                device.set_object_name(gpu_image_view, std::format("FrameGraph:ImageView:{}", image.name));

                attachments.emplace_back(as_ref(gpu_image_view));
            }

            expects(backbuffer != nullptr, "No final resource set !");

            auto renderpass = *gpu::RenderPass::create(device, pass.renderpass.description);
            device.set_object_name(renderpass, std::format("FrameGraph:RenderPass:{}", pass.name));

            auto framebuffer = *gpu::FrameBuffer::create(device, renderpass, extent, attachments);
            device.set_object_name(framebuffer, std::format("FrameGraph:FrameBuffer:{}", pass.name));

            auto cmb = *command_pool.create_command_buffer(gpu::CommandBufferLevel::SECONDARY);
            device.set_object_name(cmb, std::format("FrameGraph:CommandBuffer:{}", pass.name));

            auto& task = as<BakedFrameGraph::Data::RasterTask>(output.tasks.emplace_back(BakedFrameGraph::Data::RasterTask {
              .id           = pass.id,
              .cmb          = std::move(cmb),
              .clear_values = std::move(clear_values),
              .renderpass   = std::move(renderpass),
              .framebuffer  = std::move(framebuffer) }));

            task.cmb.begin(false, gpu::InheritanceInfo { as_ref(task.renderpass), 0, as_opt_ref(task.framebuffer) });
            auto&& graph_task = get_task(pass.id);
            graph_task.on_execute(m_datas[pass.id].front(), as_opt_ref_mut(renderpass), task.cmb, output);
            task.cmb.end();
        }

        output.cmb->begin();
        const auto visitors = Overloaded {
            [&output](const BakedFrameGraph::Data::RasterTask& task) {
                output.cmb->begin_render_pass(task.renderpass, task.framebuffer, task.clear_values, true);

                const auto command_buffers = as_refs<std::array>(task.cmb);
                output.cmb->execute_sub_command_buffers(command_buffers);
                output.cmb->end_render_pass();
            },
            [&output](const BakedFrameGraph::Data::ComputeTask& task) {
                const auto command_buffers = as_refs<std::array>(task.cmb);
                output.cmb->execute_sub_command_buffers(command_buffers);
            }
        };
        for (auto&& task : output.tasks) std::visit(visitors, task);
        output.cmb->end();

        return std::make_pair(as_ref(backbuffer), std::move(output));
    } // namespace stormkit::engine
} // namespace stormkit::engine
