#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <string.h>

static void test_create(const char *path) {
    int fd = Create((char *)path);
    if (fd == ERROR)
        printf("Create(%s) -> ERROR\n", path);
    else {
        printf("Create(%s) -> fd %d\n", path, fd);
        Close(fd);
    }
    fflush(stdout);
}

static void test_create_then_stat(const char *path) {
    int fd = Create((char *)path);
    if (fd == ERROR) {
        printf("Create(%s) -> ERROR\n", path);
        fflush(stdout);
        return;
    }
    printf("Create(%s) -> fd %d\n", path, fd);
    Close(fd);

    struct Stat st;
    if (Stat((char *)path, &st) == ERROR)
        printf("  Stat(%s) -> ERROR\n", path);
    else
        printf("  Stat(%s) -> inum=%d type=%d size=%d nlink=%d\n",
               path, st.inum, st.type, st.size, st.nlink);
    fflush(stdout);
}

static void test_create_twice(const char *path) {
    int fd1 = Create((char *)path);
    printf("Create(%s) first  -> fd=%d\n", path, fd1);
    if (fd1 != ERROR) Close(fd1);

    int fd2 = Create((char *)path);
    printf("Create(%s) second -> fd=%d (expect valid fd, not ERROR)\n", path, fd2);
    if (fd2 != ERROR) Close(fd2);
    fflush(stdout);
}

int main(void) {

    printf("basic\n");
    test_create_then_stat("/newfile");
    test_create_then_stat("/newfile2");

    printf("varying paths\n");
    test_create("/./newfile3");
    test_create("//newfile4");
    test_create("/newfile5/"); // ERROR — trailing slash
    test_create("/./bar"); // succeeds — /. is root

    printf("exptect errors\n");
    test_create("/nosuchdir/file");
    test_create("");
    test_create("/");

    printf("double create\n");
    test_create("/foo");
    test_create("/foo/bar");          // ERROR — /foo is a file
    test_create("/foo/");             // ERROR — trailing slash + file
    test_create("/./foo/baz");        // ERROR — /foo is a file

    printf("fd, pos0\n");
    {
        int fd = Create((char *)"/posfile");
        if (fd != ERROR) {
            int pos = Seek(fd, 0, SEEK_CUR);
            printf("Seek(fd, 0, SEEK_CUR) after Create -> %d (expect 0)\n", pos);
            Close(fd);
        }
        fflush(stdout);
    }

    printf("double create should truncate \n");
    test_create_twice("/truncme");

    printf("nlink should stay 1\n");
    {
        int fd = Create((char *)"/nlinkfile");
        if (fd != ERROR) Close(fd);
        fd = Create((char *)"/nlinkfile");
        if (fd != ERROR) Close(fd);
        struct Stat st;
        if (Stat("/nlinkfile", &st) != ERROR)
            printf("  nlink=%d (expect 1)\n", st.nlink);
        fflush(stdout);
    }

    printf("stat after re create size = 0\n");
    {
        int fd = Create((char *)"/sizefile");
        if (fd != ERROR) Close(fd);
        int fd2 = Create((char *)"/sizefile");
        if (fd2 != ERROR) Close(fd2);
        struct Stat st;
        if (Stat("/sizefile", &st) != ERROR)
            printf("  size=%d (expect 0)\n", st.size);
        fflush(stdout);
    }

    printf("open after create\n");
    {
        int fd = Create((char *)"/openme");
        if (fd != ERROR) Close(fd);
        int fd2 = Open("/openme");
        if (fd2 == ERROR)
            printf("Open(/openme) -> ERROR\n");
        else {
            printf("Open(/openme) -> fd %d\n", fd2);
            Close(fd2);
        }
        fflush(stdout);
    }

    printf("fill inodes\n");
    {
        // keep creating files until we run out of inodes
        // mkyfs gives 47 inodes; several are already used above
        int count = 0;
        char name[32];
        while (1) {
            // use a name that won't collide with earlier tests
            name[0] = '/';
            name[1] = 'z';
            // encode count into name digits
            name[2] = '0' + (count / 100) % 10;
            name[3] = '0' + (count / 10)  % 10;
            name[4] = '0' + (count)        % 10;
            name[5] = '\0';
            int fd = Create(name);
            if (fd == ERROR) break;
            Close(fd);
            count++;
        }
        printf("Created %d files before inode exhaustion\n", count);
        fflush(stdout);
    }

    Shutdown();
    Exit(0);
}