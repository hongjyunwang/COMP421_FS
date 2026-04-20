#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int passed = 0;
static int failed = 0;

#define EXPECT(cond, msg) do { \
    if (cond) { \
        printf("PASS: %s\n", msg); \
        passed++; \
    } else { \
        printf("FAIL: %s\n", msg); \
        failed++; \
    } \
} while(0)

// ================================================================
// Helpers
// ================================================================

// Create a file and write some content to it. Returns fd or ERROR.
static int make_file(char *path, char *content) {
    int fd = Create(path);
    if (fd == ERROR) return ERROR;
    if (content && strlen(content) > 0) {
        Write(fd, (void *)content, strlen(content));
    }
    Close(fd);
    return 0;
}

static int read_file(char *path, char *buf, int bufsz) {
    int fd = Open(path);
    if (fd == ERROR) return ERROR;
    int n = Read(fd, buf, bufsz - 1);
    Close(fd);
    if (n < 0) return ERROR;
    buf[n] = '\0';
    return n;
}

// ================================================================
// Test: basic link creates a second name for the same file
// ================================================================
static void test_basic_link(void) {
    make_file("/basic_orig.txt", "hello");

    int r = Link("/basic_orig.txt", "/basic_link.txt");
    EXPECT(r == 0, "basic link returns 0");

    // Both names should be readable and have identical content
    char buf1[64], buf2[64];
    read_file("/basic_orig.txt", buf1, sizeof(buf1));
    read_file("/basic_link.txt", buf2, sizeof(buf2));
    EXPECT(strcmp(buf1, buf2) == 0, "link and original have same content");
}

// ================================================================
// Test: nlink count reflects both names
// ================================================================
static void test_nlink_increments(void) {
    make_file("/nlink_orig.txt", "data");

    struct Stat st;
    Stat("/nlink_orig.txt", &st);
    int before = st.nlink;

    Link("/nlink_orig.txt", "/nlink_link.txt");

    Stat("/nlink_orig.txt", &st);
    EXPECT(st.nlink == before + 1, "nlink increments after link");

    // Both names report same nlink
    struct Stat st2;
    Stat("/nlink_link.txt", &st2);
    EXPECT(st.nlink == st2.nlink, "both names report same nlink");
}

// ================================================================
// Test: both names point to the same inode
// ================================================================
static void test_same_inode(void) {
    make_file("/inum_orig.txt", "x");
    Link("/inum_orig.txt", "/inum_link.txt");

    struct Stat s1, s2;
    Stat("/inum_orig.txt", &s1);
    Stat("/inum_link.txt", &s2);
    EXPECT(s1.inum == s2.inum, "both names have same inode number");
}

// ================================================================
// Test: write through one name, read through the other
// ================================================================
static void test_shared_data(void) {
    make_file("/shared_orig.txt", "initial");
    Link("/shared_orig.txt", "/shared_link.txt");

    // Overwrite through the link name
    int fd = Create("/shared_link.txt");
    Write(fd, "modified", 8);
    Close(fd);

    char buf[64];
    read_file("/shared_orig.txt", buf, sizeof(buf));
    EXPECT(strcmp(buf, "modified") == 0, "write via link visible through original");
}

// ================================================================
// Test: unlinking original leaves link intact
// ================================================================
// static void test_unlink_original(void) {
//     make_file("/unlink_orig.txt", "survive");
//     Link("/unlink_orig.txt", "/unlink_link.txt");

//     Unlink("/unlink_orig.txt");

//     // Link should still be openable and readable
//     char buf[64];
//     int n = read_file("/unlink_link.txt", buf, sizeof(buf));
//     EXPECT(n > 0 && strcmp(buf, "survive") == 0, "link survives after original unlinked");

//     // nlink should now be 1
//     struct Stat st;
//     Stat("/unlink_link.txt", &st);
//     EXPECT(st.nlink == 1, "nlink is 1 after original unlinked");
// }

// ================================================================
// Test: link into a different directory
// ================================================================
static void test_cross_directory_link(void) {
    MkDir("/linkdir");
    make_file("/crossdir_orig.txt", "crossdir");

    int r = Link("/crossdir_orig.txt", "/linkdir/crossdir_link.txt");
    EXPECT(r == 0, "cross-directory link returns 0");

    char buf[64];
    int n = read_file("/linkdir/crossdir_link.txt", buf, sizeof(buf));
    EXPECT(n > 0 && strcmp(buf, "crossdir") == 0, "cross-directory link is readable");
}

// ================================================================
// Test: multiple links to the same file
// ================================================================
static void test_multiple_links(void) {
    make_file("/multi_orig.txt", "multi");
    Link("/multi_orig.txt", "/multi_a.txt");
    Link("/multi_orig.txt", "/multi_b.txt");
    Link("/multi_orig.txt", "/multi_c.txt");

    struct Stat st;
    Stat("/multi_orig.txt", &st);
    EXPECT(st.nlink == 4, "nlink is 4 after three links");

    // All names readable
    char buf[64];
    int ok = 1;
    read_file("/multi_a.txt", buf, sizeof(buf)); if (strcmp(buf, "multi") != 0) ok = 0;
    read_file("/multi_b.txt", buf, sizeof(buf)); if (strcmp(buf, "multi") != 0) ok = 0;
    read_file("/multi_c.txt", buf, sizeof(buf)); if (strcmp(buf, "multi") != 0) ok = 0;
    EXPECT(ok, "all three link names readable with correct content");
}

// ================================================================
// Error: link to nonexistent source
// ================================================================
static void test_error_nonexistent_source(void) {
    int r = Link("/does_not_exist.txt", "/error_link.txt");
    EXPECT(r == ERROR, "link from nonexistent source returns ERROR");
}

// ================================================================
// Error: newname already exists
// ================================================================
static void test_error_newname_exists(void) {
    make_file("/exists_orig.txt", "a");
    make_file("/exists_other.txt", "b");

    int r = Link("/exists_orig.txt", "/exists_other.txt");
    EXPECT(r == ERROR, "link where newname exists returns ERROR");
}

// ================================================================
// Error: linking a directory is forbidden
// ================================================================
static void test_error_link_directory(void) {
    MkDir("/linkforbiddendir");
    int r = Link("/linkforbiddendir", "/dir_link");
    EXPECT(r == ERROR, "linking a directory returns ERROR");
}

// ================================================================
// Error: parent directory of newname does not exist
// ================================================================
static void test_error_bad_parent(void) {
    make_file("/badparent_orig.txt", "x");
    int r = Link("/badparent_orig.txt", "/nonexistent_dir/link.txt");
    EXPECT(r == ERROR, "link into nonexistent parent directory returns ERROR");
}

// ================================================================
// Error: link to empty string
// ================================================================
static void test_error_empty_path(void) {
    make_file("/empty_path_orig.txt", "x");
    int r = Link("/empty_path_orig.txt", "");
    EXPECT(r == ERROR, "link with empty newname returns ERROR");

    r = Link("", "/empty_path_link.txt");
    EXPECT(r == ERROR, "link with empty oldname returns ERROR");
}

// ================================================================
// Main
// ================================================================
int main(void) {
    printf("=== Link tests ===\n");

    test_basic_link();
    test_nlink_increments();
    test_same_inode();
    test_shared_data();
    // test_unlink_original();   <-- remove this line
    test_cross_directory_link();
    test_multiple_links();
    test_error_nonexistent_source();
    test_error_newname_exists();
    test_error_link_directory();
    test_error_bad_parent();
    test_error_empty_path();

    printf("=== Results: %d passed, %d failed ===\n", passed, failed);
    Shutdown();
    return 0;
}