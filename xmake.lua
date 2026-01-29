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
if has_config("stormkit") then
    includes("xmake/StormKit.xmake.lua")
    stormkit_dep_name = "dev_stormkit"
end

local cxx_isystem = "--cxx-isystem"
local cxx_runtime = nil
if get_config("toolchain") == "llvm" then
    if get_config("sdk") then
        cxx_isystem = cxx_isystem .. path.join(get_config("sdk"), "include", "c++", "v1")
    elseif is_plat("linux") or is_plat("darwin") then
        cxx_isystem = cxx_isystem .. "/usr/include/c++/v1"
    end
    if get_config("runtimes") and get_config("runtimes"):startswith("c++") then cxx_runtime = "-stdlib=libc++" end
end

add_requires(
    "frozen",
    "unordered_dense",
    "tl_function_ref",
    "vulkan-headers 1.4.335",
    "volk",
    "vulkan-memory-allocator v3.3.0",
    "luabridge3 master",
    {
        system = false,
        runtimes = get_config("toolchain") == "llvm" and get_config("runtimes") or nil,
    }
)
add_requireconfs("frozen", "unordered_dense", { configs = {
    modules = true,
} })

add_requires("luau master", {
    configs = {
        extern_c = true,
        cxflags = { cxx_runtime },
    },
})

add_requires(stormkit_dep_name, {
    configs = {
        log = true,
        wsi = true,
        entities = true,
        image = true,
        gpu = true,
        luau = true,

        examples = false,
        tests = false,

        shared = true,
        debug = is_mode("debug"),
    },
    alias = "stormkit",
})

if is_mode("debug") or is_mode("reldbg") then add_cxflags("clang::-ggdb3") end

-- add_defines("ANKERL_UNORDERED_DENSE_STD_MODULE=1", "FROZEN_STD_MODULE=1")
target("StormKit-Engine", function()
    set_kind("$(kind)")
    set_languages("c++latest")

    add_files("modules/stormkit/**.mpp", { public = true })
    add_files("src/**.cpp")
    add_files("src/**.cppm")

    add_packages("stormkit", { components = { "core", "log", "entities", "image", "wsi", "gpu", "luau" } })
    add_packages(
        "frozen",
        "unordered_dense",
        "tl_function_ref",
        "volk",
        "vulkan-headers",
        "vulkan-memory-allocator",
        "luau",
        "luabridge3"
    )
end)

includes("game/xmake.lua")
