#include <comp421/filesystem.h>
#include <comp421/yalnix.h>
#include <comp421/iolib.h>
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

// ======================== Free List Nodes and Operations =========================
typedef struct free_node {
    int num; // inode number for inode list, block number for data list
    struct free_node *next;
} free_node_t;

// each node holds a single inode number. Each number represents an inode number that is free
static free_node_t *free_inode_list = NULL;
// each node holds a single block number. Each number represents a disk block that is free
static free_node_t *free_block_list = NULL;

static int num_blocks_total  = 0;
static int num_inodes_total  = 0;
static int first_data_block  = 0;

// Push a number onto a free list.
static void push_free(free_node_t **list, int num) {
    free_node_t *node = malloc(sizeof(free_node_t));
    if (!node) {
        TracePrintf(0, "fs_init: malloc failed\n");
        Exit(ERROR);
    }
    node->num  = num;
    node->next = *list;
    *list = node;
}
 
// Pop a number from a free list. Returns -1 if empty.
int pop_free(free_node_t **list) {
    if (!*list) return -1;
    free_node_t *node = *list;
    int num = node->num;
    *list = node->next;
    free(node);
    return num;
}

int alloc_inode_num(void) { return pop_free(&free_inode_list); }
int alloc_block_num(void) { return pop_free(&free_block_list); }
void free_inode_num(int inum) { push_free(&free_inode_list, inum); }
void free_block_num(int blkno) { push_free(&free_block_list, blkno); }


// Wrapper around ReadSector that mallocs a fresh BLOCKSIZE buffer, reads the sector into it, and returns the pointer.
static void *read_block(int blockno) {
    void *buf = malloc(BLOCKSIZE);
    if (!buf) {
        TracePrintf(0, "fs_init: malloc failed reading block %d\n", blockno);
        Exit(ERROR);
    }
    if (ReadSector(blockno, buf) == ERROR) {
        TracePrintf(0, "fs_init: ReadSector(%d) failed\n", blockno);
        Exit(ERROR);
    }
    return buf;
}

// This is called for every live (non-free) inode during the scan. Its job is to stamp used[block_number] = 1 
// for every block that inode owns, so those blocks don't end up on the free block list.
static void mark_inode_blocks(struct inode *in, char *used) {
    if (in->size <= 0) return;
 
    // How many data blocks does this inode actually need?
    int num_data_blocks = (in->size + BLOCKSIZE - 1) / BLOCKSIZE;
 
    // Mark direct blocks.
    int d;
    for (d = 0; d < NUM_DIRECT && d < num_data_blocks; d++) {
        int blk = in->direct[d];
        if (blk > 0 && blk < num_blocks_total) {
            used[blk] = 1;
        }
    }
 
    // If we need more than NUM_DIRECT blocks, there is an indirect block.
    if (num_data_blocks > NUM_DIRECT) {
        int indirect_blk = in->indirect;
        if (indirect_blk > 0 && indirect_blk < num_blocks_total) {
            used[indirect_blk] = 1; // the indirect block itself
 
            // Read the indirect block to find the data blocks it points to
            int *indirect_buf = read_block(indirect_blk);
 
            int num_indirect = num_data_blocks - NUM_DIRECT;
            int i;
            for (i = 0; i < num_indirect; i++) {
                int blk = indirect_buf[i];
                if (blk > 0 && blk < num_blocks_total) {
                    used[blk] = 1;
                }
            }
            free(indirect_buf);
        }
    }
}

//========================= Helpers ===========================
int load_inode(int inum, struct inode *out) {
    if (out == NULL) {
        return ERROR;
    }

    if (inum < 1 || inum > num_inodes_total) {
        return ERROR;
    }

    int inodes_per_block = BLOCKSIZE / INODESIZE;

    int iblock = 1 + (inum / inodes_per_block);
    int islot  = inum % inodes_per_block;

    void *iblock_buf = read_block(iblock);
    if (iblock_buf == NULL) {
        return ERROR;
    }

    struct inode *inode_array = (struct inode *)iblock_buf;
    *out = inode_array[islot];

    free(iblock_buf);
    return 0;
}

int lookup_path_v1(const char *path, int *out_inum) {
    if (path[0] == '/' && path[1] == '\0') {
        *out_inum = ROOTINODE;
        return 0;
    }
    return ERROR;
}

