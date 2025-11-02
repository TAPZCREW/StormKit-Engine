target("Game", function()
    set_kind("binary")
    set_languages("c++latest")
    add_packages("stormkit")

    add_files("src/**.mpp")
    add_files("src/**.cpp")

    add_deps("StormKit-Engine")
    add_packages("stormkit")
    add_packages("frozen", "unordered_dense", "tl_function_ref", "volk", "vulkan-headers", "vulkan-memory-allocator")
end)
