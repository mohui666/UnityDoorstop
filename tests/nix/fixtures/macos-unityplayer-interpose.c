#include <stdio.h>
#include <unistd.h>

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
    return dup2(fd, STDOUT_FILENO);
}

__attribute__((visibility("default"))) int
unityplayer_close_stdout(void) {
    return fclose(stdout);
}
