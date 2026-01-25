target("Game", function()
    set_kind("binary")
    set_languages("c++latest")

    add_files("src/**.cppm")
    add_files("src/**.cpp")

    add_deps("StormKit-Engine")
    add_packages(
        stormkit_dep_name,
        { components = { "core", "main", "log", "entities", "image", "wsi", "gpu", "luau" } }
    )
end)
