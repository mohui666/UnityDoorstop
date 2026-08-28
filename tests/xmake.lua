if is_plat("windows") then
    target("config-util-regressions")
        set_kind("binary")
        set_default(false)
        set_group("tests")
        set_languages("c11")
        add_defines("UNICODE", "_UNICODE")
        add_includedirs("../src")
        add_files("windows/config-util-regressions.c")
        add_files("windows/test-crt.c")
        add_files("../src/bootstrap.c")
        add_files("../src/windows/config.c")
        add_files("../src/windows/util.c")
        add_files("../src/config/common.c")
        add_files("../src/runtimes/globals.c")
        add_cxflags("/Gy", {tools = "cl"})
        add_cxflags("-ffunction-sections", "-fdata-sections",
                    {tools = {"gcc", "clang"}})
        add_ldflags("/OPT:REF", {tools = "link"})
        add_ldflags("-Wl,--gc-sections", {tools = {"gcc", "clang"}})
        add_links("shell32", "kernel32")
        add_tests("default")

    target("iat-hook-regressions")
        set_kind("binary")
        set_default(false)
        set_group("tests")
        set_languages("c11")
        add_includedirs("../src")
        add_files("windows/iat-hook-regressions.c")
        add_links("kernel32")
        add_tests("default")
end

if is_plat("linux") then
    target("unityplayer-dup2-fixture")
        set_kind("shared")
        set_default(false)
        set_group("tests/fixtures")
        set_filename("UnityPlayer.so")
        set_languages("c11")
        set_warnings("allextra", "error")
        add_files("nix/fixtures/unityplayer-dup2.c")

    target("unityplayer-dup2-smoke")
        set_kind("binary")
        set_default(false)
        set_group("tests/fixtures")
        set_languages("c11")
        set_warnings("allextra", "error")
        add_deps("unityplayer-dup2-fixture")
        add_files("nix/fixtures/unityplayer-dup2-smoke.c")
        -- The fixture must keep its UnityPlayer basename for Doorstop's
        -- module lookup, so link the non-lib-prefixed output by full path.
        after_load(function (target)
            target:add("ldflags",
                       target:dep("unityplayer-dup2-fixture"):targetfile(),
                       {force = true})
        end)

    target("nix-regressions")
        set_kind("phony")
        set_default(false)
        set_group("tests")
        add_deps("doorstop", "unityplayer-dup2-fixture",
                 "unityplayer-dup2-smoke")
        add_tests("default")
        on_test(function (target)
            os.vrunv("sh", {
                path.join(os.projectdir(), "tests", "nix",
                          "run-regressions.sh"),
                target:dep("doorstop"):targetfile(),
                target:dep("unityplayer-dup2-fixture"):targetfile(),
                target:dep("unityplayer-dup2-smoke"):targetfile()
            })
            return true
        end)
end

if is_plat("macosx") then
    target("macos-unityplayer-interpose")
        set_kind("shared")
        set_default(false)
        set_group("tests/fixtures")
        set_filename("UnityPlayer.dylib")
        set_languages("c11")
        set_warnings("allextra", "error")
        add_files("nix/fixtures/macos-unityplayer-interpose.c")
        add_shflags("-Wl,-install_name,@rpath/UnityPlayer.dylib",
                    {force = true})

    target("macos-dlsym-smoke")
        set_kind("binary")
        set_default(false)
        set_group("tests/fixtures")
        set_languages("c11")
        set_warnings("allextra", "error")
        add_deps("macos-unityplayer-interpose")
        add_files("nix/fixtures/macos-dlsym-smoke.c")
        add_ldflags("-Wl,-export_dynamic", "-Wl,-rpath,@loader_path",
                    {force = true})
        -- The caller guard requires the image basename to start with
        -- UnityPlayer, so link the non-lib-prefixed dylib by full path.
        after_load(function (target)
            target:add("ldflags",
                       target:dep("macos-unityplayer-interpose"):targetfile(),
                       {force = true})
        end)

    target("macos-interpose-regressions")
        set_kind("phony")
        set_default(false)
        set_group("tests")
        add_deps("doorstop_x86_64", "doorstop_arm64",
                 "macos-unityplayer-interpose", "macos-dlsym-smoke")
        add_tests("default")
        on_test(function (target)
            local arm64_target = target:dep("doorstop_arm64")
            local mode = path.filename(arm64_target:targetdir())
            local platform_dir =
                path.directory(path.directory(arm64_target:targetdir()))
            local universal_doorstop = path.join(
                platform_dir, "universal", mode, "libdoorstop.dylib")

            os.vrunv("sh", {
                path.join(os.projectdir(), "tests", "nix",
                          "run-macos-interpose-smoke.sh"),
                universal_doorstop,
                target:dep("macos-unityplayer-interpose"):targetfile(),
                target:dep("macos-dlsym-smoke"):targetfile()
            })
            return true
        end)
end
