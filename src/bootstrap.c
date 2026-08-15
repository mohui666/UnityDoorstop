#include "bootstrap.h"
#include "config/config.h"
#include "crt.h"
#include "runtimes/coreclr.h"
#include "runtimes/il2cpp.h"
#include "runtimes/mono.h"
#include "util/logging.h"
#include "util/paths.h"
#include "util/util.h"

bool_t mono_debug_init_called = FALSE;
bool_t mono_is_net35 = FALSE;
static bool_t doorstop_bootstrapped = FALSE;

#define LOCALHOST_DEBUG_PREFIX TEXT("localhost:")
#define IPV4_LOOPBACK_DEBUG_PREFIX TEXT("127.0.0.1:")

static bool_t debug_address_uses_localhost(const char_t *address) {
    if (!address)
        return FALSE;

    for (size_t i = 0; i < STR_LEN(LOCALHOST_DEBUG_PREFIX) - 1; i++) {
        char_t actual = address[i];
        if (!actual)
            return FALSE;
        if (actual >= 'A' && actual <= 'Z')
            actual += 'a' - 'A';
        if (actual != LOCALHOST_DEBUG_PREFIX[i])
            return FALSE;
    }
    return TRUE;
}

void mono_doorstop_bootstrap(void *mono_domain) {
    if (doorstop_bootstrapped) {
        LOG("Doorstop already bootstrapped in this process, skipping!");
        return;
    }

    // Launchers such as Steam can copy this process marker into a newly
    // started game. The ignore switch already opts out of the equivalent
    // DOORSTOP_DISABLE check, so honor it here as well and use a process-local
    // flag to guard genuine re-entry.
    char_t *initialized_env = getenv(TEXT("DOORSTOP_INITIALIZED"));
    if (initialized_env && !config.ignore_disabled_env) {
        LOG("DOORSTOP_INITIALIZED is set! Skipping!");
        shutenv(initialized_env);
        return;
    }
    shutenv(initialized_env);
    doorstop_bootstrapped = TRUE;

    // Debugging does not require a managed bootstrap assembly. In debug-only
    // mode init_mono has already configured the debugger, so leave the runtime
    // alone instead of passing a null path to the file and environment APIs.
    if (!config.target_assembly || !file_exists(config.target_assembly)) {
        LOG("No target assembly configured; managed bootstrap skipped");
        return;
    }

    mono.thread_set_main(mono.thread_current());

    setenv(TEXT("DOORSTOP_INITIALIZED"), TEXT("TRUE"), TRUE);

    char_t *app_path = program_path();
    if (mono.domain_set_config) {
#define CONFIG_EXT TEXT(".config")
        char_t *config_path =
            calloc(strlen(app_path) + 1 + STR_LEN(CONFIG_EXT), sizeof(char_t));
        strcpy(config_path, app_path);
        strcat(config_path, CONFIG_EXT);
        char_t *folder_path = get_folder_name(app_path);

        char *config_path_n = narrow(config_path);
        char *folder_path_n = narrow(folder_path);

        LOG("Setting config paths: base dir: %s; config path: %s\n",
            folder_path, config_path);

        mono.domain_set_config(mono_domain, folder_path_n, config_path_n);

        free(folder_path);
        free(config_path);
        free(config_path_n);
        free(folder_path_n);
#undef CONFIG_EXT
    }

    setenv(TEXT("DOORSTOP_INVOKE_DLL_PATH"), config.target_assembly, TRUE);
    setenv(TEXT("DOORSTOP_PROCESS_PATH"), app_path, TRUE);

    char *assembly_dir = mono.assembly_getrootdir();
    char_t *norm_assembly_dir = widen(assembly_dir);

    mono.config_parse(NULL);

    LOG("Assembly dir: %s", norm_assembly_dir);
    setenv(TEXT("DOORSTOP_MANAGED_FOLDER_DIR"), norm_assembly_dir, TRUE);
    free(norm_assembly_dir);

    LOG("Opening assembly: %s", config.target_assembly);
    void *file = fopen(config.target_assembly, TEXT("r"));
    if (!file) {
        LOG("Failed to open assembly: %s", config.target_assembly);
        return;
    }

    size_t size = get_file_size(file);
    void *data = malloc(size);
    size_t bytes_read = fread(data, 1, size, file);
    fclose(file);
    if (bytes_read != size) {
        LOG("Failed to read complete assembly: %s (%d of %d bytes)",
            config.target_assembly, (int)bytes_read, (int)size);
        free(data);
        return;
    }

    LOG("Opened Assembly DLL (%d bytes); opening its main image", size);

    char *dll_path = narrow(config.target_assembly);
    MonoImageOpenStatus s = MONO_IMAGE_OK;
    void *image = mono.image_open_from_data_with_name(data, size, TRUE, &s,
                                                      FALSE, dll_path);
    free(data);
    if (s != MONO_IMAGE_OK) {
        LOG("Failed to load assembly image: %s. Got result: %d\n",
            config.target_assembly, s);
        return;
    }

    LOG("Image opened; loading included assembly");

    s = MONO_IMAGE_OK;
    mono.assembly_load_from_full(image, dll_path, &s, FALSE);
    free(dll_path);
    if (s != MONO_IMAGE_OK) {
        LOG("Failed to load assembly: %s. Got result: %d\n",
            config.target_assembly, s);
        return;
    }

    LOG("Assembly loaded; looking for Doorstop.Entrypoint:Start");
    void *desc = mono.method_desc_new("Doorstop.Entrypoint:Start", TRUE);
    void *method = mono.method_desc_search_in_image(desc, image);
    mono.method_desc_free(desc);
    if (!method) {
        LOG("Failed to find method Doorstop.Entrypoint:Start");
        return;
    }

    void *signature = mono.method_signature(method);
    unsigned int params = mono.signature_get_param_count(signature);
    if (params != 0) {
        LOG("Method has %d parameters; expected 0", params);
        return;
    }

    LOG("Invoking method %p", method);
    void *exc = NULL;
    mono.runtime_invoke(method, NULL, NULL, &exc);
    if (exc != NULL) {
        LOG("Error invoking code!");
        if (mono.object_to_string) {
            void *str = mono.object_to_string(exc, NULL);
            char *exc_str_n = mono.string_to_utf8(str);
            char_t *exc_str = widen(exc_str_n);
            LOG("Error message: %s", exc_str);
            LOG("\n");
            free(exc_str);
            mono.free(exc_str_n);
        }
    }
    LOG("Done");

    free(app_path);
}