// ======================== Initialization Function =========================
void fs_init(void) {
    /* Step 1: read the file-system header from block 1 */
 
    void *block1_buf = read_block(1);
    struct fs_header *hdr = (struct fs_header *)block1_buf;
 
    num_blocks_total = hdr->num_blocks;
    num_inodes_total = hdr->num_inodes;
    TracePrintf(1, "fs_init: num_blocks=%d  num_inodes=%d\n", num_blocks_total, num_inodes_total);
 
    /*
     * Inodes per block = BLOCKSIZE / INODESIZE = 512 / 64 = 8.
     * Block 1 holds the fs_header (slot 0) plus up to 7 inodes.
     * Total slots (header + inodes) = num_inodes + 1.
     * Number of blocks occupied by header+inodes: ceil((num_inodes + 1) / inodes_per_block)
     */
    int inodes_per_block = BLOCKSIZE / INODESIZE; //8
    int inode_blocks = (num_inodes_total + 1 + inodes_per_block - 1) / inodes_per_block;
 
    first_data_block = 1 + inode_blocks;
    TracePrintf(0, "fs_init: inode_blocks=%d  first_data_block=%d\n",
                inode_blocks, first_data_block);
 

    /* Step 2: allocate a bitmap for data blocks */
    
    /*
     * used[i] == 1 means block i is already allocated to some inode.
     * We start by marking the boot block, all inode blocks as used
     * (they are never free data blocks).
     */
    char *used = calloc(num_blocks_total, sizeof(char));
    if (!used) {
        TracePrintf(0, "fs_init: calloc for used[] failed\n");
        Exit(ERROR);
    }
    // Fill in 1 for boot block (0) and all inode-holding blocks
    int i;
    for (i = 0; i < first_data_block; i++) {
        used[i] = 1;
    }
 
    /* Step 3: scan every inode to determine free and used ones, build inode list*/
 
    /*
     * Block number for inode N = 1 + (N / inodes_per_block)
     * Slot within that block = N % inodes_per_block
     * Byte offset within block = slot * INODESIZE
     */
 
    int current_iblock = -1; // which inode-block is currently loaded
    void *iblock_buf = NULL; // buffer for current inode block
 
    int inum;
    for (inum = 1; inum <= num_inodes_total; inum++) {
 
        int iblock = 1 + (inum / inodes_per_block);
        int islot  =      inum % inodes_per_block;
 
        // Load the inode block if we haven't already.
        if (iblock != current_iblock) {
            free(iblock_buf);
            iblock_buf     = read_block(iblock);
            current_iblock = iblock;
        }
 
        struct inode *in = (struct inode *)iblock_buf + islot;

        if (in->type == INODE_FREE) {
            // This inode is available for allocation.
            push_free(&free_inode_list, inum);
            TracePrintf(3, "fs_init: inode %d is FREE\n", inum);
        } else {
            // Live inode — mark the blocks it owns as used.
            TracePrintf(3, "fs_init: inode %d type=%d size=%d\n", inum, in->type, in->size);
            mark_inode_blocks(in, used);
        }
    }
 
    free(iblock_buf);
    free(block1_buf);
 
    /* Step 4: build the free block list */
 
    /*
     * Any data block (first_data_block .. num_blocks_total-1) not in
     * 'used' is free.  We iterate in reverse so that lower-numbered
     * blocks end up at the head of the list (allocated first).
     */
    for (i = num_blocks_total - 1; i >= first_data_block; i--) {
        if (!used[i]) {
            push_free(&free_block_list, i);
        }
    }
 
    free(used);
 
    TracePrintf(0, "fs_init: initialisation complete. "  "Free inodes and blocks are ready.\n");
}


// ===== Handler prototypes =====
static void handle_shutdown(int pid, struct yfs_msg *msg);
static void handle_sync(int pid, struct yfs_msg *msg);
static void handle_stat(int pid, struct yfs_msg *msg);
static void handle_open(int pid, struct yfs_msg *msg);

//TODO: add other handlers


int main(int argc, char **argv) {

    //register service id
    if (Register(FILE_SERVER) == ERROR) {
        TracePrintf(0, "yfs: Register failed\n");
        Exit(ERROR);
    }

    TracePrintf(1, "yfs: Registered as FILE_SERVER\n");

    fs_init();


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
            case YFS_REQ_OPEN:
                handle_open(sender, &msg);
                break;
            case YFS_REQ_STAT:
                handle_stat(sender, &msg);
                break;
            case YFS_REQ_SYNC:
                handle_sync(sender, &msg);
                break;
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

static void handle_open(int pid, struct yfs_msg *msg) {
    char pathbuf[MAXPATHNAMELEN];

    if (CopyFrom(pid, pathbuf, msg->ptr1, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    int inum;

    if (lookup_path_v1(pathbuf, &inum) == ERROR) {
        //msg->arg1 = ERROR;
        //Reply(msg, pid);
        return;
    }

    struct inode in;
    if (load_inode(inum, &in) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    msg->arg1 = 0;        // success
    msg->arg2 = inum;
    msg->arg3 = in.reuse;

    Reply(msg, pid);
}

static void handle_stat(int pid, struct yfs_msg *msg) {
    char pathbuf[MAXPATHNAMELEN];

    //copy pathname from client
    if (CopyFrom(pid, pathbuf, msg->ptr1, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    int inum;

    //Resolve pathname, get inode#
    if (lookup_path_v1(pathbuf, &inum) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    //Load inode
    struct inode in;
    if (load_inode(inum, &in) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    //Build Stat struct
    struct Stat st;
    st.inum  = inum;
    st.type  = in.type;
    st.size  = in.size;
    st.nlink = in.nlink;

    //Copy result back to client
    if (CopyTo(pid, msg->ptr2, &st, sizeof(st)) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    //Reply success
    msg->arg1 = 0;
    Reply(msg, pid);
}

static void handle_sync(int pid, struct yfs_msg *msg) {
    msg->arg1 = 0;
    //TODO: write all dirty cached inodes back to their corresponding disk blocks (in the cache) and
    //then writes all dirty cached disk blocks to the disk.
    Reply(msg, pid);
}

static void handle_shutdown(int pid, struct yfs_msg *msg) {
    TracePrintf(0, "yfs: shutting down\n");

    //do cache stuff sync/flush 

    msg->arg1 = 0;   // Shutdown always returns 0
    Reply(msg, pid);

    Exit(0);
}
