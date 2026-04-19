#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <string.h>

static int make_file(const char *path) {
    int fd = Create((char *)path);
    if (fd == ERROR) {
        printf("Create(%s) -> ERROR\n", path);
        fflush(stdout);
    }
    return fd;
}

int main(void) {

    printf("=== basic write and stat ===\n");
    {
        int fd = make_file("/hello");
        int w = Write(fd, "hello", 5);
        printf("Write(\"hello\") -> %d (expect 5)\n", w);
        Close(fd);
        struct Stat st;
        Stat("/hello", &st);
        printf("Stat size=%d (expect 5)\n", st.size);
        fflush(stdout);
    }

    printf("\n=== write zero bytes ===\n");
    {
        int fd = make_file("/zero");
        int w = Write(fd, "abc", 0);
        printf("Write(size=0) -> %d (expect 0)\n", w);
        Close(fd);
        fflush(stdout);
    }

    printf("\n=== multiple writes advance offset ===\n");
    {
        int fd = make_file("/multi");
        int w1 = Write(fd, "foo", 3);
        int w2 = Write(fd, "bar", 3);
        printf("Write(\"foo\") -> %d (expect 3)\n", w1);
        printf("Write(\"bar\") -> %d (expect 3)\n", w2);
        Close(fd);
        struct Stat st;
        Stat("/multi", &st);
        printf("Stat size=%d (expect 6)\n", st.size);
        fflush(stdout);
    }

    printf("\n=== write after seek ===\n");
    {
        int fd = make_file("/seekwrite");
        Write(fd, "aaaaa", 5);
        Seek(fd, 2, SEEK_SET);
        int w = Write(fd, "BB", 2);
        printf("Write(\"BB\") at offset 2 -> %d (expect 2)\n", w);
        Close(fd);
        struct Stat st;
        Stat("/seekwrite", &st);
        printf("Stat size=%d (expect 5)\n", st.size);
        fflush(stdout);
    }

    printf("\n=== write past end extends file ===\n");
    {
        int fd = make_file("/extend");
        Write(fd, "abc", 3);
        Seek(fd, 10, SEEK_SET);
        int w = Write(fd, "X", 1);
        printf("Write(\"X\") at offset 10 -> %d (expect 1)\n", w);
        Close(fd);
        struct Stat st;
        Stat("/extend", &st);
        printf("Stat size=%d (expect 11)\n", st.size);
        fflush(stdout);
    }

    printf("\n=== write exactly BLOCKSIZE bytes ===\n");
    {
        char buf[512];
        memset(buf, 'B', 512);
        int fd = make_file("/exact");
        int w = Write(fd, buf, 512);
        printf("Write(512) -> %d (expect 512)\n", w);
        Close(fd);
        struct Stat st;
        Stat("/exact", &st);
        printf("Stat size=%d (expect 512)\n", st.size);
        fflush(stdout);
    }

    printf("\n=== write across block boundary ===\n");
    {
        char buf[600];
        memset(buf, 'A', 600);
        int fd = make_file("/cross");
        int w = Write(fd, buf, 600);
        printf("Write(600) -> %d (expect 600)\n", w);
        Close(fd);
        struct Stat st;
        Stat("/cross", &st);
        printf("Stat size=%d (expect 600)\n", st.size);
        fflush(stdout);
    }

    printf("\n=== write requiring indirect block ===\n");
    {
        // NUM_DIRECT is 11, so 11*512 = 5632 bytes fits in direct blocks.
        // Writing 6000 bytes forces allocation of the indirect block.
        char buf[6000];
        memset(buf, 'C', 6000);
        int fd = make_file("/indirect");
        int w = Write(fd, buf, 6000);
        printf("Write(6000) -> %d (expect 6000)\n", w);
        Close(fd);
        struct Stat st;
        Stat("/indirect", &st);
        printf("Stat size=%d (expect 6000)\n", st.size);
        fflush(stdout);
    }

    printf("\n=== write to directory is error ===\n");
    {
        int fd = Open("/");
        int w = Write(fd, "x", 1);
        printf("Write to directory -> %d (expect ERROR=%d)\n", w, ERROR);
        Close(fd);
        fflush(stdout);
    }

    printf("\n=== write to closed fd is error ===\n");
    {
        int fd = make_file("/closedfd");
        Close(fd);
        int w = Write(fd, "x", 1);
        printf("Write to closed fd -> %d (expect ERROR=%d)\n", w, ERROR);
        fflush(stdout);
    }

    printf("\n=== write to invalid fd is error ===\n");
    {
        int w = Write(-1, "x", 1);
        printf("Write(fd=-1) -> %d (expect ERROR=%d)\n", w, ERROR);
        fflush(stdout);
    }

    printf("\n=== create truncates then write ===\n");
    {
        int fd = make_file("/trunc");
        Write(fd, "hello world", 11);
        Close(fd);

        fd = Create("/trunc");
        int w = Write(fd, "hi", 2);
        printf("Write after truncate -> %d (expect 2)\n", w);
        Close(fd);
        struct Stat st;
        Stat("/trunc", &st);
        printf("Stat size=%d (expect 2)\n", st.size);
        fflush(stdout);
    }

    printf("\n=== seek end after write ===\n");
    {
        int fd = make_file("/seekend");
        Write(fd, "hello", 5);
        int pos = Seek(fd, 0, SEEK_END);
        printf("Seek(SEEK_END) after writing 5 bytes -> %d (expect 5)\n", pos);
        Close(fd);
        fflush(stdout);
    }

    Shutdown();
    Exit(0);
}