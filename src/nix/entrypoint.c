#include "../bootstrap.h"
#include "../config/config.h"
#include "../crt.h"
#include "../util/logging.h"
#include "../util/paths.h"
#include "../util/util.h"
#include "./plthook/plthook.h"

#if defined(__APPLE__)
// <mach-o/dyld-interposing.h> ships with the dyld source tree, not the Xcode
// SDK, so define its stable DYLD_INTERPOSE macro locally.
#define DYLD_INTERPOSE(_replacement, _replacee)                                \
    __attribute__((used)) static struct {                                      \
        const void *replacement;                                               \
        const void *replacee;                                                  \
    } _interpose_##_replacee                                                   \
        __attribute__((section("__DATA,__interpose"))) = {                     \
            (const void *)(unsigned long)&_replacement,                        \
            (const void *)(unsigned long)&_replacee};
#define PLTHOOK_OPEN_BY_HANDLE_OR_ADDRESS plthook_open_by_handle
#else
#define PLTHOOK_OPEN_BY_HANDLE_OR_ADDRESS plthook_open_by_address
#endif

void capture_mono_path(void *handle) {
    char_t *result;
    get_module_path(handle, &result, NULL, 0);
    setenv(TEXT("DOORSTOP_MONO_LIB_PATH"), result, TRUE);
}

static bool_t initialized = FALSE;
static bool_t doorstop_ready = FALSE;

#if defined(__APPLE__)
static bool_t apple_caller_is_unity_player(void *return_address) {
    Dl_info info;
    if (!doorstop_ready || !config.enabled || !return_address ||
        dladdr(return_address, &info) == 0 || !info.dli_fname) {
        return FALSE;
    }

    const char *image_name = strrchr(info.dli_fname, '/');
    image_name = image_name ? image_name + 1 : info.dli_fname;
    static const char unity_player_name[] = "UnityPlayer";
    const size_t name_len = sizeof(unity_player_name) - 1;
    return strncmp(image_name, unity_player_name, name_len) == 0 &&
           (image_name[name_len] == '\0' || image_name[name_len] == '.');
}

#define APPLE_CALLER_IS_UNITY_PLAYER()                                        \
    apple_caller_is_unity_player(                                             \
        __builtin_extract_return_addr(__builtin_return_address(0)))
#endif

void *dlsym_hook(void *handle, const char *name) {
#define REDIRECT_INIT(init_name, init_func, target, extra_init)                \
    if (!strcmp(name, init_name)) {                                            \
        if (!initialized) {                                                    \
            initialized = TRUE;                                                \
            init_func(handle);                                                 \
            extra_init;                                                        \
            /* init_func probes many optional symbols, which clobbers the      \
               caller-visible dlerror state; drain it when the caller's own    \
               lookup succeeded. */                                            \
            if (res)                                                           \
                dlerror();                                                     \
        }                                                                      \
        return (void *)target;                                                 \
    }

    // Resolve dnsym always so that it can be passed to capture_mono_path.
    // On Unix, we use dladdr which allows to use arbitrary symbols for
    // resolving their location.
    // However, using handle seems to cause issues on some distros, so we pass
    // the resolved symbol instead.
    void *res = dlsym(handle, name);
    if (!doorstop_ready || !config.enabled) {
        return res;
    }
    REDIRECT_INIT("il2cpp_init", load_il2cpp_funcs, init_il2cpp, {});
    REDIRECT_INIT("mono_jit_init_version", load_mono_funcs, init_mono,
                  capture_mono_path(res));
    REDIRECT_INIT("mono_image_open_from_data_with_name", load_mono_funcs,
                  hook_mono_image_open_from_data_with_name,
                  capture_mono_path(res));
    REDIRECT_INIT("mono_jit_parse_options", load_mono_funcs,
                  hook_mono_jit_parse_options, capture_mono_path(res));
    REDIRECT_INIT("mono_debug_init", load_mono_funcs, hook_mono_debug_init,
                  capture_mono_path(res));

#undef REDIRECT_INIT
    return res;
}

int fclose_hook(FILE *stream) {
#if defined(__APPLE__)
    if (!APPLE_CALLER_IS_UNITY_PLAYER())
        return fclose(stream);
#endif
    // Some versions of Unity wrongly close stdout, which prevents writing
    // to console
    if (stream == stdout)
        return F_OK;
    return fclose(stream);
}

char_t *default_boot_config_path = NULL;
#if !defined(__APPLE__)
FILE *fopen64_hook(const char *filename, const char *mode) {
    const char *actual_file_name = filename;

    if (strcmp(filename, default_boot_config_path) == 0) {
        actual_file_name = config.boot_config_override;
        LOG("Overriding boot.config to %s", actual_file_name);
    }

    return fopen64(actual_file_name, mode);
}
#endif

FILE *fopen_hook(const char *filename, const char *mode) {
    const char *actual_file_name = filename;

#if defined(__APPLE__)
    if (!APPLE_CALLER_IS_UNITY_PLAYER())
        return fopen(filename, mode);
#endif

    if (filename && default_boot_config_path && config.boot_config_override &&
        strcmp(filename, default_boot_config_path) == 0) {
        actual_file_name = config.boot_config_override;
        LOG("Overriding boot.config to %s", actual_file_name);
    }

    return fopen(actual_file_name, mode);
}

