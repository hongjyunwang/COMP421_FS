#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <string.h>

static void test_stat(const char *path) {
    struct Stat st;
    int r = Stat((char *)path, &st);

    if (r == ERROR) {
        printf("Stat(%s) -> ERROR\n", path);
    } else {
        printf("Stat(%s) -> OK inum=%d type=%d size=%d nlink=%d\n",
               path, st.inum, st.type, st.size, st.nlink);
    }
    fflush(stdout);
}

static void test_mkdir(const char *path) {
    int r = MkDir((char *)path);
    if (r == ERROR) {
        printf("MkDir(%s) -> ERROR\n", path);
    } else {
        printf("MkDir(%s) -> OK\n", path);
    }
    fflush(stdout);
}

static void test_rmdir(const char *path) {
    int r = RmDir((char *)path);
    if (r == ERROR) {
        printf("RmDir(%s) -> ERROR\n", path);
    } else {
        printf("RmDir(%s) -> OK\n", path);
    }
    fflush(stdout);
}

int main(void) {
    printf("mkdir rmdir test\n");
    fflush(stdout);

    //check
    test_stat("/");
    test_stat("/a");
    test_stat("/a/b");

    //create
    test_mkdir("/x");
    test_stat("/x");
    test_stat("/x/.");
    test_stat("/x/..");

    //duplicate should fail
    test_mkdir("/x");

    //nested create
    test_mkdir("/x/y");
    test_stat("/x/y");
    test_stat("/x/y/.");
    test_stat("/x/y/..");
    test_stat("/x/y/../..");

    //cannot remove non-empty
    test_rmdir("/x");

    //;remove child
    test_rmdir("/x/y");
    test_stat("/x/y");

    //remove parent
    test_rmdir("/x");
    test_stat("/x");

    // should return error
    test_mkdir("/x/");
    test_rmdir("/");
    test_rmdir("/nosuch");
    test_rmdir("/a/b/f");   /* regular file, should fail */

    printf("end!\n");
    fflush(stdout);

    Shutdown();
    return 0;
}