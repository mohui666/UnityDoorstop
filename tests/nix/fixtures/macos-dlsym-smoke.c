#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int original_marker;

__attribute__((visibility("default"))) void *
mono_jit_init_version(const char *root_domain_name,
                      const char *runtime_version) {
    (void)root_domain_name;
    (void)runtime_version;
    return &original_marker;
}

typedef void *(*mono_init_fn)(const char *, const char *);

extern int unityplayer_read_first_byte(const char *path);
extern int unityplayer_redirect_stdout(int fd);
extern int unityplayer_close_stdout(void);

int main(int argc, char **argv) {
    if (argc != 2 ||
        (strcmp(argv[1], "enabled") != 0 &&
         strcmp(argv[1], "disabled") != 0)) {
        fprintf(stderr, "usage: %s enabled|disabled\n", argv[0]);
        return 2;
    }

    dlerror();
    void *resolved = dlsym(RTLD_DEFAULT, "mono_jit_init_version");
    const char *error = dlerror();
    if (!resolved || error) {
        fprintf(stderr, "target lookup failed: %s\n",
                error ? error : "unknown error");
        return 1;
    }

    if (strcmp(argv[1], "disabled") == 0) {
        mono_init_fn original = (mono_init_fn)resolved;
        if (original("smoke", "v4.0") != &original_marker) {
            fputs("disabled interposer did not return the original symbol\n",
                  stderr);
            return 1;
        }
        return 0;
    }

    dlerror();
    void *doorstop_init = dlsym(RTLD_DEFAULT, "init_mono");
    error = dlerror();
    if (!doorstop_init || error) {
        fprintf(stderr, "Doorstop init lookup failed: %s\n",
                error ? error : "unknown error");
        return 1;
    }
    if (resolved != doorstop_init) {
        fprintf(stderr, "dlsym was not redirected: got %p, expected %p\n",
                resolved, doorstop_init);
        return 1;
    }

    const char *default_boot_config = getenv("EXPECTED_BOOT_CONFIG_PATH");
    const char *redirect_path = getenv("REDIRECT_OUTPUT_PATH");
    if (!default_boot_config || !redirect_path) {
        fputs("missing interposition smoke-test paths\n", stderr);
        return 1;
    }
    if (unityplayer_read_first_byte(default_boot_config) != 'o') {
        fputs("UnityPlayer fopen was not redirected\n", stderr);
        return 1;
    }

    int redirect_fd = open(redirect_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (redirect_fd < 0 ||
        unityplayer_redirect_stdout(redirect_fd) != STDOUT_FILENO) {
        fputs("UnityPlayer dup2 interception failed\n", stderr);
        return 1;
    }
    close(redirect_fd);
    if (unityplayer_close_stdout() != 0) {
        fputs("UnityPlayer fclose interception failed\n", stderr);
        return 1;
    }
    if (write(STDOUT_FILENO, "unity-interpose-ok\n", 19) != 19) {
        fputs("UnityPlayer closed or redirected stdout\n", stderr);
        return 1;
    }

    return 0;
}
