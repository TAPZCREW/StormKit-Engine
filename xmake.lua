add_rules("mode.debug", "mode.release", "mode.releasedbg")

add_repositories("tapzcrew-repo https://github.com/tapzcrew/xmake-repo main")

option("tests", { default = false, category = "root menu/build" })
option("sanitizers", { default = false, category = "root menu/build" })
option("mold", { default = false, category = "root menu/build" })
option("lto", { default = true, category = "root menu/build" })
option("shared_deps", { default = false, category = "root menu/build" })

option("stormkit", { description = "local stormkit folder", type = "string", category = "root menu/support" })
option("on_ci", { default = false, category = "root menu/support" })
option("compile_commands", { default = false, category = "root menu/support" })
option("vsxmake", { default = false, category = "root menu/support" })
option("devmode", {
    category = "root menu/support",
    deps = { "tests", "lto", "sanitizers" },
    after_check = function(option)
        if option:enabled() then
            for _, name in ipairs({ "tests", "sanitizers" }) do
                option:dep(name):enable(true)
            end
            option:dep("lto"):enable(false)
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
    includes("xmake/StormKit.xmake.lua")
    stormkit_dep_name = "dev_stormkit"
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
local is_libcpp = is_plat("linux") and has_config("runtimes") and get_config("runtimes"):startswith("c++")
add_requires("luau", {
    system = false,
    version = "upstream",
    configs = {
        shared = false,
        extern_c = true,
        build_cli = false,
        cxxflags = is_libcpp and { "-stdlib=libc++" } or nil,
        shflags = is_libcpp and { "-stdlib=libc++" } or nil,
        arflags = is_libcpp and { "-stdlib=libc++" } or nil,
    },
})
add_requires("libktx")
add_requires("libpng")
add_requires("libjpeg-turbo", is_plat("windows") and {
    system = false,
    configs = {
        runtimes = "MD",
        shared = true,
    },
} or {})
add_requires("sol2_luau", {
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
        "wayland-protocols"
    )
    add_requires("libxkbcommon", {
        system = false,
        configs = {
            wayland = true,
            x11 = true,
        },
    })
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

        shared = get_config("kind") == "shared",
        debug = is_mode("debug"),
        lto = get_config("lto"),
    },
    version = "dev",
    alias = "stormkit",
})

if is_mode("debug") or is_mode("reldbg") then add_cxflags("clang::-ggdb3") end

namespace("stormkit", function()
    includes("xmake/rules/*.xmake.lua")

    target("engine", function()
        set_kind("$(kind)")
        set_languages("cxxlatest", "clatest")

        add_rules("compile.shaders")

        set_basename("stormkit-engine")

        add_rules(stormkit_rule_prefix .. "stormkit::library")
        set_values("stormkit.components", { "stormkit", "log", "entities", "image", "wsi", "gpu", "lua" })

        add_includedirs("include")

        add_files("modules/stormkit/**.cppm", { public = true })
        add_files("src/**.cpp")
        add_files("shaders/**.nzsl")

        add_defines("STORMKIT_ENGINE_BUILD", { public = false })

        add_embeddirs("$(builddir)/shaders")
        add_cxflags("--embed-dir=$(builddir)/shaders")
    end)

    includes("game/xmake.lua")
end)
