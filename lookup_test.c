#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>
int main(void) {
    struct Stat st;

    if (Stat("/", &st) != ERROR)
        printf("/ -> inum %d\n", st.inum);

    if (Stat("/a", &st) != ERROR)
        printf("/a -> inum %d\n", st.inum);


    if (Stat("/a/b", &st) != ERROR)
        printf("/a/b -> inum %d\n", st.inum);
  
    if (Stat("/a/./b", &st) != ERROR)
        printf("/a/./b -> inum %d\n", st.inum);

    if (Stat("/a/b/..", &st) != ERROR)
        printf("/a/b/.. -> inum %d\n", st.inum);

    if (Stat("/nosuch", &st) == ERROR)
        printf("/nosuch -> ERROR\n");

    fflush(stdout);

    Shutdown();
    return 0;
}