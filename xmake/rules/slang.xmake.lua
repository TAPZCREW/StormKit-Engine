add_requires("wgpu-native", {
    system = false,
    configs = {
        naga = true,
    },
})

rule("wgsl.find_naga", function()
    on_config(function(target)
        import("core.project.project")
        import("core.tool.toolchain")
        import("lib.detect.find_tool")

        -- on windows+asan we need run envs because of .dll dependencies which may be not part of the PATH
        local envs
        if is_plat("windows") then
            local msvc = target:toolchain("msvc")
            if msvc and msvc:check() then envs = msvc:runenvs() end
        end
        target:data_set("wgsl_envs", envs)

        -- find wgsl binaries
        local wgsl = project.required_package("wgpu-native~host") or project.required_package("wgpu-native")
        local wgsldir
        if wgsl then
            wgsldir = path.join(wgsl:installdir(), "bin")
            local osenvs = os.getenvs()
            envs = envs or {}
            for env, values in pairs(wgsl:get("envs")) do
                local flatval = path.joinenv(values)
                local oldenv = envs[env] or osenvs[env]
                if not oldenv or oldenv == "" then
                    envs[env] = flatval
                elseif not oldenv:startswith(flatval) then
                    envs[env] = flatval .. path.envsep() .. oldenv
                end
            end
        end

        local naga = find_tool("naga", { version = false, paths = wgsldir, envs = envs, check = "--help" })
        assert(naga, "naga not found! please install wgsl package with naga enabled")

        target:data_set("naga", naga)
        target:data_set("naga_runenv", envs)
    end)
end)

-- Compile shaders to includables headers
rule("wgsl.compile.shaders", function()
    set_extensions(".wgsl")
    add_deps("wgsl.find_naga")
    before_buildcmd_file(function(target, batchcmds, shaderfile, opt)
        import("lib.detect.find_tool")
        import("utils.progress")

        local outputdir = path.join(import("core.project.config").builddir(), "shaders")
        local naga = target:data("naga")
        local runenvs = target:data("naga_runenv")
        assert(naga, "naga not found! please install wgsl package with naga enabled")

        local outputfile = path.join(outputdir or path.directory(shaderfile), path.basename(shaderfile) .. ".spv")

        -- add commands
        if progress.apply_target then opt.progress = progress.apply_target(target, opt.progress) end
        batchcmds:show_progress(opt.progress, "${color.build.object}compiling.shader %s", shaderfile)
        local argv = {
            "--spirv-version",
            "1.3",
            shaderfile,
        }
        if outputdir then
            batchcmds:mkdir(outputdir)
            local outputfile = path.join(outputdir, path.basename(shaderfile) .. ".spv")
            table.insert(argv, outputfile)
        end

        batchcmds:vrunv(naga.program, argv, { curdir = ".", envs = runenvs })

        -- add deps
        batchcmds:add_depfiles(shaderfile)
        batchcmds:add_depvalues(naga.version)
        batchcmds:set_depmtime(os.mtime(outputfile))
        batchcmds:set_depcache(target:dependfile(outputfile))
    end)
end)
