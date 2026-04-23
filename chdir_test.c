#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <string.h>

static void test_chdir(const char *path) {
    int r = ChDir((char *)path);
    if (r == ERROR) {
        printf("ChDir(%s) -> ERROR\n", path);
    } else {
        printf("ChDir(%s) -> OK\n", path);
    }
    fflush(stdout);
}

static void test_stat(const char *path) {
    struct Stat st;
    int r = Stat((char *)path, &st);

    if (r == ERROR) {
        printf("Stat(%s) -> ERROR\n", path);
    } else {
        printf("Stat(%s) -> inum=%d\n", path, st.inum);
    }
    fflush(stdout);
}

int main(void) {

    test_chdir("/");
    test_stat(".");   // should be root

    test_chdir("/a");
    test_stat(".");   // should be /a

    test_chdir("b");
    test_stat(".");   // should be /a/b

    test_chdir("..");
    test_stat(".");   // should be /a

    test_chdir("../..");
    test_stat(".");   // should be /

    test_chdir("/a/b/f");   // NOT a directory
    test_chdir("/nosuch");  // invalid

    Shutdown();
    return 0;
}