int dup2_hook(int od, int nd) {
#if defined(__APPLE__)
    if (!APPLE_CALLER_IS_UNITY_PLAYER())
        return dup2(od, nd);
#endif
    // Newer versions of Unity redirect stdout to player.log, we don't want
    // that
    if (nd == fileno(stdout) || nd == fileno(stderr))
        return nd;
    return dup2(od, nd);
}

#if defined(__APPLE__)
// Modern Mach-O images commonly use chained fixups instead of a traditional
// lazy-symbol pointer table. Interpose the small set of required libc calls at
// dyld level. The stdio hooks above still act only for a UnityPlayer caller, so
// inherited injection cannot alter shell and launcher redirection semantics.
DYLD_INTERPOSE(dlsym_hook, dlsym)
DYLD_INTERPOSE(fopen_hook, fopen)
DYLD_INTERPOSE(fclose_hook, fclose)
DYLD_INTERPOSE(dup2_hook, dup2)
#endif

__attribute__((constructor)) void doorstop_ctor() {
    init_logger();
    load_config();
    if (config.ignore_disabled_env) {
        unsetenv("DOORSTOP_INITIALIZED");
        unsetenv("DOORSTOP_DISABLE");
        LOG("Cleared inherited DOORSTOP_INITIALIZED / DOORSTOP_DISABLE");
    }

    if (!config.enabled) {
        LOG("Doorstop not enabled! Skipping!");
        return;
    }
    doorstop_ready = TRUE;

    plthook_t *hook = NULL;
    bool_t hook_available = FALSE;
    bool_t hooking_unity_player = FALSE;

    void *unity_player = plthook_handle_by_name("UnityPlayer");

    if (unity_player &&
        PLTHOOK_OPEN_BY_HANDLE_OR_ADDRESS(&hook, unity_player) == 0) {
        hook_available = TRUE;
        hooking_unity_player = TRUE;
        LOG("Found UnityPlayer, hooking into it instead");
    } else if (plthook_open(&hook, NULL) == 0) {
        hook_available = TRUE;
    } else {
#if defined(__APPLE__)
        LOG("Failed to open a PLT hook target; continuing with dyld dlsym "
            "interposition. Error: %s",
            plthook_error());
#else
        LOG("Failed to open current process PLT! Cannot run Doorstop! "
            "Error: "
            "%s\n",
            plthook_error());
        return;
#endif
    }

#if !defined(__APPLE__)
    if (hook_available &&
        plthook_replace(hook, "dlsym", &dlsym_hook, NULL) != 0)
        LOG("Failed to hook dlsym, ignoring it. Error: %s",
               plthook_error());
#endif

    if (config.boot_config_override) {
        if (file_exists(config.boot_config_override)) {
            default_boot_config_path = calloc(MAX_PATH, sizeof(char_t));
            memset(default_boot_config_path, 0, MAX_PATH * sizeof(char_t));
            strcat(default_boot_config_path, get_working_dir());
            strcat(default_boot_config_path, TEXT("/"));
            strcat(default_boot_config_path,
                   get_file_name(program_path(), FALSE));
            strcat(default_boot_config_path, TEXT("_Data/boot.config"));

            if (hook_available) {
#if !defined(__APPLE__)
                if (plthook_replace(hook, "fopen64", &fopen64_hook, NULL) != 0)
                    LOG("Failed to hook fopen64, ignoring it. Error: %s",
                           plthook_error());
#endif
                if (plthook_replace(hook, "fopen", &fopen_hook, NULL) != 0)
                    LOG("Failed to hook fopen, ignoring it. Error: %s",
                           plthook_error());
            }
        } else {
            LOG("The boot.config file won't be overriden because the provided "
                "one does not exist: %s",
                config.boot_config_override);
        }
    }

    // Only suppress UnityPlayer's attempts to redirect or close standard
    // output. LD_PRELOAD is inherited by child processes, and applying these
    // hooks to their main executables breaks legitimate redirection such as
    // command substitution in sh.
    if (hooking_unity_player) {
        if (plthook_replace(hook, "fclose", &fclose_hook, NULL) != 0)
            LOG("Failed to hook fclose, ignoring it. Error: %s",
                   plthook_error());

        if (plthook_replace(hook, "dup2", &dup2_hook, NULL) != 0)
            LOG("Failed to hook dup2, ignoring it. Error: %s",
                   plthook_error());
    }

#if defined(__APPLE__)
    /*
        On older Unity versions, Mono methods are resolved by the OS's
       loader directly. Because of this, there is no dlsym, in which case we
       need to apply a PLT hook.
    */
    if (hook_available) {
        void *mono_handle = plthook_handle_by_name("libmono");

        if (plthook_replace(hook, "mono_jit_init_version", &init_mono, NULL) !=
            0)
            LOG("Failed to hook jit_init_version, ignoring it. This is "
                "probably fine unless you see other errors. Error: %s",
                   plthook_error());
        else if (mono_handle)
            load_mono_funcs(mono_handle);
    }
#endif

    if (hook_available)
        plthook_close(hook);
}
