#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <comp421/filesystem.h>

int main(void) {
    int a = Sync();
    printf("client: Sync returned %d\n", a);
    
    struct Stat st;
    int b = Stat("/", &st);
    if (b == ERROR) {
        printf("client: Stat returned %d\n", b);
    } else {
        printf("inum=%d type=%d size=%d nlink=%d\n", st.inum, st.type, st.size, st.nlink);
    }

    int z = Shutdown();
    printf("client: Shutdown returned %d\n", z);
    fflush(stdout);
    Exit(0);
}