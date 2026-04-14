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

    int fd = Open("/");
    int fd1 = Open("/");
    printf("client: Open returned %d\n", fd);
    printf("client: Open returned %d\n", fd1);

    int r1 = Close(fd);
    int r2 = Close(fd1);
    int r3 = Close(fd1);
    printf("client: Close returned %d\n", r1);
    printf("client: Close returned %d\n", r2);
    printf("client: Close returned %d\n", r3);





    int z = Shutdown();
    printf("client: Shutdown returned %d\n", z);
    fflush(stdout);
    Exit(0);
}