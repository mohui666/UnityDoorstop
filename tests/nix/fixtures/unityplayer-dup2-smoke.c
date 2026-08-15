#include <stdio.h>
#include <unistd.h>

extern int unityplayer_redirect_output(int destination);

int main(void) {
    int output_pipe[2];
    if (pipe(output_pipe) != 0)
        return 2;

    int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0)
        return 2;

    int result = unityplayer_redirect_output(output_pipe[1]);
    static const char visible[] = "visible";
    if (write(STDOUT_FILENO, visible, sizeof(visible) - 1) !=
        (ssize_t)(sizeof(visible) - 1))
        return 2;
    if (dup2(saved_stdout, STDOUT_FILENO) != STDOUT_FILENO)
        return 2;
    close(saved_stdout);
    close(output_pipe[1]);

    char unexpected;
    ssize_t captured = read(output_pipe[0], &unexpected, sizeof(unexpected));
    close(output_pipe[0]);

    if (result != STDOUT_FILENO || captured != 0) {
        fprintf(stderr,
                "UnityPlayer dup2 protection failed: result=%d captured=%ld\n",
                result, (long)captured);
        return 1;
    }

    puts("unityplayer-dup2-ok");
    return 0;
}
