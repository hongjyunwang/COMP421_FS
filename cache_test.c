#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char buf[32];

    //multi read, should use cache!
    int fd = Open("/a/b/f");
    if (fd == ERROR) {
        printf("Open failed\n");
        Shutdown();
        return 0;
    }

    printf("First read\n");
    Read(fd, buf, 5);

    printf("Second read (should be cache hit)\n");
    Seek(fd, 0, SEEK_SET);
    Read(fd, buf, 5);

    Close(fd);

   //repeated stat
    printf("\nRepeated Stat\n");
    struct Stat st;

    Stat("/a/b/f", &st);
    Stat("/a/b/f", &st);
    Stat("/a/b/f", &st);

    //mark dirty write
    printf("\nCreating directory /cachetest\n");
    MkDir("/cachetest");

    //flush all
    printf("\nCalling Sync()\n");
    Sync();

    //flush all
    printf("\nCalling Shutdown()\n");
    fflush(stdout);
    Shutdown();

    return 0;
}