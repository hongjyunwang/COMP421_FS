#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>

static void test_open(const char *path) {
    int fd = Open((char *)path);

    if (fd == ERROR) {
        printf("Open(%s) -> ERROR\n", path);
    } else {
        printf("Open(%s) -> fd %d\n", path, fd);
        fflush(stdout);

        int r = Close(fd);
        if (r == ERROR) {
            printf("  Close(fd=%d) -> ERROR\n", fd);
        } else {
            printf("  Close(fd=%d) -> OK\n", fd);
        }
    }
    fflush(stdout);
}

static void test_open_twice(const char *path) {
    int fd1 = Open((char *)path);
    int fd2 = Open((char *)path);

    printf("Open twice on %s -> fd1=%d fd2=%d\n", path, fd1, fd2);
    fflush(stdout);

    if (fd1 != ERROR) Close(fd1);
    if (fd2 != ERROR) Close(fd2);
}

int main(void) {
    
    test_open("/");
    test_open("/a");
    test_open("/a/b");

    test_open("/a/./b");
    test_open("/a/b/..");
    test_open("/a//b");
    test_open("/a/b/");

    test_open("/nosuch");
    test_open("");

    test_open_twice("/");
    test_open_twice("/a");
    
    fflush(stdout);

    Shutdown();
    Exit(0);
}