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
// Test: basic unlink removes the file
// ================================================================
static void test_basic_unlink(void) {
    make_file("/unlink_basic.txt", "hello");

    int r = Unlink("/unlink_basic.txt");
    EXPECT(r == 0, "basic unlink returns 0");

    // File should no longer be openable
    int fd = Open("/unlink_basic.txt");
    EXPECT(fd == ERROR, "file is gone after unlink");
}

// ================================================================
// Test: unlink frees the inode — name can be reused
// ================================================================
static void test_inode_freed(void) {
    make_file("/inode_free.txt", "data");

    struct Stat st1;
    Stat("/inode_free.txt", &st1);

    Unlink("/inode_free.txt");

    // Recreate a file — the inode number may be reused
    make_file("/inode_free.txt", "new");

    struct Stat st2;
    Stat("/inode_free.txt", &st2);

    // The new file is valid regardless of whether the inode was reused
    int fd = Open("/inode_free.txt");
    EXPECT(fd != ERROR, "name can be recreated after unlink");
    Close(fd);
    (void)st1; (void)st2;
}

// ================================================================
// Test: unlink with multiple hard links only removes one name
// ================================================================
static void test_unlink_one_of_many(void) {
    make_file("/multi_unlink_orig.txt", "shared");
    Link("/multi_unlink_orig.txt", "/multi_unlink_link.txt");

    int r = Unlink("/multi_unlink_orig.txt");
    EXPECT(r == 0, "unlink one of two links returns 0");

    // The other name must still be accessible
    char buf[64];
    int n = read_file("/multi_unlink_link.txt", buf, sizeof(buf));
    EXPECT(n > 0 && strcmp(buf, "shared") == 0, "remaining link still readable");
}

// ================================================================
// Test: nlink decrements on unlink
// ================================================================
static void test_nlink_decrements(void) {
    make_file("/nlink_dec_orig.txt", "x");
    Link("/nlink_dec_orig.txt", "/nlink_dec_link.txt");

    struct Stat st;
    Stat("/nlink_dec_orig.txt", &st);
    EXPECT(st.nlink == 2, "nlink is 2 before unlink");

    Unlink("/nlink_dec_orig.txt");

    Stat("/nlink_dec_link.txt", &st);
    EXPECT(st.nlink == 1, "nlink is 1 after unlinking one name");
}

// ================================================================
// Test: unlink in a subdirectory
// ================================================================
static void test_unlink_in_subdir(void) {
    MkDir("/unlink_subdir");
    make_file("/unlink_subdir/file.txt", "subdir file");

    int r = Unlink("/unlink_subdir/file.txt");
    EXPECT(r == 0, "unlink in subdirectory returns 0");

    int fd = Open("/unlink_subdir/file.txt");
    EXPECT(fd == ERROR, "file in subdirectory is gone after unlink");
}

// ================================================================
// Test: unlink last link — inode and blocks are freed
// ================================================================
static void test_unlink_last_link(void) {
    make_file("/last_link.txt", "disappear");
    Link("/last_link.txt", "/last_link_b.txt");

    Unlink("/last_link.txt");
    Unlink("/last_link_b.txt");

    int fd1 = Open("/last_link.txt");
    int fd2 = Open("/last_link_b.txt");
    EXPECT(fd1 == ERROR && fd2 == ERROR, "both names gone after unlinking last link");
}

// ================================================================
// Test: data is intact up until the last unlink
// ================================================================
static void test_data_intact_until_last_unlink(void) {
    make_file("/data_intact.txt", "important");
    Link("/data_intact.txt", "/data_intact_b.txt");

    Unlink("/data_intact.txt");

    char buf[64];
    int n = read_file("/data_intact_b.txt", buf, sizeof(buf));
    EXPECT(n > 0 && strcmp(buf, "important") == 0,
           "data intact through remaining link after first unlink");

    Unlink("/data_intact_b.txt");
}

// ================================================================
// Error: unlink nonexistent file
// ================================================================
static void test_error_nonexistent(void) {
    int r = Unlink("/does_not_exist_unlink.txt");
    EXPECT(r == ERROR, "unlink nonexistent file returns ERROR");
}

// ================================================================
// Error: unlink a directory
// ================================================================
static void test_error_unlink_directory(void) {
    MkDir("/unlink_dir_test");
    int r = Unlink("/unlink_dir_test");
    EXPECT(r == ERROR, "unlink on directory returns ERROR");

    // Directory should still be there
    int fd = Open("/unlink_dir_test");
    EXPECT(fd != ERROR, "directory still exists after failed unlink");
    if (fd != ERROR) Close(fd);
}

// ================================================================
// Error: unlink with empty path
// ================================================================
static void test_error_empty_path(void) {
    int r = Unlink("");
    EXPECT(r == ERROR, "unlink with empty path returns ERROR");
}

// ================================================================
// Error: unlink "." and ".."
// ================================================================
static void test_error_dot_entries(void) {
    MkDir("/dot_test_dir");
    ChDir("/dot_test_dir");

    int r1 = Unlink(".");
    EXPECT(r1 == ERROR, "unlink '.' returns ERROR");

    int r2 = Unlink("..");
    EXPECT(r2 == ERROR, "unlink '..' returns ERROR");

    ChDir("/");
}

// ================================================================
// Error: parent directory does not exist
// ================================================================
static void test_error_bad_parent(void) {
    int r = Unlink("/nonexistent_dir/file.txt");
    EXPECT(r == ERROR, "unlink with nonexistent parent returns ERROR");
}

// ================================================================
// Main
// ================================================================
int main(void) {
    printf("=== Unlink tests ===\n");

    test_basic_unlink();
    test_inode_freed();
    test_unlink_one_of_many();
    test_nlink_decrements();
    test_unlink_in_subdir();
    test_unlink_last_link();
    test_data_intact_until_last_unlink();
    test_error_nonexistent();
    test_error_unlink_directory();
    test_error_empty_path();
    test_error_dot_entries();
    test_error_bad_parent();

    printf("=== Results: %d passed, %d failed ===\n", passed, failed);
    Shutdown();
    return 0;
}