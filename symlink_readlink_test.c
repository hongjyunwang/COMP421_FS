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


//helpers
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

//symlink tests
static void test_symlink_basic(void) {
    make_file("/sym_target.txt", "hello");

    int r = SymLink("/sym_target.txt", "/sym_link.txt");
    EXPECT(r == 0, "symlink creation returns 0");
}

static void test_symlink_stat_type(void) {
    make_file("/sym_stat_target.txt", "data");
    SymLink("/sym_stat_target.txt", "/sym_stat_link.txt");

    // Stat follows symlinks, so we use ReadLink's existence to confirm type
    // Instead open the symlink via readlink to verify it's a symlink inode
    char buf[256];
    int n = ReadLink("/sym_stat_link.txt", buf, sizeof(buf));
    EXPECT(n > 0, "ReadLink on symlink succeeds confirming it is a symlink inode");
}

static void test_symlink_open_follows(void) {
    make_file("/sym_open_target.txt", "through symlink");
    SymLink("/sym_open_target.txt", "/sym_open_link.txt");

    char buf[64];
    int n = read_file("/sym_open_link.txt", buf, sizeof(buf));
    EXPECT(n > 0 && strcmp(buf, "through symlink") == 0,
           "open through symlink reads target content");
}

static void test_symlink_to_directory(void) {
    MkDir("/sym_dir_target");
    make_file("/sym_dir_target/inside.txt", "inside");

    int r = SymLink("/sym_dir_target", "/sym_dir_link");
    EXPECT(r == 0, "symlink to directory returns 0");

    // Open a file through the symlink path
    char buf[64];
    int n = read_file("/sym_dir_link/inside.txt", buf, sizeof(buf));
    EXPECT(n > 0 && strcmp(buf, "inside") == 0,
           "file inside directory reachable through symlink");
}

static void test_symlink_dangling_create(void) {
    int r = SymLink("/nonexistent_target.txt", "/dangling_link.txt");
    EXPECT(r == 0, "creating dangling symlink returns 0");
}


static void test_symlink_dangling_traverse(void) {
    SymLink("/truly_nonexistent.txt", "/dangling_traverse_link.txt");

    int fd = Open("/dangling_traverse_link.txt");
    EXPECT(fd == ERROR, "opening dangling symlink returns ERROR");
}

static void test_symlink_relative_target(void) {
    MkDir("/reldir");
    make_file("/reldir/relfile.txt", "relative");

    // Symlink with a relative target — resolved from the directory
    // containing the symlink, which is "/"
    SymLink("reldir/relfile.txt", "/rel_link.txt");

    char buf[64];
    int n = read_file("/rel_link.txt", buf, sizeof(buf));
    EXPECT(n > 0 && strcmp(buf, "relative") == 0,
           "relative symlink target resolves correctly");
}

static void test_symlink_chained(void) {
    make_file("/chain_final.txt", "end of chain");
    SymLink("/chain_final.txt", "/chain_link1.txt");
    SymLink("/chain_link1.txt", "/chain_link2.txt");

    char buf[64];
    int n = read_file("/chain_link2.txt", buf, sizeof(buf));
    EXPECT(n > 0 && strcmp(buf, "end of chain") == 0,
           "chained symlinks resolve to final target");
}

static void test_symlink_different_inode(void) {
    make_file("/sym_inum_target.txt", "x");
    SymLink("/sym_inum_target.txt", "/sym_inum_link.txt");

    struct Stat st_target, st_link;
    Stat("/sym_inum_target.txt", &st_target);

    // Stat follows the symlink so we verify via ReadLink that the
    // symlink inode exists separately
    char buf[256];
    int n = ReadLink("/sym_inum_link.txt", buf, sizeof(buf));
    EXPECT(n > 0, "symlink has its own inode separate from target");
    (void)st_target; (void)st_link;
}

static void test_symlink_error_newname_exists(void) {
    make_file("/sym_exists_target.txt", "a");
    make_file("/sym_exists_new.txt", "b");

    int r = SymLink("/sym_exists_target.txt", "/sym_exists_new.txt");
    EXPECT(r == ERROR, "symlink where newname exists returns ERROR");
}

static void test_symlink_error_empty_target(void) {
    int r = SymLink("", "/sym_empty_target_link.txt");
    EXPECT(r == ERROR, "symlink to empty string returns ERROR");
}


static void test_symlink_error_empty_newname(void) {
    int r = SymLink("/some_target.txt", "");
    EXPECT(r == ERROR, "symlink with empty newname returns ERROR");
}

static void test_symlink_error_bad_parent(void) {
    int r = SymLink("/target.txt", "/nonexistent_dir/link.txt");
    EXPECT(r == ERROR, "symlink into nonexistent parent returns ERROR");
}

