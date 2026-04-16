#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>

int main(void) {

    int fd = Open("/a/b");
    printf("fd = %d\n", fd);
    fflush(stdout);
    
    printf("Seek(fd, 0, SEEK_SET) = %d\n", Seek(fd, 0, SEEK_SET));
    fflush(stdout);

    printf("Seek(fd, 5, SEEK_SET) = %d\n", Seek(fd, 5, SEEK_SET));
    fflush(stdout);

    printf("Seek(fd, -2, SEEK_CUR) = %d\n", Seek(fd, -2, SEEK_CUR));
    fflush(stdout);

    printf("Seek(fd, 0, SEEK_END) = %d\n", Seek(fd, 0, SEEK_SET));
    fflush(stdout);

    printf("Seek(fd, -1, SEEK_END) = %d\n", Seek(fd, -1, SEEK_SET));
    fflush(stdout);

    printf("Seek(fd, 3, SEEK_END) = %d\n", Seek(fd, 3, SEEK_SET));
    fflush(stdout);

    Close(fd);
    Shutdown();
    Exit(0);
}