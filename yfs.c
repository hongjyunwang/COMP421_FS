#include <comp421/filesystem.h>
#include <comp421/yalnix.h>
#include <comp421/iolib.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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

// Read the blk_idx-th logical data block of inode `in` into `buf`.
// Handles direct blocks, indirect blocks, and holes (zero-filled).
static int inode_read_block(struct inode *in, int blk_idx, void *buf) {
    int blkno;

    if (blk_idx < NUM_DIRECT) {
        blkno = in->direct[blk_idx];
    } else {
        if (in->indirect == 0) {
            memset(buf, 0, BLOCKSIZE);
            return 0;
        }
        int *indirect_buf = read_block(in->indirect);
        if (!indirect_buf) return ERROR;
        blkno = indirect_buf[blk_idx - NUM_DIRECT];
        free(indirect_buf);
    }

    if (blkno == 0) {
        // hole in file
        memset(buf, 0, BLOCKSIZE);
        return 0;
    }

    return ReadSector(blkno, buf);
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
// Search directory inode `dir_inum` for a component `name` of length `namelen`.
// Returns inode number on success, ERROR if not found.
static int dir_lookup(int dir_inum, const char *name, int namelen) {
    struct inode dir_in;
    if (load_inode(dir_inum, &dir_in) == ERROR) return ERROR;
    if (dir_in.type != INODE_DIRECTORY) return ERROR;

    int total_entries    = dir_in.size / sizeof(struct dir_entry);
    int entries_per_block = BLOCKSIZE  / sizeof(struct dir_entry);
    char block_buf[BLOCKSIZE];

    for (int i = 0; i < total_entries; i++) {
        // only read a new block when we cross a block boundary
        if (i % entries_per_block == 0) {
            if (inode_read_block(&dir_in, i / entries_per_block, block_buf) == ERROR)
                return ERROR;
        }

        struct dir_entry *ent = (struct dir_entry *)block_buf + (i % entries_per_block);
        if (ent->inum == 0) continue; // free entry, skip

        // names are NOT null-terminated when exactly DIRNAMELEN chars long
        // so we can't use strcmp — use memcmp + manual length check
        if (namelen > DIRNAMELEN) continue;
        if (memcmp(ent->name, name, namelen) != 0) continue;
        if (namelen < DIRNAMELEN && ent->name[namelen] != '\0') continue;

        return ent->inum;
    }
    return ERROR;
}


// Full path lookup.
// cwd_inum:      starting directory for relative paths
// symlink_depth: tracks recursion for MAXSYMLINKS enforcement (pass 0 from callers)
// follow_last:   1 = follow symlink at last component (Open, Create, ChDir)
//                0 = return symlink inode itself    (Unlink, ReadLink, etc.)
// Returns inode number on success, ERROR on failure.
int lookup_path(const char *path, int cwd_inum, int symlink_depth, int follow_last) {
    // Error checking
    if (path == NULL || path[0] == '\0') return ERROR;
    if (symlink_depth > MAXSYMLINKS) return ERROR;

    // setup
    int cur_inum = (path[0] == '/') ? ROOTINODE : cwd_inum;
    const char *p = path;
    while (*p == '/') p++;   // skip leading slashes
    if (*p == '\0') return cur_inum;  // path was just "/"

    while (*p != '\0') {
        // each iteration extracts one component

        // extract next component
        const char *start = p;
        while (*p != '/' && *p != '\0') p++;
        int complen = p - start;

        // peek past any slashes following this component
        const char *q = p;
        while (*q == '/') q++;
        int is_last = (*q == '\0'); // true if there is nothing after the slashes
        int trailing_slash = (p != q && is_last); // slashes at end of path
        p = q; // advance past slashes

        if (complen > DIRNAMELEN) return ERROR;
        int next_inum = dir_lookup(cur_inum, start, complen);
        if (next_inum == ERROR) return ERROR;

        struct inode next_in;
        if (load_inode(next_inum, &next_in) == ERROR) return ERROR;

        // handle symlinks
        if (next_in.type == INODE_SYMLINK) {
            // don't follow if it's the last component with no trailing slash and follow_last=0
            if (is_last && !trailing_slash && !follow_last)
                return next_inum;

            if (symlink_depth >= MAXSYMLINKS) return ERROR;

            // read the symlink target out of its data blocks
            char target[MAXPATHNAMELEN];
            int len = next_in.size;
            if (len <= 0 || len >= MAXPATHNAMELEN) return ERROR;

            char block_buf[BLOCKSIZE];
            int bytes_read = 0, blk_idx = 0;
            while (bytes_read < len) {
                if (inode_read_block(&next_in, blk_idx++, block_buf) == ERROR)
                    return ERROR;
                int to_copy = len - bytes_read;
                if (to_copy > BLOCKSIZE) to_copy = BLOCKSIZE;
                memcpy(target + bytes_read, block_buf, to_copy);
                bytes_read += to_copy;
            }
            target[len] = '\0';

            // relative symlink targets are resolved from the directory
            // that contains the symlink, not from cwd
            int symlink_base = (target[0] == '/') ? ROOTINODE : cur_inum;

            if (is_last) {
                // nothing remains after symlink — resolve target fully
                return lookup_path(target, symlink_base, symlink_depth + 1, follow_last);
            } else {
                // more path remains after symlink — resolve target, then continue
                int resolved = lookup_path(target, symlink_base, symlink_depth + 1, 1);
                if (resolved == ERROR) return ERROR;
                return lookup_path(p, resolved, symlink_depth, follow_last);
            }
        }

        // intermediate components must be directories
        if (!is_last || trailing_slash) {
            if (next_in.type != INODE_DIRECTORY) return ERROR;
        }

        cur_inum = next_inum;

        // spec: trailing slash = treat as if followed by "."
        // "." maps to cur_inum, which we've already verified is a directory
        if (trailing_slash) return cur_inum;
    }

    return cur_inum;
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

    if ((inum = lookup_path(pathbuf, msg->arg1, 0, 0)) == ERROR) {
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
    if ((inum = lookup_path(pathbuf, msg->arg1, 0, 1)) == ERROR) {
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
