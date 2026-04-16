#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>

static void print_buf(const char *label, char *buf, int n) {
    printf("%s (%d bytes): ", label, n);
    fflush(stdout);

    for (int i = 0; i < n; i++) {
        putchar(buf[i]);
    }
    printf("\n");
    fflush(stdout);
}

int main(void) {
    char buf[64];


    int fd = Open("/a/b/f");
    if (fd == ERROR) {
        printf("Open(/a/b/f) -> ERROR\n");
        fflush(stdout);
        Shutdown();
        return 0;
    }

    printf("Open(/a/b/f) -> fd %d\n", fd);
    fflush(stdout);

    // --- Read first 5 bytes ---
    int n = Read(fd, buf, 5);
    printf("Read 5 -> %d\n", n);
    print_buf("chunk1", buf, n);

    // --- Read next 5 bytes ---
    n = Read(fd, buf, 5);
    printf("Read 5 -> %d\n", n);
    print_buf("chunk2", buf, n);

    // --- Try reading past EOF ---
    n = Read(fd, buf, 10);
    printf("Read past EOF -> %d\n", n);
    fflush(stdout);

    // --- Seek back to beginning ---
    int s = Seek(fd, 0, SEEK_SET);
    printf("Seek(fd, 0, SEEK_SET) -> %d\n", s);
    fflush(stdout);

    // --- Read entire file ---
    n = Read(fd, buf, sizeof(buf));
    printf("Read full -> %d\n", n);
    print_buf("full", buf, n);

    // --- Seek relative ---
    s = Seek(fd, -4, SEEK_END);
    printf("Seek(fd, -4, SEEK_END) -> %d\n", s);
    fflush(stdout);

    n = Read(fd, buf, 4);
    printf("Read last 4 -> %d\n", n);
    print_buf("tail", buf, n);

    // --- Close ---
    int c = Close(fd);
    printf("Close(fd) -> %d\n", c);
    fflush(stdout);

    Shutdown();
    return 0;
}