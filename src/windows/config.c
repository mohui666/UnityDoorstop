#include "../config/config.h"
#include "../crt.h"
#include "../util/logging.h"
#include "../util/util.h"

#define CONFIG_NAME TEXT("doorstop_config.ini")
#define DEFAULT_TARGET_ASSEMBLY TEXT("Doorstop.dll")
#define DEFAULT_MONO_DEBUG_ADDRESS TEXT("127.0.0.1:10000")
#define DOORSTOP_ARG_PREFIX TEXT("--doorstop-")
#define EXE_EXTENSION_LENGTH 4
#define STR_EQUAL(str1, str2) (lstrcmpi(str1, str2) == 0)

static char_t *copy_string(const char_t *value) {
    if (!value)
        return NULL;

    const size_t len = strlen(value) + 1;
    char_t *result = malloc(sizeof(char_t) * len);
    strncpy(result, value, len);
    return result;
}

static bool_t is_doorstop_arg(const char_t *value) {
    const size_t prefix_len = STR_LEN(DOORSTOP_ARG_PREFIX) - 1;
    return value && strlen(value) >= prefix_len &&
           CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, value, prefix_len,
                         DOORSTOP_ARG_PREFIX, prefix_len) == CSTR_EQUAL;
}

void load_bool_file(const char_t *path, const char_t *section,
                    const char_t *key, const char_t *def, bool_t *value) {
    char_t enabled_string[256] = TEXT("true");
    GetPrivateProfileString(section, key, def, enabled_string, 256, path);
    LOG("CONFIG: %s.%s = %s", section, key, enabled_string);

    if (STR_EQUAL(enabled_string, TEXT("true")))
        *value = TRUE;
    else if (STR_EQUAL(enabled_string, TEXT("false")))
        *value = FALSE;
}

char_t *get_ini_entry(const char_t *config_file, const char_t *section,
                      const char_t *key, const char_t *default_val) {
    DWORD i = 0;
    DWORD size, read;
    char_t *result = NULL;
    do {
        if (result != NULL)
            free(result);
        i++;
        size = i * MAX_PATH + 1;
        result = malloc(sizeof(char_t) * size);
        read = GetPrivateProfileString(section, key, default_val, result, size,
                                       config_file);
    } while (read == size - 1);
    return result;
}

bool_t load_str_file(const char_t *path, const char_t *section,
                     const char_t *key, const char_t *def, char_t **value) {
    char_t *tmp = get_ini_entry(path, section, key, def);
    if (!tmp || strlen(tmp) == 0) {
        LOG("CONFIG: %s.%s is empty", section, key);
        if (tmp)
            free(tmp);
        return FALSE;
    }

    LOG("CONFIG: %s.%s = %s", section, key, tmp);
    if (*value)
        free(*value);
    *value = tmp;
    return TRUE;
}

void load_path_file(const char_t *path, const char_t *section,
                    const char_t *key, const char_t *def, char_t **value) {
    char_t *tmp = NULL;
    if (!load_str_file(path, section, key, def, &tmp))
        return;

    char_t *full_path = get_full_path(tmp);
    if (!full_path) {
        LOG("CONFIG: Failed to resolve %s.%s path: %s", section, key, tmp);
        free(tmp);
        return;
    }

    LOG("(%s.%s) %s => %s", section, key, tmp, full_path);
    if (*value)
        free(*value);
    *value = full_path;
    free(tmp);
}

static inline void init_config_file() {
    config.mono_debug_address = copy_string(DEFAULT_MONO_DEBUG_ADDRESS);

    if (!file_exists(CONFIG_NAME))
        return;

    char_t *config_path = get_full_path(CONFIG_NAME);
    if (!config_path) {
        LOG("Failed to resolve config file path");
        return;
    }

    load_bool_file(config_path, TEXT("General"), TEXT("enabled"), TEXT("true"),
                   &config.enabled);
    load_bool_file(config_path, TEXT("General"), TEXT("ignore_disable_switch"),
                   TEXT("false"), &config.ignore_disabled_env);
    load_bool_file(config_path, TEXT("General"), TEXT("redirect_output_log"),
                   TEXT("false"), &config.redirect_output_log);
    load_path_file(config_path, TEXT("General"), TEXT("target_assembly"),
                   DEFAULT_TARGET_ASSEMBLY, &config.target_assembly);
    load_path_file(config_path, TEXT("General"), TEXT("boot_config_override"),
                   NULL, &config.boot_config_override);

    load_str_file(config_path, TEXT("UnityMono"),
                  TEXT("dll_search_path_override"), TEXT(""),
                  &config.mono_dll_search_path_override);
    load_bool_file(config_path, TEXT("UnityMono"), TEXT("debug_enabled"),
                   TEXT("false"), &config.mono_debug_enabled);
    load_bool_file(config_path, TEXT("UnityMono"), TEXT("debug_suspend"),
                   TEXT("false"), &config.mono_debug_suspend);
    load_str_file(config_path, TEXT("UnityMono"), TEXT("debug_address"),
                  DEFAULT_MONO_DEBUG_ADDRESS, &config.mono_debug_address);

    load_path_file(config_path, TEXT("Il2Cpp"), TEXT("coreclr_path"), NULL,
                   &config.clr_runtime_coreclr_path);
    load_path_file(config_path, TEXT("Il2Cpp"), TEXT("corlib_dir"), NULL,
                   &config.clr_corlib_dir);

    free(config_path);
}

