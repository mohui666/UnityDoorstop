#include "../../src/bootstrap.h"
#include "../../src/config/config.h"
#include "../../src/crt.h"
#include "../../src/util/util.h"

extern bool_t load_bool_argv(char_t **argv, int *i, int argc,
                             const char_t *arg_name, bool_t *value);
extern bool_t load_str_argv(char_t **argv, int *i, int argc,
                            const char_t *arg_name, char_t **value);
extern bool_t load_path_argv(char_t **argv, int *i, int argc,
                             const char_t *arg_name, char_t **value);

#define CHECK(condition, code)                                                \
    if (!(condition))                                                         \
        return code

static char_t *owned_string(const char_t *value) {
    const size_t len = strlen(value) + 1;
    char_t *result = malloc(len * sizeof(char_t));
    strncpy(result, value, len);
    return result;
}

static bool_t debug_options_canonicalized;
static const char *expected_debug_options;
static bool_t mono_thread_api_called;

static void *capture_debug_options(int argc, char **argv) {
    debug_options_canonicalized =
        argc == 1 && argv && argv[0] && expected_debug_options &&
        lstrcmpA(argv[0], expected_debug_options) == 0;
    return NULL;
}

static void *capture_thread_current(void) {
    mono_thread_api_called = TRUE;
    return NULL;
}

static void capture_thread_set_main(void *thread) {
    (void)thread;
    mono_thread_api_called = TRUE;
}

int main(void) {
    init_crt();

    char_t *missing_bool[] = {TEXT("--flag")};
    int index = 0;
    bool_t bool_value = TRUE;
    CHECK(load_bool_argv(missing_bool, &index, 1, TEXT("--flag"),
                         &bool_value),
          1);
    CHECK(index == 0 && bool_value == TRUE, 2);

    char_t *bool_before_option[] = {TEXT("--flag"),
                                    TEXT("--doorstop-enabled")};
    index = 0;
    CHECK(load_bool_argv(bool_before_option, &index, 2, TEXT("--flag"),
                         &bool_value),
          21);
    CHECK(index == 0 && bool_value == TRUE, 22);

    char_t *invalid_bool[] = {TEXT("--flag"), TEXT("not-a-bool")};
    index = 0;
    CHECK(load_bool_argv(invalid_bool, &index, 2, TEXT("--flag"), &bool_value),
          3);
    CHECK(index == 1 && bool_value == TRUE, 4);

    char_t *valid_bool[] = {TEXT("--flag"), TEXT("false")};
    index = 0;
    CHECK(load_bool_argv(valid_bool, &index, 2, TEXT("--flag"), &bool_value),
          5);
    CHECK(index == 1 && bool_value == FALSE, 6);

    char_t *missing_string[] = {TEXT("--string")};
    char_t *string_value = owned_string(TEXT("default"));
    char_t *original_string = string_value;
    index = 0;
    CHECK(load_str_argv(missing_string, &index, 1, TEXT("--string"),
                        &string_value),
          7);
    CHECK(index == 0 && string_value == original_string, 8);

    char_t *string_before_option[] = {TEXT("--string"),
                                      TEXT("--doorstop-enabled")};
    index = 0;
    CHECK(load_str_argv(string_before_option, &index, 2, TEXT("--string"),
                        &string_value),
          23);
    CHECK(index == 0 && string_value == original_string, 24);

    char_t *empty_path[] = {TEXT("--path"), TEXT("")};
    char_t *path_value = owned_string(TEXT("default-path"));
    char_t *original_path = path_value;
    index = 0;
    CHECK(load_path_argv(empty_path, &index, 2, TEXT("--path"), &path_value),
          9);
    CHECK(index == 1 && path_value == original_path, 10);

    char_t *missing_path[] = {TEXT("--path")};
    index = 0;
    CHECK(load_path_argv(missing_path, &index, 1, TEXT("--path"), &path_value),
          11);
    CHECK(index == 0 && path_value == original_path, 12);

    char_t *path_before_option[] = {TEXT("--path"),
                                    TEXT("--doorstop-enabled")};
    index = 0;
    CHECK(load_path_argv(path_before_option, &index, 2, TEXT("--path"),
                         &path_value),
          25);
    CHECK(index == 0 && path_value == original_path, 26);

    CHECK(get_full_path(NULL) == NULL, 13);
    CHECK(get_full_path(TEXT("")) == NULL, 14);
    CHECK(get_folder_name(NULL) == NULL, 15);
    CHECK(get_file_name(TEXT(""), TRUE) == NULL, 16);

    char_t *file_name = get_file_name(TEXT("game.exe"), FALSE);
    CHECK(file_name && lstrcmp(file_name, TEXT("game")) == 0, 17);

    char_t *folder_name = get_folder_name(TEXT("game.exe"));
    CHECK(folder_name && folder_name[0] == TEXT('\0'), 18);

    load_config();
    CHECK(config.mono_debug_address != NULL, 19);
    CHECK(lstrcmp(config.mono_debug_address, TEXT("127.0.0.1:10000")) == 0,
          20);

    free(config.mono_debug_address);
    config.mono_debug_address = owned_string(TEXT("LOCALHOST:23456"));
    config.mono_debug_enabled = TRUE;
    config.mono_debug_suspend = FALSE;
    mono.jit_parse_options = capture_debug_options;
    expected_debug_options =
        "--debugger-agent=transport=dt_socket,server=y,"
        "address=127.0.0.1:23456,suspend=n";
    hook_mono_jit_parse_options(0, NULL);
    CHECK(debug_options_canonicalized, 27);

    free(config.mono_debug_address);
    config.mono_debug_address = NULL;
    debug_options_canonicalized = FALSE;
    expected_debug_options =
        "--debugger-agent=transport=dt_socket,server=y,"
        "address=127.0.0.1:10000,suspend=n";
    hook_mono_jit_parse_options(0, NULL);
    CHECK(debug_options_canonicalized, 28);

    // Debug-only mode must return before touching managed-bootstrap-only Mono
    // APIs. Some stripped runtimes do not export these functions at all.
    free(config.target_assembly);
    config.target_assembly = NULL;
    mono.thread_current = capture_thread_current;
    mono.thread_set_main = capture_thread_set_main;
    mono_doorstop_bootstrap(NULL);
    CHECK(!mono_thread_api_called, 29);

    cleanup_config();

    free(file_name);
    free(folder_name);
    free(string_value);
    free(path_value);
    return 0;
}
