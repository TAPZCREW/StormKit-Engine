target("Game", function()
    set_kind("binary")
    set_languages("c++latest")

    add_files("src/**.cppm")
    add_files("src/**.cpp")

    add_syslinks("dl")

    add_deps("StormKit-Engine")
    -- add_packages("luau")
    add_packages("stormkit", { components = { "core", "main", "log", "entities", "image", "wsi", "gpu", "luau" } })
    if get_config("devmode") then set_rundir("$(projectdir)/game") end

    if get_config("sanitizers") and is_mode("debug", "release", "releasedbg") then
        set_policy("build.sanitizer.address", true)
        set_policy("build.sanitizer.undefined", true)
    end
end)
