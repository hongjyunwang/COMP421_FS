#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stddef.h>


//32-byte message struct
struct yfs_msg {
    int type;       // request type
    int arg1;
    int arg2;
    int arg3;
    void *ptr1;
    void *ptr2;
};

// request types
enum {
    YFS_REQ_OPEN = 1,
    YFS_REQ_CLOSE,
    YFS_REQ_CREATE,
    YFS_REQ_READ,
    YFS_REQ_WRITE,
    YFS_REQ_SEEK,
    YFS_REQ_LINK,
    YFS_REQ_UNLINK,
    YFS_REQ_SYMLINK,
    YFS_REQ_READLINK,
    YFS_REQ_MKDIR,
    YFS_REQ_RMDIR,
    YFS_REQ_CHDIR,
    YFS_REQ_STAT,
    YFS_REQ_SYNC,
    YFS_REQ_SHUTDOWN
};

int Shutdown(void) {
    struct yfs_msg msg;

    msg.type = YFS_REQ_SHUTDOWN;
    msg.arg1 = 0;
    msg.arg2 = 0;
    msg.arg3 = 0;
    msg.ptr1 = NULL;
    msg.ptr2 = NULL;

    if (Send(&msg, -FILE_SERVER) == ERROR) {
        return ERROR;
    }

    return msg.arg1;
}