bool_t load_bool_argv(char_t **argv, int *i, int argc, const char_t *arg_name,
                      bool_t *value) {
    if (!argv || !i || *i < 0 || *i >= argc || !argv[*i] ||
        !STR_EQUAL(argv[*i], arg_name))
        return FALSE;

    if (*i + 1 >= argc || !argv[*i + 1] || is_doorstop_arg(argv[*i + 1])) {
        LOG("ARGV: Missing value for %s", arg_name);
        return TRUE;
    }

    char_t *par = argv[++*i];
    if (STR_EQUAL(par, TEXT("true")))
        *value = TRUE;
    else if (STR_EQUAL(par, TEXT("false")))
        *value = FALSE;
    else {
        LOG("ARGV: Invalid boolean value for %s: %s", arg_name, par);
        return TRUE;
    }

    LOG("ARGV: %s = %s", arg_name, par);
    return TRUE;
}

bool_t load_str_argv(char_t **argv, int *i, int argc, const char_t *arg_name,
                     char_t **value) {
    if (!argv || !i || *i < 0 || *i >= argc || !argv[*i] ||
        !STR_EQUAL(argv[*i], arg_name))
        return FALSE;

    if (*i + 1 >= argc || !argv[*i + 1] || is_doorstop_arg(argv[*i + 1])) {
        LOG("ARGV: Missing value for %s", arg_name);
        return TRUE;
    }

    char_t *new_value = copy_string(argv[++*i]);
    if (*value)
        free(*value);
    *value = new_value;
    LOG("ARGV: %s = %s", arg_name, *value);
    return TRUE;
}

bool_t load_path_argv(char_t **argv, int *i, int argc, const char_t *arg_name,
                      char_t **value) {
    if (!argv || !i || *i < 0 || *i >= argc || !argv[*i] ||
        !STR_EQUAL(argv[*i], arg_name))
        return FALSE;

    if (*i + 1 >= argc || !argv[*i + 1] || is_doorstop_arg(argv[*i + 1])) {
        LOG("ARGV: Missing value for %s", arg_name);
        return TRUE;
    }

    char_t *path = argv[++*i];
    if (strlen(path) == 0) {
        LOG("ARGV: Empty path for %s", arg_name);
        return TRUE;
    }

    char_t *full_path = get_full_path(path);
    if (!full_path) {
        LOG("ARGV: Failed to resolve path for %s: %s", arg_name, path);
        return TRUE;
    }

    LOG("(%s) %s => %s", arg_name, path, full_path);
    if (*value)
        free(*value);
    *value = full_path;
    return TRUE;
}

static inline void init_cmd_args() {
    char_t *args = GetCommandLine();
    int argc = 0;
    char_t **argv = CommandLineToArgv(args, &argc);
    if (!argv) {
        LOG("Failed to parse command line arguments");
        return;
    }

#define PARSE_ARG(name, dest, parser)                                          \
    if (parser(argv, &i, argc, name, &(dest)))                                 \
        continue;

    for (int i = 0; i < argc; i++) {
        PARSE_ARG(TEXT("--doorstop-enabled"), config.enabled, load_bool_argv);
        PARSE_ARG(TEXT("--doorstop-redirect-output-log"),
                  config.redirect_output_log, load_bool_argv);
        PARSE_ARG(TEXT("--doorstop-target-assembly"), config.target_assembly,
                  load_path_argv);
        PARSE_ARG(TEXT("--doorstop-boot-config-override"),
                  config.boot_config_override, load_path_argv);

        PARSE_ARG(TEXT("--doorstop-mono-dll-search-path-override"),
                  config.mono_dll_search_path_override, load_path_argv);
        PARSE_ARG(TEXT("--doorstop-mono-debug-enabled"),
                  config.mono_debug_enabled, load_bool_argv);
        PARSE_ARG(TEXT("--doorstop-mono-debug-suspend"),
                  config.mono_debug_suspend, load_bool_argv);
        PARSE_ARG(TEXT("--doorstop-mono-debug-address"),
                  config.mono_debug_address, load_str_argv);

        PARSE_ARG(TEXT("--doorstop-clr-corlib-dir"), config.clr_corlib_dir,
                  load_path_argv);
        PARSE_ARG(TEXT("--doorstop-clr-runtime-coreclr-path"),
                  config.clr_runtime_coreclr_path, load_path_argv);
    }

    LocalFree(argv);

#undef PARSE_ARG
}

static inline void init_env_vars() {
    char_t *disable_env = getenv(TEXT("DOORSTOP_DISABLE"));
    if (!config.ignore_disabled_env && disable_env != 0) {
        LOG("DOORSTOP_DISABLE is set! Disabling Doorstop!");
        config.enabled = FALSE;
    }
    shutenv(disable_env);
}

void load_config() {
    init_config_defaults();
    init_config_file();
    init_cmd_args();
    init_env_vars();
}
