#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>

int main(void) {
    int r = Shutdown();
    printf("client: Shutdown returned %d\n", r);
    fflush(stdout);
    Exit(0);
}