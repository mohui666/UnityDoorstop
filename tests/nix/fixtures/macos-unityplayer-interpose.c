#include <stdio.h>
#include <unistd.h>

/* Keep work after the libc calls so release builds cannot turn these wrappers
   into tail calls. The interposition guard intentionally identifies the
   UnityPlayer caller from its return address. */
static volatile int interpose_result_sink;

__attribute__((visibility("default"))) int
unityplayer_read_first_byte(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file)
        return -1;
    int value = fgetc(file);
    fclose(file);
    return value;
}

__attribute__((visibility("default"))) int
unityplayer_redirect_stdout(int fd) {
    int result = dup2(fd, STDOUT_FILENO);
    interpose_result_sink = result;
    return result;
}

__attribute__((visibility("default"))) int
unityplayer_close_stdout(void) {
    int result = fclose(stdout);
    interpose_result_sink = result;
    return result;
}