//readlink
static void test_readlink_basic(void) {
    make_file("/rl_target.txt", "data");
    SymLink("/rl_target.txt", "/rl_link.txt");

    char buf[256];
    int n = ReadLink("/rl_link.txt", buf, sizeof(buf));
    buf[n] = '\0';

    EXPECT(n == (int)strlen("/rl_target.txt"), "readlink returns correct length");
    EXPECT(strcmp(buf, "/rl_target.txt") == 0, "readlink returns correct target string");
}

static void test_readlink_length(void) {
    SymLink("/length_target.txt", "/length_link.txt");

    char buf[256];
    memset(buf, 0xFF, sizeof(buf));  // fill with non-zero sentinel
    int n = ReadLink("/length_link.txt", buf, sizeof(buf));

    EXPECT(n == (int)strlen("/length_target.txt"),
           "readlink returns length of target, not including null");
}

static void test_readlink_truncation(void) {
    SymLink("/truncation_target.txt", "/truncation_link.txt");

    char buf[5];
    int n = ReadLink("/truncation_link.txt", buf, 4);

    // Should return 4 (the cap), not the full length
    EXPECT(n == 4, "readlink truncates to len when target is longer");
}

static void test_readlink_dangling(void) {
    SymLink("/dangling_rl_target.txt", "/dangling_rl_link.txt");

    char buf[256];
    int n = ReadLink("/dangling_rl_link.txt", buf, sizeof(buf));
    buf[n] = '\0';

    EXPECT(n > 0 && strcmp(buf, "/dangling_rl_target.txt") == 0,
           "readlink on dangling symlink returns target string");
}

static void test_readlink_does_not_follow(void) {
    make_file("/rl_follow_target.txt", "content");
    SymLink("/rl_follow_target.txt", "/rl_follow_link.txt");

    char buf[256];
    int n = ReadLink("/rl_follow_link.txt", buf, sizeof(buf));
    buf[n] = '\0';

    // Should return the target string, not the file content
    EXPECT(strcmp(buf, "/rl_follow_target.txt") == 0,
           "readlink returns target path, not file content");
}


static void test_readlink_relative(void) {
    SymLink("relative/path.txt", "/rl_relative_link.txt");

    char buf[256];
    int n = ReadLink("/rl_relative_link.txt", buf, sizeof(buf));
    buf[n] = '\0';

    EXPECT(strcmp(buf, "relative/path.txt") == 0,
           "readlink returns relative target string as-is");
}

static void test_readlink_error_regular_file(void) {
    make_file("/rl_regular.txt", "not a symlink");

    char buf[256];
    int n = ReadLink("/rl_regular.txt", buf, sizeof(buf));
    EXPECT(n == ERROR, "readlink on regular file returns ERROR");
}

static void test_readlink_error_directory(void) {
    MkDir("/rl_dir_test");

    char buf[256];
    int n = ReadLink("/rl_dir_test", buf, sizeof(buf));
    EXPECT(n == ERROR, "readlink on directory returns ERROR");
}

static void test_readlink_error_nonexistent(void) {
    char buf[256];
    int n = ReadLink("/rl_nonexistent_link.txt", buf, sizeof(buf));
    EXPECT(n == ERROR, "readlink on nonexistent path returns ERROR");
}

static void test_readlink_error_bad_len(void) {
    SymLink("/rl_badlen_target.txt", "/rl_badlen_link.txt");

    char buf[256];
    int n = ReadLink("/rl_badlen_link.txt", buf, 0);
    EXPECT(n == ERROR, "readlink with len=0 returns ERROR");
}


int main(void) {
    printf("symlink tests\n");

    test_symlink_basic();
    test_symlink_stat_type();
    test_symlink_open_follows();
    test_symlink_to_directory();
    test_symlink_dangling_create();
    test_symlink_dangling_traverse();
    test_symlink_relative_target();
    test_symlink_chained();
    test_symlink_different_inode();
    test_symlink_error_newname_exists();
    test_symlink_error_empty_target();
    test_symlink_error_empty_newname();
    test_symlink_error_bad_parent();

    printf("readlink\n");

    test_readlink_basic();
    test_readlink_length();
    test_readlink_truncation();
    test_readlink_dangling();
    test_readlink_does_not_follow();
    test_readlink_relative();
    test_readlink_error_regular_file();
    test_readlink_error_directory();
    test_readlink_error_nonexistent();
    test_readlink_error_bad_len();

    printf("Results: %d passed, %d failed ===\n", passed, failed);
    Shutdown();
    return 0;
}