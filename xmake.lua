add_rules("mode.debug", "mode.release", "mode.releasedbg")

add_repositories("tapzcrew-repo https://github.com/tapzcrew/xmake-repo main")

option("tests", { default = false, category = "root menu/build" })
option("sanitizers", { default = false, category = "root menu/build" })
option("mold", { default = false, category = "root menu/build" })
option("lto", { default = false, category = "root menu/build" })
option("shared_deps", { default = false, category = "root menu/build" })

option("stormkit", { description = "local stormkit folder", type = "string", category = "root menu/support" })
option("on_ci", { default = false, category = "root menu/support" })
option("compile_commands", { default = false, category = "root menu/support" })
option("vsxmake", { default = false, category = "root menu/support" })
option("devmode", {
    category = "root menu/support",
    deps = { "tests", "compile_commands", "mold", "sanitizers" },
    after_check = function(option)
        if option:enabled() then
            for _, name in ipairs({ "tests", "compile_commands", "mold", "sanitizers" }) do
                option:dep(name):enable(true)
            end
        end
    end,
})

if get_config("devmode") then
    -- set_policy("build.c++.modules.non_cascading_changes", true)
    set_policy("build.c++.modules.hide_dependencies", true)
end

if get_config("vsxmake") then add_rules("plugin.vsxmake.autoupdate") end

if get_config("compile_commands") then
    add_rules("plugin.compile_commands.autoupdate", { outputdir = "build", lsp = "clangd" })
end

local stormkit_dep_name = "stormkit"
stormkit_rule_prefix = "@stormkit/"
if has_config("stormkit") then
    -- includes("xmake/StormKit.xmake.lua")
    -- stormkit_rule_prefix = ""
    -- includes(path.join(get_config("stormkit"), "xmake", "rules", "**.lua"))
end

add_requires("frozen", {
    system = false,
    configs = {
        modules = true,
        std_import = true,
        cpp = "latest",
    },
})
add_requires("unordered_dense", {
    system = false,
    configs = {
        modules = true,
        std_import = true,
    },
})
add_requires("tl_function_ref", {
    system = false,
    configs = {
        modules = true,
        std_import = true,
    },
})
add_requires("luau", {
    system = false,
    version = "master",
    configs = {
        shared = false,
        extern_c = true,
        build_cli = false,
    },
})
add_requires("sol2", {
    system = false,
    version = "develop",
})
add_requires("volk", { version = "1.4.335" })
add_requires("vulkan-headers", {
    version = "1.4.335",
    system = false,
    configs = {
        modules = false,
    },
})
add_requires("vulkan-memory-allocator", {
    version = "v3.3.0",
    system = false,
})
if is_plat("linux") then
    add_requires(
        "libxcb",
        "xcb-util-keysyms",
        "xcb-util",
        "xcb-util-image",
        "xcb-util-wm",
        "xcb-util-errors",
        "wayland",
        "wayland-protocols",
        "libxkbcommon"
    )
end

add_requires(stormkit_dep_name, {
    configs = {
        log = true,
        wsi = true,
        entities = true,
        image = true,
        gpu = true,
        lua = true,

        examples = false,
        tests = false,
        tools = false,

        -- shared = true,
        debug = is_mode("debug"),
    },
    alias = "stormkit",
})

if is_mode("debug") or is_mode("reldbg") then add_cxflags("clang::-ggdb3") end

namespace("stormkit", function()
    target("engine", function()
        set_kind("$(kind)")
        set_languages("cxxlatest", "clatest")
        add_rules(stormkit_rule_prefix .. "stormkit::library")
        set_values("stormkit.components", { "log", "entities", "image", "wsi", "gpu", "lua" })

        add_includedirs("include")

        add_files("modules/stormkit/**.cppm", { public = true })
        add_files("src/**.cpp")

        add_defines("STORMKIT_ENGINE_BUILD", { public = false })
    end)

    includes("game/xmake.lua")
end)
