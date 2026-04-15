#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>

static void test_stat(const char *path) {
    struct Stat st;
    int r = Stat((char *)path, &st);

    if (r == ERROR) {
        printf("%s -> ERROR\n", path);
    } else {
        printf("%s -> inum %d\n", path, st.inum);
    }
    fflush(stdout);
}
int main(void) {
    
    test_stat("/");
    test_stat("/a");
    test_stat("/a/b");
    test_stat("/a/./b");
    test_stat("/a/b/..");
    test_stat("/nosuch");

    test_stat("/a/.");
    test_stat("/a/..");
    test_stat("/a/b/.");
    test_stat("/a/b/../..");
    test_stat("/..");
    test_stat("/a//b");
    test_stat("/a/b/");
    test_stat("");



    Shutdown();
    Exit(0);
}