target("game", function()
    set_kind("binary")
    set_languages("cxxlatest", "clatest")

    add_rules("compile.shaders")

    add_rules(stormkit_rule_prefix .. "stormkit::application")
    set_values("stormkit.components", { "log", "entities", "image", "wsi", "gpu", "lua" })

    add_rules("platform.windows.subsystem")
    set_values("windows.subsystem", "console")

    add_files("src/**.cppm")
    add_files("src/**.cpp")
    -- add_files("shaders/*.nzsl")
    --
    add_packages("libjpeg-turbo")

    add_deps("stormkit::engine")

    on_load(function(target)
        if get_config("devmode") then
            import("core.project.config")
            local shader_dir = path.unix(path.join(config.builddir(), "shaders"))
            local lua_dir = path.unix(path.join(os.projectdir(), "game", "lua"))
            target:add("defines", format('SHADER_DIR="%s"', shader_dir))
            target:add("defines", format('LUA_DIR="%s"', lua_dir))
        end
    end)

    if get_config("devmode") then set_rundir("$(projectdir)/game") end
end)
