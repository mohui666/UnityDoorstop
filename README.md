<p align="center">
  <img height="256" width="256" src="assets/img/icon.png">
</p>

<h1 align="center">Unity Doorstop</h1>

[![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/NeighTools/UnityDoorstop/build-be.yml?branch=master)](https://github.com/NeighTools/UnityDoorstop/actions/workflows/build-be.yml)
[![nightly.link artifacts](https://img.shields.io/badge/Artifacts-nightly.link-blueviolet)](https://nightly.link/NeighTools/UnityDoorstop/workflows/build-be/master)

***

Doorstop is a tool to execute managed .NET assemblies inside Unity as early as possible.

**This is a total rewrite of UnityDoorstop 3. See [list of breaking changes](CHANGES.md) for more information.**

## Features

* **Runs first**: Doorstop runs its code before Unity can do so
* **Configurable**: An elementary configuration file allows you to specify your assembly to execute
* **Multiplatform**: Supports Windows, Linux, macOS
* **Debugger support**: Allows to debug managed assemblies in Visual Studio, Rider or dnSpy *without modifications to Mono*

## Unity runtime support

Doorstop supports executing .NET assemblies in both Unity Mono and Il2Cpp runtimes.
Depending on the runtime the game uses, Doorstop tries to run your assembly as follows:

* On Unity Mono, your assembly is executed in the same runtime. As a result
  * You don't need to include your custom Common Language Runtime (CLR); the one bundled with the game is used
  * Your assembly is run alongside other Unity code
  * You can access all Unity API directly
* On Il2Cpp, your assembly is executed in CoreCLR runtime because Il2Cpp cannot run managed assemblies. As a result:
  * You need to include .NET 6 or newer CoreCLR runtime with your managed assembly
  * Your assembly is run in a runtime that is isolated from Il2Cpp
  * You can access Il2Cpp runtime by interacting with its native `il2cpp_` API

## Building

Doorstop uses [xmake](https://xmake.io/) to build the project. To build, run `build.bat`, `build.ps1` or `build.sh`.

Available build options:

* `-with_logging`: build with logging enabled
* `-arch`: the architectures to build for, separated by commas (e.g. `-arch x86,x64`)
* `-debug`: build in debug mode (currently only for *nix)

> **Note:** Initial build times are usually slower because the build script automatically downloads and installs xmake.  
> On Unix, xmake is built directly from the source code.

## Minimal injection example

To have Doorstop inject your code, create `Entrypoint` class into `Doorstop` namespace.
Define a public static `Start` method in it:

```cs
using System.IO;

namespace Doorstop;

class Entrypoint
{
  public static void Start()
  {
      File.WriteAllText("doorstop_hello.log", "Hello from Unity!");
  }
}
```

You can then define any code you want in `Start`.

**NOTE:** On UnityMono, Doorstop bootstraps your assembly with a minimal number of assemblies and minimal configuration.
This early execution allows for some interesting tricks, like redirecting the loading of some game assemblies.
Bear also in mind that some of the Unity runtime is not initialized at such an early stage, limiting the code you can execute.
You might need to appropriately pause the execution of your code until the moment you want to modify the game.

### Doorstop environment variables

Doorstop sets some environment variables useful for code execution:

| Environment variable          | Description                                                                                                                      |
| ----------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| `DOORSTOP_INITIALIZED`        | Set to `TRUE` when Doorstop bootstraps a managed target. Use to determine if your code is run via Doorstop.                     |
| `DOORSTOP_INVOKE_DLL_PATH`    | Full path to the assembly executed by Doorstop.                                                                                  |
| `DOORSTOP_PROCESS_PATH`       | Path to the application executable where the injected assembly is run.                                                           |
| `DOORSTOP_MANAGED_FOLDER_DIR` | *UnityMono*: Path to the game's `Managed` folder. *Il2Cpp*: Path to CoreCLR's base class library folder.                         |
| `DOORSTOP_DLL_SEARCH_DIRS`    | Paths where the runtime searches for assemblies by default, separated by the OS-specific separator (`;` on Windows and `:` on Unix). |
| `DOORSTOP_MONO_LIB_PATH`      | *Only on UnityMono*: Full path to the mono runtime library.                                                                      |

### Debugging

Doorstop 4 supports debugging the assemblies in the runtime.

#### Debugging in UnityMono

To enable debugging, set `debug_enabled` to `true` and optionally change the debug server address via `debug_address` (see [configuration options](#doorstop-configuration)).  
After launching the game, you may connect to the debugger using the server address (default is `127.0.0.1:10000`).  
By default, the game won't wait for the debugger to connect; you may change the behaviour with the `debug_suspend` option.

> **If you use dnSpy**, you can use the `Debug > Start Debugging > Debug engine > Unity` option, automatically setting the correct debugging configuration.  
> Doorstop detects dnSpy and automatically enables debugging without any extra configuration.

#### Debugging in Il2Cpp

Debugging is automatically enabled in CoreCLR. 

To start debugging, compile your DLL in debug mode (with embedded or portable symbols) and start the game with the debugger of your choice.  
Alternatively, attach a debugger to the game once it is running. All standard CoreCLR debuggers should detect the CoreCLR runtime in the game.

Moreover, hot reloading is supported for Visual Studio, Rider and other debuggers with .NET 6 hot reloading feature enabled.

**Note that you can only debug managed code!** Because the game code is unmanaged (i.e. Il2Cpp), you cannot directly debug the actual game code.
Consider using native debuggers like GDB and visual debugging tools like IDA or Ghidra to debug actual game code.

## Doorstop configuration

Doorstop is highly configurable based on your needs and the environment you want to use.
There are two ways to configure Doorstop: via config and CLI arguments.

### Via configuration file

Refer to [`doorstop_config.ini`](assets/windows/doorstop_config.ini) (Windows) or [`run.sh`](assets/nix/run.sh) for all available configuration options.

### CLI arguments

The following CLI arguments are available on both *nix, and Windows builds:

All Doorstop arguments start with `--doorstop-` and always contain an argument. The arguments can be of the following type:

* `bool` = `true` or `false`
* `string` = any sequence of characters and numbers. Wrap into `"`s if the string contains spaces

| Argument                                          | Description                                                                                          |
| ------------------------------------------------- |------------------------------------------------------------------------------------------------------|
| `--doorstop-enabled bool`                         | Enable or disable Doorstop.                                                                          |
| `--doorstop-redirect-output-log bool`             | *Only on Windows*: If `true` Unity's output log is redirected to `<current folder>\output_log.txt`   |
| `--doorstop-target-assembly string`               | Path to the assembly to load and execute.                                                            |
| `--doorstop-boot-config-override string`          | Overrides the boot.config file path.                                                                 |
| `--doorstop-mono-dll-search-path-override string` | Overrides default Mono DLL search path                                                               |
| `--doorstop-mono-debug-enabled bool`              | If true, Mono debugger server will be enabled                                                        |
| `--doorstop-mono-debug-suspend bool`              | Whether to suspend the game execution until the debugger is attached.                                |
| `--doorstop-mono-debug-address string`            | The address to use for the Mono debugger server.                                                     |
| `--doorstop-clr-corlib-dir string`                | Path to coreclr library that contains the CoreCLR runtime                                            |
| `--doorstop-clr-runtime-coreclr-path string`      | Path to the directory containing the managed core libraries for CoreCLR (`mscorlib`, `System`, etc.) |

## Troubleshooting and compatibility

### Choosing a Windows proxy DLL

Windows builds can be installed as `winhttp.dll`, `version.dll`, or
`dxgi.dll`. If a game or launcher uses WinHTTP during very early startup,
proxying `winhttp.dll` can conflict with that initialization. Rename the
Doorstop proxy to `dxgi.dll` (or `version.dll`) and keep
`doorstop_config.ini` beside it. This is particularly useful for games that
silently exit before Doorstop reaches the managed bootstrap.
Install only one of these proxy DLL names at a time.

### Native Unix games, Proton, and relative paths

`run.sh` is for native Linux and macOS executables. A Windows PE executable
running through Wine or Proton must use the Windows Doorstop build and a
Windows proxy DLL instead.

Relative executable and `target_assembly` paths in `run.sh` are resolved from
the script directory, so the script can be launched from another working
directory. Each relative entry in the `dll_search_path_override` list is
resolved from that directory as well. The target assembly's parent directory
is not added to Mono's search path automatically. Separate multiple paths with
`;` on Windows and `:` on Unix.

### Debug-only mode

Mono debugging can be enabled without loading a target assembly. Set
`debug_enabled=true` (or `debug_enable=1` in `run.sh`) and leave
`target_assembly` empty. Doorstop installs the Mono initialization hook and
configures the debugger, then skips the managed entrypoint.

Doorstop accepts `localhost` for the Mono debugger and canonicalizes it to
`127.0.0.1`; using `127.0.0.1` in the IDE avoids an IPv6 (`::1`) DNS choice on
clients that do not retry IPv4. An explicitly configured IPv6 listener remains
distinct. Doorstop also honors dnSpy's `DNSPY_UNITY_DBG2` environment variable,
so clear a stale value when debugging is unexpectedly enabled.

On modern macOS Mach-O images that use chained fixups, Doorstop uses dyld
interposition for runtime symbol lookup, boot.config access, and UnityPlayer's
stdout protection. Those stdio hooks verify that their caller is UnityPlayer,
so an inherited `DYLD_INSERT_LIBRARIES` value does not change shell or launcher
redirection. Legacy Mono players that bind initialization directly continue to
use the traditional Mach-O lazy-bind path.

Reconnect behavior after an IDE disconnect is implemented by the Mono runtime
bundled with the game. Some older Unity Mono versions do not reliably reopen
their listener; Doorstop cannot replace that runtime-side connection loop.

### Games that restart themselves

Steam and self-restarting games can copy `DOORSTOP_DISABLE` and
`DOORSTOP_INITIALIZED` into the replacement process. Set
`ignore_disable_switch=true` on Windows, or `ignore_disable_switch=1` in
`run.sh`, when the launcher is known to do this. Doorstop then clears both
inherited markers; the Mono bootstrap also uses a process-local guard to
prevent genuine duplicate initialization.

### Waiting until a game assembly is loaded

Doorstop deliberately invokes `Doorstop.Entrypoint.Start()` before game
assemblies. A target that needs types from `Assembly-CSharp` can use the
managed assembly-load event instead of a version-specific native hook:

```cs
AppDomain.CurrentDomain.AssemblyLoad += (_, eventArgs) =>
{
    if (eventArgs.LoadedAssembly.GetName().Name == "Assembly-CSharp")
        OnGameAssemblyLoaded();
};
```

Register the handler in `Start()` and make the callback one-shot if the loader
must run only once.

### RenderDoc and other native hook tools

Doorstop preserves and calls through a pre-existing `GetProcAddress` IAT hook,
which allows common RenderDoc/apitrace injection orders to coexist. Tools that
replace other required import-table entries can still conflict; in that case,
start through Doorstop first, load or attach the graphics tool from the managed
entrypoint, and then capture the running process.


## License

Doorstop 4 is licensed under LGPLv2.1. You can view the entire license [here](LICENSE).  
You can still access the source code to the original UnityDoorstop 3 source (licensed under CC0) from [the legacy branch](https://github.com/NeighTools/UnityDoorstop/tree/legacy).