void *init_mono(const char *root_domain_name, const char *runtime_version) {
    char_t *root_domain_name_w = widen(root_domain_name);
    char_t *runtime_version_w = widen(runtime_version);
    LOG("Starting mono domain \"%s\"", root_domain_name_w);
    LOG("Runtime version: %s", runtime_version_w);
    if (strlen(runtime_version_w) > 2 &&
        (runtime_version_w[1] == L'2' || runtime_version_w[1] == L'1')) {
        mono_is_net35 = TRUE;
    }
    free(root_domain_name_w);
    free(runtime_version_w);
    char *root_dir_n = mono.assembly_getrootdir();
    char_t *root_dir = widen(root_dir_n);
    LOG("Current root: %s", root_dir);

    LOG("Overriding mono DLL search path");

    size_t mono_search_path_len = strlen(root_dir) + 1;

    char_t *override_dir_full = NULL;
    char_t *config_path_value = config.mono_dll_search_path_override;
    bool_t has_override = config_path_value && strlen(config_path_value);
    if (has_override) {
        size_t path_start = 0;
        override_dir_full = calloc(MAX_PATH, sizeof(char_t));
        memset(override_dir_full, 0, MAX_PATH * sizeof(char_t));

        bool_t found_path = FALSE;
        for (size_t i = 0; i <= strlen(config_path_value); i++) {
            char_t current_char = config_path_value[i];
            if (current_char == *PATH_SEP || current_char == 0) {
                if (i <= path_start) {
                    path_start++;
                    continue;
                }

                size_t path_len = i - path_start;
                char_t *path = calloc(path_len + 1, sizeof(char_t));
                strncpy(path, config_path_value + path_start, path_len);
                path[path_len] = 0;

                char_t *full_path = get_full_path(path);

                if (!full_path) {
                    LOG("Ignoring invalid root path: %s", path);
                    free(path);
                    path_start = i + 1;
                    continue;
                }

                if (strlen(override_dir_full) + strlen(full_path) + 2 >
                    MAX_PATH) {
                    LOG("Ignoring this root path because its absolute version "
                        "is too long: %s",
                        full_path);
                    free(path);
                    free(full_path);
                    path_start = i + 1;
                    continue;
                }

                if (found_path) {
                    strcat(override_dir_full, PATH_SEP);
                }

                strcat(override_dir_full, full_path);
                LOG("Adding root path: %s", full_path);

                free(path);
                free(full_path);

                found_path = TRUE;
                path_start = i + 1;
            }
        }

        mono_search_path_len += strlen(override_dir_full) + 1;
    }

    char_t *mono_search_path = calloc(mono_search_path_len + 1, sizeof(char_t));
    if (override_dir_full && strlen(override_dir_full)) {
        strcat(mono_search_path, override_dir_full);
        strcat(mono_search_path, PATH_SEP);
    }
    strcat(mono_search_path, root_dir);

    LOG("Mono search path: %s", mono_search_path);
    char *mono_search_path_n = narrow(mono_search_path);
    mono.set_assemblies_path(mono_search_path_n);
    setenv(TEXT("DOORSTOP_DLL_SEARCH_DIRS"), mono_search_path, TRUE);
    free(mono_search_path);
    free(mono_search_path_n);
    if (override_dir_full) {
        free(override_dir_full);
    }

    hook_mono_jit_parse_options(0, NULL);

    bool_t debugger_already_enabled = mono_debug_init_called;
    if (mono.debug_enabled) {
        debugger_already_enabled |= mono.debug_enabled();
    }

    void *domain = NULL;
    if (config.mono_debug_enabled && !debugger_already_enabled) {
        LOG("Detected mono debugger is not initialized; initialized it");
        mono.debug_init(MONO_DEBUG_FORMAT_MONO);
    }
    domain = mono.jit_init_version(root_domain_name, runtime_version);

    mono_doorstop_bootstrap(domain);

    return domain;
}

