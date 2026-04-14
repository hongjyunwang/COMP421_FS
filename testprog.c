#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>

int main(void) {
    int a = Sync();
    printf("client: Sync returned %d\n", a);
    int b = Shutdown();
    printf("client: Shutdown returned %d\n", b);
    fflush(stdout);
    Exit(0);
}