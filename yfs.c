#include <comp421/filesystem.h>
#include <comp421/yalnix.h>
#include <stddef.h>
#include <stdlib.h>

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


// ===== Handler prototypes =====
static void handle_shutdown(int pid, struct yfs_msg *msg);
//TODO: add other handlers


int main(int argc, char **argv) {

    //register service id
    if (Register(FILE_SERVER) == ERROR) {
        TracePrintf(0, "yfs: Register failed\n");
        Exit(ERROR);
    }

    TracePrintf(1, "yfs: Registered as FILE_SERVER\n");


    //make first client process
    //argv[0] is just YFS
    //use shell command like: yalnix yfs testprog testarg1....
    if (argc > 1) {
        int pid = Fork();
        if (pid == 0) {
            Exec(argv[1], argv + 1);
            TracePrintf(0, "yfs: Exec failed\n");
            Exit(ERROR);
        }
    }


    //server loop
    while (1) {
        struct yfs_msg msg;

        int sender = Receive(&msg);

        if (sender == ERROR) {
            TracePrintf(0, "yfs: Receive error\n");
            continue;
        }

        if (sender == 0) {
            // Deadlock-break case (all processes blocked)?
            continue;
        }

        //Dispatcher
        switch (msg.type) {
            // TODO: add other cases

            case YFS_REQ_SHUTDOWN:
                handle_shutdown(sender, &msg);
                break;

            default:
                TracePrintf(0, "yfs: Unknown request %d\n", msg.type);
                msg.arg1 = ERROR;
                Reply(&msg, sender);
                break;
        }
    }

    return 0; // never reached
}

static void handle_shutdown(int pid, struct yfs_msg *msg) {
    TracePrintf(0, "yfs: shutting down\n");

    //do cache stuff sync/flush 

    msg->arg1 = 0;   // Shutdown always returns 0
    Reply(msg, pid);

    Exit(0);
}