void il2cpp_doorstop_bootstrap() {
    if (!config.clr_corlib_dir || !config.clr_runtime_coreclr_path) {
        LOG("No CoreCLR paths set, skipping loading");
        return;
    }

    if (!config.target_assembly || !file_exists(config.target_assembly)) {
        LOG("No target assembly configured; CoreCLR bootstrap skipped");
        return;
    }

    LOG("CoreCLR runtime path: %s", config.clr_runtime_coreclr_path);
    LOG("CoreCLR corlib dir: %s", config.clr_corlib_dir);

    if (!file_exists(config.clr_runtime_coreclr_path) ||
        !folder_exists(config.clr_corlib_dir)) {
        LOG("CoreCLR startup dirs are not set up skipping invoking Doorstop");
        return;
    }

    void *coreclr_module = dlopen(config.clr_runtime_coreclr_path, RTLD_LAZY);
    LOG("Loaded coreclr.dll: %p", coreclr_module);
    if (!coreclr_module) {
        LOG("Failed to load CoreCLR runtime!");
        return;
    }

    load_coreclr_funcs(coreclr_module);

    char_t *app_path = program_path();
    char *app_path_n = narrow(app_path);

    char_t *target_dir = get_folder_name(config.target_assembly);
    char_t *target_name = get_file_name(config.target_assembly, FALSE);
    char *target_name_n = narrow(target_name);

    char_t *app_paths_env =
        calloc(strlen(config.clr_corlib_dir) + 1 + strlen(target_dir) + 1,
               sizeof(char_t));
    strcat(app_paths_env, config.clr_corlib_dir);
    strcat(app_paths_env, PATH_SEP);
    strcat(app_paths_env, target_dir);
    const char *app_paths_env_n = narrow(app_paths_env);

    LOG("App path: %s", app_path);
    LOG("Target dir: %s", target_dir);
    LOG("Target name: %s", target_name);
    LOG("APP_PATHS: %s", app_paths_env);

    const char *props = "APP_PATHS";

    setenv(TEXT("DOORSTOP_INITIALIZED"), TEXT("TRUE"), TRUE);
    setenv(TEXT("DOORSTOP_INVOKE_DLL_PATH"), config.target_assembly, TRUE);
    setenv(TEXT("DOORSTOP_MANAGED_FOLDER_DIR"), config.clr_corlib_dir, TRUE);
    setenv(TEXT("DOORSTOP_PROCESS_PATH"), app_path, TRUE);
    setenv(TEXT("DOORSTOP_DLL_SEARCH_DIRS"), app_paths_env, TRUE);

    void *host = NULL;
    unsigned int domain_id = 0;
    int result = coreclr.initialize(app_path_n, "Doorstop Domain", 1, &props,
                                    &app_paths_env_n, &host, &domain_id);
    if (result != 0) {
        LOG("Failed to initialize CoreCLR: 0x%08x", result);
        return;
    }

    void (*startup)() = NULL;
    result = coreclr.create_delegate(host, domain_id, target_name_n,
                                     "Doorstop.Entrypoint", "Start",
                                     (void **)&startup);
    if (result != 0) {
        LOG("Failed to get entrypoint delegate: 0x%08x", result);
        return;
    }

    LOG("Invoking Doorstop.Entrypoint.Start()");
    startup();
}

int init_il2cpp(const char *domain_name) {
    char_t *domain_name_w = widen(domain_name);
    LOG("Starting IL2CPP domain \"%s\"", domain_name_w);
    free(domain_name_w);
    const int orig_result = il2cpp.init(domain_name);
    il2cpp_doorstop_bootstrap();
    return orig_result;
}

#define MONO_DEBUG_ARG_START                                                   \
    TEXT("--debugger-agent=transport=dt_socket,server=y,address=")
#define MONO_DEBUG_NO_SUSPEND TEXT(",suspend=n")
#define MONO_DEBUG_NO_SUSPEND_NET35 TEXT(",suspend=n,defer=y")

