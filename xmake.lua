add_rules("mode.debug", "mode.release", "mode.releasedbg")

add_repositories("tapzcrew-repo https://github.com/tapzcrew/xmake-repo main")

add_requires(
    "frozen",
    "unordered_dense",
    "tl_function_ref",
    "vulkan-headers v1.4.309",
    "volk",
    "vulkan-memory-allocator 3.2.1",
    {
        system = false,
    }
)

add_requireconfs("frozen", "unordered_dense", { configs = {
    modules = true,
} })

add_requires("stormkit develop", {
    configs = {
        image = true,
        wsi = true,
        log = true,
        entities = true,
        gpu = true,
        examples = false,
        tests = false,
    },
})

add_defines("ANKERL_UNORDERED_DENSE_STD_MODULE=1", "FROZEN_STD_MODULE=1")
target("StormKit-Engine", function()
    set_kind("$(kind)")
    set_languages("c++latest")

    add_files("modules/stormkit/**.mpp")
    add_files("src/**.cpp")

    add_packages("stormkit")
    add_packages("frozen", "unordered_dense", "tl_function_ref", "volk", "vulkan-headers", "vulkan-memory-allocator")
end)

includes("game/xmake.lua")
