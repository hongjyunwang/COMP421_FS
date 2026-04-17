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
    printf("==== MkDir / RmDir tests start ====\n");
    fflush(stdout);

    /* Initial sanity */
    test_stat("/");
    test_stat("/a");
    test_stat("/a/b");

    /* Create /x */
    test_mkdir("/x");
    test_stat("/x");
    test_stat("/x/.");
    test_stat("/x/..");

    /* Duplicate should fail */
    test_mkdir("/x");

    /* Create nested /x/y */
    test_mkdir("/x/y");
    test_stat("/x/y");
    test_stat("/x/y/.");
    test_stat("/x/y/..");
    test_stat("/x/y/../..");

    /* Removing non-empty dir should fail */
    test_rmdir("/x");

    /* Remove child first */
    test_rmdir("/x/y");
    test_stat("/x/y");

    /* Now remove parent */
    test_rmdir("/x");
    test_stat("/x");

    /* Error cases */
    test_rmdir("/");
    test_rmdir("/nosuch");
    test_rmdir("/a/b/f");   /* regular file, should fail */

    printf("==== MkDir / RmDir tests end ====\n");
    fflush(stdout);

    Shutdown();
    return 0;
}