void hook_mono_jit_parse_options(int argc, char **argv) {
    char_t *debug_options = getenv(TEXT("DNSPY_UNITY_DBG2"));
    bool_t debug_options_from_env = debug_options != NULL;
    if (debug_options) {
        config.mono_debug_enabled = TRUE;
        LOG("Mono debugging enabled by DNSPY_UNITY_DBG2; overriding Doorstop "
            "debug options");
    }

    if (config.mono_debug_enabled) {
        LOG("Configuring mono debug server");

        int size = argc + 1;
        char **new_argv = calloc(size, sizeof(char *));
        if (argc > 0 && argv)
            memcpy(new_argv, argv, argc * sizeof(char *));

        if (!debug_options) {
            const char_t *debug_address = config.mono_debug_address;
            if (!debug_address || !debug_address[0]) {
                debug_address = TEXT("127.0.0.1:10000");
                LOG("Mono debug address is empty; using %s", debug_address);
            }

            const char_t *debug_address_suffix = NULL;
            size_t debug_address_len = strlen(debug_address);
            if (debug_address_uses_localhost(debug_address)) {
                debug_address_suffix =
                    debug_address + STR_LEN(LOCALHOST_DEBUG_PREFIX) - 1;
                debug_address_len =
                    STR_LEN(IPV4_LOOPBACK_DEBUG_PREFIX) - 1 +
                    strlen(debug_address_suffix);
                LOG("Normalizing Mono debug host localhost to 127.0.0.1");
            }

            size_t debug_args_len =
                STR_LEN(MONO_DEBUG_ARG_START) + debug_address_len;
            if (!config.mono_debug_suspend) {
                if (mono_is_net35) {
                    debug_args_len += STR_LEN(MONO_DEBUG_NO_SUSPEND_NET35);
                } else {
                    debug_args_len += STR_LEN(MONO_DEBUG_NO_SUSPEND);
                }
            }

            debug_options = calloc(debug_args_len + 1, sizeof(char_t));
            strcat(debug_options, MONO_DEBUG_ARG_START);
            if (debug_address_suffix) {
                strcat(debug_options, IPV4_LOOPBACK_DEBUG_PREFIX);
                strcat(debug_options, debug_address_suffix);
            } else {
                strcat(debug_options, debug_address);
            }
            if (!config.mono_debug_suspend) {
                if (mono_is_net35) {
                    strcat(debug_options, MONO_DEBUG_NO_SUSPEND_NET35);
                } else {
                    strcat(debug_options, MONO_DEBUG_NO_SUSPEND);
                }
            }
        }

        LOG("Debug options: %s", debug_options);

        char *debug_options_n = narrow(debug_options);
        new_argv[argc] = debug_options_n;
        mono.jit_parse_options(size, new_argv);

        if (debug_options_from_env)
            shutenv(debug_options);
        else
            free(debug_options);
        free(debug_options_n);
        free(new_argv);
    } else {
        mono.jit_parse_options(argc, argv);
    }
}

void *hook_mono_image_open_from_data_with_name(void *data,
                                               unsigned long data_len,
                                               int need_copy,
                                               MonoImageOpenStatus *status,
                                               int refonly, const char *name) {
    void *result = NULL;
    if (config.mono_dll_search_path_override) {
        char_t *name_wide = widen(name);
        char_t *name_file = get_file_name(name_wide, TRUE);
        free(name_wide);

        size_t name_file_len = strlen(name_file);
        size_t bcl_root_len = strlen(config.mono_dll_search_path_override);

        char_t *new_full_path =
            calloc(name_file_len + bcl_root_len + 2, sizeof(char_t));
        strcat(new_full_path, config.mono_dll_search_path_override);
        strcat(new_full_path, TEXT("/"));
        strcat(new_full_path, name_file);

        if (file_exists(new_full_path)) {
            void *file = fopen(new_full_path, TEXT("r"));
            if (file) {
                size_t size = get_file_size(file);
                void *buf = malloc(size);
                size_t bytes_read = fread(buf, 1, size, file);
                fclose(file);
                if (bytes_read == size) {
                    result = mono.image_open_from_data_with_name(
                        buf, size, need_copy, status, refonly, name);
                } else {
                    LOG("Failed to read complete override assembly: %s (%d "
                        "of %d bytes)",
                        new_full_path, (int)bytes_read, (int)size);
                }
                if (need_copy || bytes_read != size)
                    free(buf);
            }
        }
        free(new_full_path);
    }

    if (!result) {
        result = mono.image_open_from_data_with_name(data, data_len, need_copy,
                                                     status, refonly, name);
    }
    return result;
}

void hook_mono_debug_init(MonoDebugFormat format) {
    mono_debug_init_called = TRUE;
    mono.debug_init(format);
}
