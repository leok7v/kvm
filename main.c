#include "rt/ustd.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

int kvm_tests(void);
int map_tests(void);

static void on_signal(int signum) {
    fprintf(stderr, "signal: %d\n", signum);
    exit(EXIT_FAILURE);
}

static void set_on_signal(void) {
    if (signal(SIGABRT, on_signal) == SIG_ERR) {
        perror("signal(SIGABRT, on_signal) failed\n");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, const char* argv[]) {
    (void)argc; (void)argv; // unused
    set_on_signal();
    return kvm_tests() || map_tests();
}


