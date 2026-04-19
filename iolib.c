#include <comp421/iolib.h>
#include <comp421/yalnix.h>
#include <stddef.h>
#include <comp421/filesystem.h>
#include <stdlib.h>

//need to update every call
static int curdir_inum = ROOTINODE;
static int curdir_reuse = 0;

struct open_file {
    int inum;
    int reuse;
    int offset;
};

static struct open_file *open_table[MAX_OPEN_FILES];

static int alloc_fd(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_table[i] == NULL) return i;
    }
    return ERROR;
}



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
    YFS_REQ_SHUTDOWN,
    //helpers
    YFS_REQ_GETFSIZE
};

int ChDir(char *pathname) {
    struct yfs_msg msg;

    msg.type = YFS_REQ_CHDIR;
    msg.arg1 = curdir_inum;
    msg.arg2 = curdir_reuse;
    msg.arg3 = 0;
    msg.ptr1 = pathname;
    msg.ptr2 = NULL;

    if (Send(&msg, -FILE_SERVER) == ERROR) {
        return ERROR;
    }

    if (msg.arg1 == ERROR) {
        return ERROR;
    }

    // update current directory
    curdir_inum = msg.arg1;
    curdir_reuse = msg.arg2;

    return 0;
}

int MkDir(char *pathname) {
    struct yfs_msg msg;
    msg.type = YFS_REQ_MKDIR;
    msg.arg1 = curdir_inum;
    msg.arg2 = curdir_reuse;
    msg.arg3 = 0;
    msg.ptr1 = pathname;
    msg.ptr2 = NULL;

    if (Send(&msg, -FILE_SERVER) == ERROR) return ERROR;
    return msg.arg1;
}

int RmDir(char *pathname) {
    struct yfs_msg msg;
    msg.type = YFS_REQ_RMDIR;
    msg.arg1 = curdir_inum;
    msg.arg2 = curdir_reuse;
    msg.arg3 = 0;
    msg.ptr1 = pathname;
    msg.ptr2 = NULL;

    if (Send(&msg, -FILE_SERVER) == ERROR) return ERROR;
    return msg.arg1;
}

int Read(int fd, void *buf, int size){

    if(fd < 0 || fd >= MAX_OPEN_FILES){
        return ERROR;
    }

    if(open_table[fd] == NULL){
        return ERROR;
    }

    if(size < 0){
        return ERROR;
    }

    struct yfs_msg msg;

    msg.type = YFS_REQ_READ;
    msg.arg1 = open_table[fd]->inum;
    msg.arg2 = open_table[fd]->offset;
    msg.arg3 = size;
    msg.ptr1 = buf;

    if (Send(&msg, -FILE_SERVER) == ERROR) {
        return ERROR;
    }

    if (msg.arg1 == ERROR) {
        return ERROR;
    }
    
    // server returns bytes_read in arg1
    open_table[fd]->offset += msg.arg1;
    return msg.arg1;
}

int Open(char *pathname) {
    struct yfs_msg msg;

    msg.type = YFS_REQ_OPEN;
    msg.arg1 = curdir_inum;
    msg.arg2 = curdir_reuse;
    msg.ptr1 = pathname;

    if (Send(&msg, -FILE_SERVER) == ERROR) {
        return ERROR;
    }

    if (msg.arg1 == ERROR) {
        return ERROR;
    }

    int fd = alloc_fd();
    if (fd == ERROR) return ERROR;

    struct open_file *of = malloc(sizeof(*of));
    of->inum = msg.arg2;
    of->reuse = msg.arg3;
    of->offset = 0;

    open_table[fd] = of;

    return fd;
}

int Close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return ERROR;
    }

    if (open_table[fd] == NULL) {
        return ERROR;
    }

    free(open_table[fd]);
    open_table[fd] = NULL;

    return 0;
}

int Create(char *pathname) {
    struct yfs_msg msg;

    msg.type = YFS_REQ_CREATE;
    msg.arg1 = curdir_inum;
    msg.arg2 = curdir_reuse;
    msg.arg3 = 0;
    msg.ptr1 = pathname;
    msg.ptr2 = NULL;

    if (Send(&msg, -FILE_SERVER) == ERROR) return ERROR;
    if (msg.arg1 == ERROR) return ERROR;

    int fd = alloc_fd();
    if (fd == ERROR) return ERROR;

    struct open_file *of = malloc(sizeof(*of));
    if (!of) return ERROR;
    of->inum   = msg.arg2;
    of->reuse  = msg.arg3;
    of->offset = 0;

    open_table[fd] = of;
    return fd;
}

int Write(int fd, void *buf, int size) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || open_table[fd] == NULL) return ERROR;
    if (size < 0) return ERROR;
    if (size == 0) return 0;

    struct open_file *of = open_table[fd];

    struct yfs_msg msg;
    msg.type = YFS_REQ_WRITE;
    msg.arg1 = of->inum; // which inode
    msg.arg2 = of->reuse; // staleness check
    msg.arg3 = of->offset; // where in the file to write
    msg.ptr1 = buf; // client-side buffer address (sent as raw pointer value)
    msg.ptr2 = (void *)(long)size;  // piggyback size in the pointer field

    if (Send(&msg, -FILE_SERVER) == ERROR) return ERROR;
    if (msg.arg1 == ERROR) return ERROR;

    int written = msg.arg1;
    of->offset += written;
    return written;
}

int Seek(int fd, int offset, int whence){

    if(fd < 0 || fd >= MAX_OPEN_FILES){
        return ERROR;
    }

    if(open_table[fd] == NULL){
        return ERROR;
    }

    int base;

    if (whence == SEEK_SET){
        base = 0;
    }else if (whence == SEEK_CUR){
        base = open_table[fd]->offset;
    }else if (whence == SEEK_END){
        //get file size
        struct yfs_msg msg;
        msg.type = YFS_REQ_GETFSIZE;
        msg.arg1 = open_table[fd]->inum;

        if(Send(&msg, -FILE_SERVER) == ERROR){
            return ERROR;
        }

        if(msg.arg1 == ERROR){
            return ERROR;
        }
        base = msg.arg2;
    } else {
        return ERROR;
    }

    int new_pos = base + offset;

    if(new_pos < 0){
        return ERROR;
    }

    //upddate pos
    open_table[fd]->offset = new_pos;
    
    return new_pos;

}

int Stat(char *pathname, struct Stat *statbuf) {
    struct yfs_msg msg;

    msg.type = YFS_REQ_STAT;

    msg.arg1 = curdir_inum;    //cd's inode#
    msg.arg2 = curdir_reuse;   //# reuse
    msg.arg3 = 0;

    msg.ptr1 = pathname;       // input
    msg.ptr2 = statbuf;        // output buffer

    if (Send(&msg, -FILE_SERVER) == ERROR) {
        return ERROR;
    }

    return msg.arg1;  // 0 or ERROR
}


int Sync(void) {
    struct yfs_msg msg;
    msg.type = YFS_REQ_SYNC;
    
    if (Send(&msg, -FILE_SERVER) == ERROR) return ERROR;
    return msg.arg1;
}

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