#include <unistd.h>

int unityplayer_redirect_output(int destination) {
    return dup2(destination, STDOUT_FILENO);
}
