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
    YFS_REQ_SHUTDOWN,
    //helpers
    YFS_REQ_GETFSIZE
};

// ======================== Cache =========================
//temp
//#define BLOCK_CACHESIZE_1 3   
typedef struct cache_block {
    int valid;
    int dirty;
    int blockno;

    char data[BLOCKSIZE];

    //chain
    struct cache_block *hash_next;

    //LRU doubly linked list
    struct cache_block *lru_prev;
    struct cache_block *lru_next;
} cache_block_t;

//fixed size cache
static cache_block_t block_cache[BLOCK_CACHESIZE];

//bucket heads
#define BLOCK_HASH_SIZE 64   //should be enough
static cache_block_t *block_hash[BLOCK_HASH_SIZE];

//LRU ptrs
static cache_block_t *lru_head = NULL;   /* most recently used */
static cache_block_t *lru_tail = NULL;   /* least recently used */

//simple hash func
static int block_hash_fn(int blockno) {
    return blockno % BLOCK_HASH_SIZE;
}

//LRU helpers, ins and rm
static void lru_remove(cache_block_t *cb) {
    if (cb->lru_prev) cb->lru_prev->lru_next = cb->lru_next;
    else lru_head = cb->lru_next;

    if (cb->lru_next) cb->lru_next->lru_prev = cb->lru_prev;
    else lru_tail = cb->lru_prev;

    cb->lru_prev = NULL;
    cb->lru_next = NULL;
}

static void lru_insert_head(cache_block_t *cb) {
    cb->lru_prev = NULL;
    cb->lru_next = lru_head;

    if (lru_head) lru_head->lru_prev = cb;
    else lru_tail = cb;

    lru_head = cb;
}

//mark recently used
static void lru_touch(cache_block_t *cb) {
    if (lru_head == cb) return;
    lru_remove(cb);
    lru_insert_head(cb);
}
//hastable helpers
static void hash_insert(cache_block_t *cb) {
    int h = block_hash_fn(cb->blockno);
    cb->hash_next = block_hash[h];
    block_hash[h] = cb;
}

static void hash_remove(cache_block_t *cb) {
    int h = block_hash_fn(cb->blockno);
    cache_block_t **pp = &block_hash[h];

    while (*pp) {
        if (*pp == cb) {
            *pp = cb->hash_next;
            cb->hash_next = NULL;
            return;
        }
        pp = &((*pp)->hash_next);
    }
}

static cache_block_t *hash_find(int blockno) {
    int h = block_hash_fn(blockno);
    cache_block_t *cur = block_hash[h];

    while (cur) {
        if (cur->valid && cur->blockno == blockno) {
            return cur;
        }
        cur = cur->hash_next;
    }
    return NULL;
}

//cache init
static void block_cache_init(void) {
    memset(block_cache, 0, sizeof(block_cache));
    memset(block_hash, 0, sizeof(block_hash));
    lru_head = NULL;
    lru_tail = NULL;

    for (int i = 0; i < BLOCK_CACHESIZE; i++) {
        lru_insert_head(&block_cache[i]);
    }
}

//flush
static int block_cache_flush_entry(cache_block_t *cb) {
    if (!cb->valid || !cb->dirty) return 0;

    if (cb->valid && cb->dirty) {
    TracePrintf(1, "FLUSH block %d\n", cb->blockno);
    }

    if (WriteSector(cb->blockno, cb->data) == ERROR) {
        return ERROR;
    }

    cb->dirty = 0;
    return 0;
}

//cached get block
static cache_block_t *block_cache_get(int blockno) {
    cache_block_t *cb = hash_find(blockno);
    if (cb) {
        TracePrintf(1, "CACHE HIT block %d\n", blockno);
        lru_touch(cb);             //hit
        return cb;
    }

    //miss, evict tail
    TracePrintf(1, "CACHE MISS block %d\n", blockno);
    cb = lru_tail;
    if (cb == NULL) return NULL;

    //remove old mapping if valid 
    if (cb->valid) {
        TracePrintf(1, "EVICT block %d dirty=%d\n", cb->blockno, cb->dirty);
        if (block_cache_flush_entry(cb) == ERROR) {
            return NULL;
        }
        hash_remove(cb);
    }

    //load requested block
    if (ReadSector(blockno, cb->data) == ERROR) {
        return NULL;
    }
    TracePrintf(1, "LOAD block %d into cache\n", blockno);

    cb->valid = 1;
    cb->dirty = 0;
    cb->blockno = blockno;
    cb->hash_next = NULL;

    hash_insert(cb);
    lru_touch(cb);

    return cb;
}

//Read and Write wrappers 
static int cache_read_block(int blockno, void *buf) {
    cache_block_t *cb = block_cache_get(blockno);
    if (cb == NULL) return ERROR;

    memcpy(buf, cb->data, BLOCKSIZE);
    return 0;
}

static int cache_write_block(int blockno, const void *buf) {
    cache_block_t *cb = block_cache_get(blockno);
    if (cb == NULL) return ERROR;

    memcpy(cb->data, buf, BLOCKSIZE);
    cb->dirty = 1;
    TracePrintf(1, "CACHE WRITE block %d (mark dirty)\n", blockno);
    lru_touch(cb);
    return 0;
}

//flush all for Sync and Shutdown
static int block_cache_flush_all(void) {
    TracePrintf(1, "FLUSH ALL CACHE\n");
    for (int i = 0; i < BLOCK_CACHESIZE; i++) {
        if (block_cache_flush_entry(&block_cache[i]) == ERROR) {
            return ERROR;
        }
    }
    return 0;
}

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
    if (cache_read_block(blockno, buf) == ERROR) {
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

    return cache_read_block(blkno, buf);;
}

static int inode_read_bytes(struct inode *in, int offset, char *dst, int len) {
    if (in == NULL || dst == NULL) {
        return ERROR;
    }

    if (offset < 0 || len < 0) {
        return ERROR;
    }

    if (offset > in->size) {
        return ERROR;
    }

    if (offset + len > in->size) {
        return ERROR;
    }

    int copied = 0;
    char block_buf[BLOCKSIZE];

    while (copied < len) {
        int abs_off = offset + copied;
        int blk_idx = abs_off / BLOCKSIZE;
        int blk_off = abs_off % BLOCKSIZE;

        if (inode_read_block(in, blk_idx, block_buf) == ERROR) {
            return ERROR;
        }

        int chunk = len - copied;
        int space_in_block = BLOCKSIZE - blk_off;
        if (chunk > space_in_block) {
            chunk = space_in_block;
        }

        memcpy(dst + copied, block_buf + blk_off, chunk);
        copied += chunk;
    }

    return 0;
}

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

// Save a modified inode back to its disk block.
static int save_inode(int inum, struct inode *in) {
    int inodes_per_block = BLOCKSIZE / INODESIZE;
    int iblock = 1 + (inum / inodes_per_block);
    int islot  = inum % inodes_per_block;

    void *iblock_buf = read_block(iblock);
    if (!iblock_buf) return ERROR;

    struct inode *inode_array = (struct inode *)iblock_buf;
    inode_array[islot] = *in;

    if (cache_write_block(iblock, iblock_buf) == ERROR) {
        free(iblock_buf);
        return ERROR;
    }
    free(iblock_buf);
    return 0;
}

// Write a full BLOCKSIZE buffer into the blk_idx-th logical data block
// of inode `in`, allocating a new disk block if that slot was empty.
// Also allocates and writes back the indirect block if needed.
// Returns 0 on success, ERROR on failure.
static int inode_write_block(struct inode *in, int blk_idx, void *buf) {
    int max_blocks = NUM_DIRECT + BLOCKSIZE / (int)sizeof(int);
    if (blk_idx >= max_blocks) return ERROR;

    int blkno;

    if (blk_idx < NUM_DIRECT) {
        // Direct block path
        if (in->direct[blk_idx] == 0) {
            blkno = alloc_block_num();
            if (blkno == ERROR) return ERROR;
            in->direct[blk_idx] = blkno;
        } else {
            blkno = in->direct[blk_idx];
        }
        return cache_write_block(blkno, buf);

    } else {
        // Indirect block path
        int *indirect_buf;

        if (in->indirect == 0) {
            // Allocate the indirect block itself for the first time
            int ind_blkno = alloc_block_num();
            if (ind_blkno == ERROR) return ERROR;
            in->indirect = ind_blkno;
            indirect_buf = calloc(1, BLOCKSIZE); // zero all entries
            if (!indirect_buf) return ERROR;
        } else {
            indirect_buf = read_block(in->indirect);
            if (!indirect_buf) return ERROR;
        }

        int idx = blk_idx - NUM_DIRECT;

        if (indirect_buf[idx] == 0) {
            blkno = alloc_block_num();
            if (blkno == ERROR) { free(indirect_buf); return ERROR; }
            indirect_buf[idx] = blkno;
        } else {
            blkno = indirect_buf[idx];
        }

        // Write the data block first
        if (cache_write_block(blkno, buf) == ERROR) {
            free(indirect_buf); return ERROR;
        }

        // Write the (possibly updated) indirect block back
        if (cache_write_block(in->indirect, indirect_buf) == ERROR) {
            free(indirect_buf); return ERROR;
        }

        free(indirect_buf);
        return 0;
    }
}

// Search directory inode `dir_inum` for a component `name` of length `namelen`.
// Returns inode number on success, ERROR if not found.
static int dir_lookup(int dir_inum, const char *name, int namelen) {
    struct inode dir_in;
    if (load_inode(dir_inum, &dir_in) == ERROR) return ERROR;
    if (dir_in.type != INODE_DIRECTORY) return ERROR;

    TracePrintf(0, "dir_lookup: dir_inum=%d name='%.*s'\n", dir_inum, namelen, name);
    TracePrintf(0, "dir inode: type=%d size=%d\n", dir_in.type, dir_in.size);

    int total_entries    = dir_in.size / sizeof(struct dir_entry);
    int entries_per_block = BLOCKSIZE  / sizeof(struct dir_entry);
    char block_buf[BLOCKSIZE];

    TracePrintf(0, "total_entries=%d entries_per_block=%d\n", total_entries, entries_per_block);

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

        TracePrintf(0, "entry %d: inum=%d name='%.*s'\n", i, ent->inum, DIRNAMELEN, ent->name);

        return ent->inum;
    }
    return ERROR;
}

// Split `path` into parent directory and last component name.
// parent_out must be MAXPATHNAMELEN bytes, name_out must be DIRNAMELEN+1 bytes.
// Returns 0 on success, ERROR if path is empty or last component is invalid.
static int split_path(const char *path, char *parent_out, char *name_out) {
    int len = strlen(path);
    if (len == 0) return ERROR;

    // strip trailing slashes
    while (len > 1 && path[len-1] == '/') len--;

    // find last slash
    int last_slash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (path[i] == '/') { last_slash = i; break; }
    }

    const char *name_start;
    int name_len;

    if (last_slash == -1) {
        // relative path — parent is current directory
        strncpy(parent_out, ".", MAXPATHNAMELEN - 1);
        parent_out[MAXPATHNAMELEN - 1] = '\0';
        name_start = path;
        name_len = len;
    } else {
        int parent_len = (last_slash == 0) ? 1 : last_slash;
        if (parent_len >= MAXPATHNAMELEN) return ERROR;
        memcpy(parent_out, path, parent_len);
        parent_out[parent_len] = '\0';
        name_start = path + last_slash + 1;
        name_len = len - last_slash - 1;
    }

    if (name_len == 0 || name_len > DIRNAMELEN) return ERROR;
    memcpy(name_out, name_start, name_len);
    name_out[name_len] = '\0';
    return 0;
}

// Add a directory entry to dir_inum, reusing a free slot if one exists,
// otherwise appending a new entry. Returns 0 on success, ERROR on failure.
static int dir_add_entry(int dir_inum, int new_inum, const char *name) {
    struct inode dir_in;
    if (load_inode(dir_inum, &dir_in) == ERROR) return ERROR;
    if (dir_in.type != INODE_DIRECTORY) return ERROR;

    int entries_per_block = BLOCKSIZE / sizeof(struct dir_entry);
    int total_entries = dir_in.size / sizeof(struct dir_entry);
    char block_buf[BLOCKSIZE];

    // scan for a free slot (inum == 0)
    int free_blk_idx = -1, free_blk_off = -1;
    for (int i = 0; i < total_entries; i++) {
        int blk_idx = i / entries_per_block;
        int blk_off = i % entries_per_block;
        if (blk_off == 0) {
            if (inode_read_block(&dir_in, blk_idx, block_buf) == ERROR)
                return ERROR;
        }
        struct dir_entry *ent = (struct dir_entry *)block_buf + blk_off;
        if (ent->inum == 0) {
            free_blk_idx = blk_idx;
            free_blk_off = blk_off;
            break;
        }
    }

    if (free_blk_idx != -1) {
        // reuse the free slot
        if (inode_read_block(&dir_in, free_blk_idx, block_buf) == ERROR)
            return ERROR;
        struct dir_entry *ent = (struct dir_entry *)block_buf + free_blk_off;
        ent->inum = new_inum;
        memset(ent->name, 0, DIRNAMELEN);
        strncpy(ent->name, name, DIRNAMELEN);
        return inode_write_block(&dir_in, free_blk_idx, block_buf);
    }

    // no free slot (append)
    int new_idx = total_entries;
    int blk_idx = new_idx / entries_per_block;
    int blk_off = new_idx % entries_per_block;

    if (blk_off == 0) {
        memset(block_buf, 0, BLOCKSIZE);
    } else {
        if (inode_read_block(&dir_in, blk_idx, block_buf) == ERROR)
            return ERROR;
    }

    struct dir_entry *ent = (struct dir_entry *)block_buf + blk_off;
    ent->inum = new_inum;
    memset(ent->name, 0, DIRNAMELEN);
    strncpy(ent->name, name, DIRNAMELEN);

    if (inode_write_block(&dir_in, blk_idx, block_buf) == ERROR)
        return ERROR;

    dir_in.size += sizeof(struct dir_entry);
    return save_inode(dir_inum, &dir_in);
}

// Free all data blocks (and the indirect block) owned by inode `in`.
// Zeros out direct[] and indirect. Does NOT save the inode — caller must do that.
static void inode_free_blocks(struct inode *in) {
    if (in->size == 0) return;

    int num_data_blocks = (in->size + BLOCKSIZE - 1) / BLOCKSIZE;

    for (int d = 0; d < NUM_DIRECT && d < num_data_blocks; d++) {
        if (in->direct[d] != 0) {
            free_block_num(in->direct[d]);
            in->direct[d] = 0;
        }
    }

    if (num_data_blocks > NUM_DIRECT && in->indirect != 0) {
        int *indirect_buf = read_block(in->indirect);
        if (indirect_buf) {
            int num_indirect = num_data_blocks - NUM_DIRECT;
            for (int i = 0; i < num_indirect; i++) {
                if (indirect_buf[i] != 0)
                    free_block_num(indirect_buf[i]);
            }
            free(indirect_buf);
        }
        free_block_num(in->indirect);
        in->indirect = 0;
    }

    in->size = 0;
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

    TracePrintf(0, "lookup_path: path='%s' start=%d\n", path, cur_inum);

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

        TracePrintf(0, "component='%.*s' cur_inum=%d is_last=%d trailing=%d\n",
            complen, start, cur_inum, is_last, trailing_slash);

        if (complen > DIRNAMELEN) return ERROR;
        int next_inum = dir_lookup(cur_inum, start, complen);
        if (next_inum == ERROR) return ERROR;

        TracePrintf(0, "dir_lookup returned next_inum=%d\n", next_inum);

        struct inode next_in;
        if (load_inode(next_inum, &next_in) == ERROR) return ERROR;

        TracePrintf(0, "next inode: type=%d size=%d\n", next_in.type, next_in.size);

        // handle symlinks
        if (next_in.type == INODE_SYMLINK) {
            TracePrintf(0, "SYMLINK\n");
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

// Remove a directory entry from dir_inum.
// Returns 0 on success, ERROR if not found or on failure.
static int dir_remove_entry(int dir_inum, const char *name) {
    struct inode dir_in;
    if (load_inode(dir_inum, &dir_in) == ERROR) return ERROR;
    if (dir_in.type != INODE_DIRECTORY) return ERROR;

    int namelen = strlen(name);
    if (namelen <= 0 || namelen > DIRNAMELEN) return ERROR;

    int total_entries = dir_in.size / sizeof(struct dir_entry);
    int entries_per_block = BLOCKSIZE / sizeof(struct dir_entry);
    char block_buf[BLOCKSIZE];

    for (int i = 0; i < total_entries; i++) {
        int blk_idx = i / entries_per_block;
        int blk_off = i % entries_per_block;

        if (blk_off == 0) {
            if (inode_read_block(&dir_in, blk_idx, block_buf) == ERROR)
                return ERROR;
        }

        struct dir_entry *ent = (struct dir_entry *)block_buf + blk_off;
        if (ent->inum == 0) continue;

        if (memcmp(ent->name, name, namelen) != 0) continue;
        if (namelen < DIRNAMELEN && ent->name[namelen] != '\0') continue;

        ent->inum = 0;
        memset(ent->name, 0, DIRNAMELEN);

        return inode_write_block(&dir_in, blk_idx, block_buf);
    }

    return ERROR;
}

// Returns 1 if directory is empty except for "." and ".." and free entries.
// Returns 0 if it contains anything else.
// Returns ERROR on failure.
static int dir_is_empty(int dir_inum) {
    struct inode dir_in;
    if (load_inode(dir_inum, &dir_in) == ERROR) return ERROR;
    if (dir_in.type != INODE_DIRECTORY) return ERROR;

    int total_entries = dir_in.size / sizeof(struct dir_entry);
    int entries_per_block = BLOCKSIZE / sizeof(struct dir_entry);
    char block_buf[BLOCKSIZE];

    for (int i = 0; i < total_entries; i++) {
        int blk_idx = i / entries_per_block;
        int blk_off = i % entries_per_block;

        if (blk_off == 0) {
            if (inode_read_block(&dir_in, blk_idx, block_buf) == ERROR)
                return ERROR;
        }

        struct dir_entry *ent = (struct dir_entry *)block_buf + blk_off;
        if (ent->inum == 0) continue;

        // "." ?
        if (ent->name[0] == '.' && ent->name[1] == '\0') continue;

        // ".." ?
        if (ent->name[0] == '.' && ent->name[1] == '.' && ent->name[2] == '\0') continue;

        return 0;   // found a real entry
    }

    return 1;   // only ".", "..", or free entries
}

// Resolve the parent directory of `path` and return the final component name.
// On success:
//   *parent_inum_out = inode number of parent directory
//   name_out = final component
// Returns 0 on success, ERROR on failure.
static int lookup_parent(const char *path, int cwd_inum,
                         int *parent_inum_out, char *name_out) {
    if (path == NULL || parent_inum_out == NULL || name_out == NULL)
        return ERROR;

    char parent_path[MAXPATHNAMELEN];
    char final_name[DIRNAMELEN + 1];

    if (split_path(path, parent_path, final_name) == ERROR)
        return ERROR;

    int parent_inum = lookup_path(parent_path, cwd_inum, 0, 1);
    if (parent_inum == ERROR)
        return ERROR;

    struct inode parent_in;
    if (load_inode(parent_inum, &parent_in) == ERROR)
        return ERROR;
    if (parent_in.type != INODE_DIRECTORY)
        return ERROR;

    *parent_inum_out = parent_inum;
    strcpy(name_out, final_name);
    return 0;
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
static void handle_read(int pid, struct yfs_msg *msg);
static void handle_getfsize(int pid, struct yfs_msg *msg);
static void handle_create(int pid, struct yfs_msg *msg);
static void handle_write(int pid, struct yfs_msg *msg);
static void handle_mkdir(int pid, struct yfs_msg *msg);
static void handle_rmdir(int pid, struct yfs_msg *msg);
static void handle_chdir(int pid, struct yfs_msg *msg);
static void handle_link(int pid, struct yfs_msg *msg);
static void handle_unlink(int pid, struct yfs_msg *msg);
static void handle_symlink(int pid, struct yfs_msg *msg);
static void handle_readlink(int pid, struct yfs_msg *msg);
//TODO: add other handlers


int main(int argc, char **argv) {

    //register service id
    if (Register(FILE_SERVER) == ERROR) {
        TracePrintf(0, "yfs: Register failed\n");
        Exit(ERROR);
    }

    TracePrintf(1, "yfs: Registered as FILE_SERVER\n");

    //init cache
    block_cache_init();

    TracePrintf(1, "fs_init: initialized block cache\n");

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
            case YFS_REQ_CHDIR:
                handle_chdir(sender, &msg);
                break;
            case YFS_REQ_MKDIR:
                handle_mkdir(sender, &msg);
                break;
            case YFS_REQ_RMDIR:
                handle_rmdir(sender, &msg);
                break;
            case YFS_REQ_READ:
                handle_read(sender, &msg);
                break;
            case YFS_REQ_OPEN:
                handle_open(sender, &msg);
                break;
            case YFS_REQ_CREATE:
                handle_create(sender, &msg);
                break;
            case YFS_REQ_WRITE:
                handle_write(sender, &msg);
                break;
            case YFS_REQ_STAT:
                handle_stat(sender, &msg);
                break;
            case YFS_REQ_GETFSIZE:
                handle_getfsize(sender, &msg);
                break;
            case YFS_REQ_SYNC:
                handle_sync(sender, &msg);
                break;
            case YFS_REQ_SHUTDOWN:
                handle_shutdown(sender, &msg);
                break;
            case YFS_REQ_LINK:
                handle_link(sender, &msg);
                break;
            case YFS_REQ_UNLINK:
                handle_unlink(sender, &msg);
                break;
            case YFS_REQ_SYMLINK:
                handle_symlink(sender, &msg);
                break;
            case YFS_REQ_READLINK:
                handle_readlink(sender, &msg);
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

static void handle_chdir(int pid, struct yfs_msg *msg) {
    char pathbuf[MAXPATHNAMELEN];

    if (CopyFrom(pid, pathbuf, msg->ptr1, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    int cwd_inum = msg->arg1;

    int target_inum = lookup_path(pathbuf, cwd_inum, 0, 1);
    if (target_inum == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    struct inode in;
    if (load_inode(target_inum, &in) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    if (in.type != INODE_DIRECTORY) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    msg->arg1 = target_inum;
    msg->arg2 = in.reuse;

    Reply(msg, pid);
}

static void handle_mkdir(int pid, struct yfs_msg *msg) {
    char pathbuf[MAXPATHNAMELEN];

    if (CopyFrom(pid, pathbuf, msg->ptr1, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    int cwd_inum = msg->arg1;

    int parent_inum;
    char name[DIRNAMELEN + 1];
    if (lookup_parent(pathbuf, cwd_inum, &parent_inum, name) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    if (dir_lookup(parent_inum, name, strlen(name)) != ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    int new_inum = alloc_inode_num();
    if (new_inum == -1) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    int new_blk = alloc_block_num();
    if (new_blk == -1) {
        free_inode_num(new_inum);
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    struct inode new_dir;
    memset(&new_dir, 0, sizeof(new_dir));
    new_dir.type = INODE_DIRECTORY;
    new_dir.nlink = 2;
    new_dir.reuse = 1;   // or increment from prior value if you track old reuse
    new_dir.size = 2 * sizeof(struct dir_entry);
    new_dir.direct[0] = new_blk;

    char block_buf[BLOCKSIZE];
    memset(block_buf, 0, BLOCKSIZE);

    struct dir_entry *ents = (struct dir_entry *)block_buf;
    ents[0].inum = new_inum;
    ents[0].name[0] = '.';

    ents[1].inum = parent_inum;
    ents[1].name[0] = '.';
    ents[1].name[1] = '.';

    if (WriteSector(new_blk, block_buf) == ERROR) {
        free_block_num(new_blk);
        free_inode_num(new_inum);
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    if (save_inode(new_inum, &new_dir) == ERROR) {
        free_block_num(new_blk);
        free_inode_num(new_inum);
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    if (dir_add_entry(parent_inum, new_inum, name) == ERROR) {
        inode_free_blocks(&new_dir);
        save_inode(new_inum, &new_dir);
        free_inode_num(new_inum);
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    struct inode parent_in;
    if (load_inode(parent_inum, &parent_in) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }
    parent_in.nlink++;
    if (save_inode(parent_inum, &parent_in) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    msg->arg1 = 0;
    Reply(msg, pid);
}

static void handle_rmdir(int pid, struct yfs_msg *msg) {
    char pathbuf[MAXPATHNAMELEN];

    if (CopyFrom(pid, pathbuf, msg->ptr1, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    int cwd_inum = msg->arg1;

    int parent_inum;
    char name[DIRNAMELEN + 1];
    if (lookup_parent(pathbuf, cwd_inum, &parent_inum, name) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    int target_inum = dir_lookup(parent_inum, name, strlen(name));
    if (target_inum == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    if (target_inum == ROOTINODE) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    struct inode target_in;
    if (load_inode(target_inum, &target_in) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    if (target_in.type != INODE_DIRECTORY) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    int empty = dir_is_empty(target_inum);
    if (empty != 1) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    if (dir_remove_entry(parent_inum, name) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    struct inode parent_in;
    if (load_inode(parent_inum, &parent_in) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }
    parent_in.nlink--;
    if (save_inode(parent_inum, &parent_in) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    inode_free_blocks(&target_in);
    target_in.type = INODE_FREE;
    target_in.nlink = 0;
    target_in.size = 0;

    if (save_inode(target_inum, &target_in) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    free_inode_num(target_inum);

    msg->arg1 = 0;
    Reply(msg, pid);
}

static void handle_read(int pid, struct yfs_msg *msg){
    
    int inum = msg->arg1;
    int offset = msg->arg2;
    int size = msg->arg3;
    void *client_buf = msg->ptr1;

    if (size < 0 || offset < 0) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    struct inode in;
    if (load_inode(inum, &in) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    int bytes_left = in.size - offset;
    if (bytes_left <= 0) {
        msg->arg1 = 0;   // EOF
        Reply(msg, pid);
        return;
    }

    int bytes_to_read = size;
    if (bytes_to_read > bytes_left) {
        bytes_to_read = bytes_left;
    }

    char *tmp = malloc(bytes_to_read);
    if(tmp == NULL){
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    if (inode_read_bytes(&in, offset, tmp, bytes_to_read) == ERROR) {
        free(tmp);
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    if (CopyTo(pid, client_buf, tmp, bytes_to_read) == ERROR) {
        free(tmp);
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    free(tmp);
    msg->arg1 = bytes_to_read;
    Reply(msg, pid);
}

static void handle_open(int pid, struct yfs_msg *msg) {
    char pathbuf[MAXPATHNAMELEN];

    if (CopyFrom(pid, pathbuf, msg->ptr1, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }

    int inum;

    if ((inum = lookup_path(pathbuf, msg->arg1, 0, 1)) == ERROR) {
        msg->arg1 = ERROR;
        Reply(msg, pid);
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

static void handle_getfsize(int pid, struct yfs_msg *msg){
    int inum = msg->arg1;

    struct inode in;
    if(load_inode(inum, &in) == ERROR){
        msg->arg1 = ERROR;
        Reply(msg, pid);
        return;
    }
    msg->arg1 = 0;//reply success
    msg->arg2 = in.size;
    Reply(msg, pid);
}

static void handle_sync(int pid, struct yfs_msg *msg) {
    msg->arg1 = 0;
    //TODO: write all dirty cached inodes back to their corresponding disk blocks (in the cache) and
    //then writes all dirty cached disk blocks to the disk.
    block_cache_flush_all();

    Reply(msg, pid);
}

static void handle_shutdown(int pid, struct yfs_msg *msg) {
    TracePrintf(0, "yfs: shutting down\n");

    //do cache stuff sync/flush 
    block_cache_flush_all();

    msg->arg1 = 0;   // Shutdown always returns 0
    Reply(msg, pid);

    Exit(0);
}

static void handle_create(int pid, struct yfs_msg *msg) {
    char pathbuf[MAXPATHNAMELEN];
    if (CopyFrom(pid, pathbuf, msg->ptr1, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // trailing slash implies the caller expects a directory — error for Create
    int plen = strlen(pathbuf);
    if (plen > 0 && pathbuf[plen - 1] == '/') {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    char parent_path[MAXPATHNAMELEN];
    char name[DIRNAMELEN + 1];
    if (split_path(pathbuf, parent_path, name) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // resolve parent directory
    int dir_inum = lookup_path(parent_path, msg->arg1, 0, 1);
    if (dir_inum == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }
    struct inode dir_in;
    if (load_inode(dir_inum, &dir_in) == ERROR || dir_in.type != INODE_DIRECTORY) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // check if name already exists in that directory
    int existing = dir_lookup(dir_inum, name, strlen(name));
    int file_inum;
    struct inode file_in;

    if (existing != ERROR) {
        // file already exists
        if (load_inode(existing, &file_in) == ERROR) {
            msg->arg1 = ERROR; Reply(msg, pid); return;
        }
        // cannot truncate a directory
        if (file_in.type == INODE_DIRECTORY) {
            msg->arg1 = ERROR; Reply(msg, pid); return;
        }
        // truncate: free blocks, set size=0; reuse count does NOT change per spec
        inode_free_blocks(&file_in);
        if (save_inode(existing, &file_in) == ERROR) {
            msg->arg1 = ERROR; Reply(msg, pid); return;
        }
        file_inum = existing;

    } else {
        // allocate a fresh inode
        file_inum = alloc_inode_num();
        if (file_inum == ERROR) {
            msg->arg1 = ERROR; Reply(msg, pid); return;
        }

        memset(&file_in, 0, sizeof(file_in));
        file_in.type  = INODE_REGULAR;
        file_in.nlink = 1;
        file_in.reuse = 1;   // first allocation — increment from 0
        file_in.size  = 0;

        if (save_inode(file_inum, &file_in) == ERROR) {
            free_inode_num(file_inum);
            msg->arg1 = ERROR; Reply(msg, pid); return;
        }

        if (dir_add_entry(dir_inum, file_inum, name) == ERROR) {
            // roll back inode allocation
            file_in.type = INODE_FREE;
            save_inode(file_inum, &file_in);
            free_inode_num(file_inum);
            msg->arg1 = ERROR; Reply(msg, pid); return;
        }
    }

    msg->arg1 = 0;
    msg->arg2 = file_inum;
    msg->arg3 = file_in.reuse;
    Reply(msg, pid);
}

static void handle_write(int pid, struct yfs_msg *msg) {
    int inum      = msg->arg1;
    int reuse     = msg->arg2;
    int offset    = msg->arg3;
    int size      = (int)(long)msg->ptr2;
    void *clibuf  = msg->ptr1;

    // Load and verify inode
    struct inode in;
    if (load_inode(inum, &in) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }
    if (in.reuse != reuse) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }
    if (in.type == INODE_DIRECTORY) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }
    if (size <= 0) {
        msg->arg1 = 0; Reply(msg, pid); return;
    }

    // Clamp to maximum possible file size
    int max_bytes = (NUM_DIRECT + BLOCKSIZE / (int)sizeof(int)) * BLOCKSIZE;
    if (offset >= max_bytes) {
        msg->arg1 = 0; Reply(msg, pid); return;
    }
    if (offset + size > max_bytes) size = max_bytes - offset;

    // Pull the write data out of the client's address space
    char *data = malloc(size);
    if (!data) { msg->arg1 = ERROR; Reply(msg, pid); return; }
    if (CopyFrom(pid, data, clibuf, size) == ERROR) {
        free(data); msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    int bytes_written = 0;
    char block_buf[BLOCKSIZE];

    while (bytes_written < size) {
        int cur_off  = offset + bytes_written;
        int blk_idx  = cur_off / BLOCKSIZE;
        int blk_off  = cur_off % BLOCKSIZE;            // byte offset within the block
        int to_write = BLOCKSIZE - blk_off;            // space left in this block
        if (to_write > size - bytes_written)
            to_write = size - bytes_written;           // don't exceed requested size

        // Partial write: must read the existing block first so we don't
        // clobber the bytes we aren't touching (read-modify-write).
        if (blk_off != 0 || to_write != BLOCKSIZE) {
            if (inode_read_block(&in, blk_idx, block_buf) == ERROR) break;
        }

        memcpy(block_buf + blk_off, data + bytes_written, to_write);

        if (inode_write_block(&in, blk_idx, block_buf) == ERROR) break;

        bytes_written += to_write;
    }

    free(data);

    // Extend the file's size if we wrote past the old EOF
    if (offset + bytes_written > in.size)
        in.size = offset + bytes_written;

    if (save_inode(inum, &in) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    msg->arg1 = bytes_written;
    Reply(msg, pid);
}

static void handle_link(int pid, struct yfs_msg *msg) {
    char oldbuf[MAXPATHNAMELEN];
    char newbuf[MAXPATHNAMELEN];

    // Pull both pathnames out of the client's address space
    // ptr1 = oldname, ptr2 = newname, arg1 = cwd_inum (set by iolib)
    if (CopyFrom(pid, oldbuf, msg->ptr1, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }
    if (CopyFrom(pid, newbuf, msg->ptr2, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    int cwd_inum = msg->arg1;

    // Resolve oldname -> must exist, symlinks followed
    int old_inum = lookup_path(oldbuf, cwd_inum, 0, 1);
    if (old_inum == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Verify oldname is not a directory
    struct inode old_in;
    if (load_inode(old_inum, &old_in) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }
    if (old_in.type == INODE_DIRECTORY) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Resolve newname's parent directory + extract the final name 
    // lookup_parent splits "a/b/c" into parent="a/b" and name="c",
    // then calls lookup_path on the parent (follow_last=1).
    // also verifies the parent is actually a directory.
    int parent_inum;
    char name[DIRNAMELEN + 1];
    if (lookup_parent(newbuf, cwd_inum, &parent_inum, name) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Verify newname does not already exist in that directory 
    // dir_lookup returns ERROR when the name is absent, which is what we want.
    if (dir_lookup(parent_inum, name, strlen(name)) != ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Insert the new directory entry pointing at old_inum
    // dir_add_entry reuses a free slot (inum==0) if one exists, otherwise
    // appends and grows the directory's size field.
    if (dir_add_entry(parent_inum, old_inum, name) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Increment nlink on the inode and persist it
    // nlink counts every directory entry across the whole filesystem that
    // contains this inode number — we just added one, so bump it.
    // We reload old_in here in case dir_add_entry modified the same inode
    // block on disk (unlikely for a regular file, but defensively correct).
    if (load_inode(old_inum, &old_in) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }
    old_in.nlink++;
    if (save_inode(old_inum, &old_in) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    msg->arg1 = 0; // success
    Reply(msg, pid);
}

static void handle_unlink(int pid, struct yfs_msg *msg) {
    char pathbuf[MAXPATHNAMELEN];

    if (CopyFrom(pid, pathbuf, msg->ptr1, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    int cwd_inum = msg->arg1;

    // Resolve parent dir and final component name
    int parent_inum;
    char name[DIRNAMELEN + 1];
    if (lookup_parent(pathbuf, cwd_inum, &parent_inum, name) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Reject "." and ".." explicitly
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Find the target inode number in the parent directory
    int target_inum = dir_lookup(parent_inum, name, strlen(name));
    if (target_inum == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Load target inode and verify it is not a directory
    // The spec explicitly forbids Unlink on directories — use RmDir for that.
    struct inode target_in;
    if (load_inode(target_inum, &target_in) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }
    if (target_in.type == INODE_DIRECTORY) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Remove the directory entry from the parent 
    // This zeroes out the inum field of the matching dir_entry, making that
    // slot available for reuse. It does NOT touch the inode itself yet.
    if (dir_remove_entry(parent_inum, name) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Decrement nlink
    target_in.nlink--;

    if (target_in.nlink == 0) {
        //  Last link gone — free everything
        inode_free_blocks(&target_in);
        target_in.type = INODE_FREE;

        if (save_inode(target_inum, &target_in) == ERROR) {
            msg->arg1 = ERROR; Reply(msg, pid); return;
        }

        // Return the inode number to the free inode list so it can be
        // allocated again for a future Create or MkDir.
        free_inode_num(target_inum);

    } else {
        // Other links still exist
        if (save_inode(target_inum, &target_in) == ERROR) {
            msg->arg1 = ERROR; Reply(msg, pid); return;
        }
    }

    msg->arg1 = 0;
    Reply(msg, pid);
}

static void handle_symlink(int pid, struct yfs_msg *msg) {
    char oldbuf[MAXPATHNAMELEN];
    char newbuf[MAXPATHNAMELEN];

    // ptr1 = oldname (the target), ptr2 = newname (the symlink to create)
    if (CopyFrom(pid, oldbuf, msg->ptr1, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }
    if (CopyFrom(pid, newbuf, msg->ptr2, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    int cwd_inum = msg->arg1;

    //  Validate the target string
    int targetlen = strlen(oldbuf);
    if (targetlen == 0) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Resolve newname's parent and extract the final component
    int parent_inum;
    char name[DIRNAMELEN + 1];
    if (lookup_parent(newbuf, cwd_inum, &parent_inum, name) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Verify newname does not already exist
    if (dir_lookup(parent_inum, name, strlen(name)) != ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Allocate a fresh inode for the symlink 
    int new_inum = alloc_inode_num();
    if (new_inum == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Write the target string into the symlink's data blocks
    struct inode sym_in;
    memset(&sym_in, 0, sizeof(sym_in));
    sym_in.type  = INODE_SYMLINK;
    sym_in.nlink = 1;
    sym_in.reuse = 1;
    sym_in.size  = 0;

    // Write the target string block by block. Since MAXPATHNAMELEN < BLOCKSIZE
    int bytes_written = 0;
    char block_buf[BLOCKSIZE];

    while (bytes_written < targetlen) {
        int blk_idx = bytes_written / BLOCKSIZE;
        int blk_off = bytes_written % BLOCKSIZE;
        int to_copy = BLOCKSIZE - blk_off;
        if (to_copy > targetlen - bytes_written)
            to_copy = targetlen - bytes_written;

        // Read-modify-write for partial blocks (only matters if string
        // somehow spans multiple blocks, which MAXPATHNAMELEN makes unlikely)
        if (blk_off != 0) {
            if (inode_read_block(&sym_in, blk_idx, block_buf) == ERROR) {
                free_inode_num(new_inum);
                msg->arg1 = ERROR; Reply(msg, pid); return;
            }
        } else {
            memset(block_buf, 0, BLOCKSIZE);
        }

        memcpy(block_buf + blk_off, oldbuf + bytes_written, to_copy);

        if (inode_write_block(&sym_in, blk_idx, block_buf) == ERROR) {
            free_inode_num(new_inum);
            msg->arg1 = ERROR; Reply(msg, pid); return;
        }

        bytes_written += to_copy;
    }

    sym_in.size = targetlen;

    // Save the symlink inode
    if (save_inode(new_inum, &sym_in) == ERROR) {
        inode_free_blocks(&sym_in);
        free_inode_num(new_inum);
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Add the directory entry for newname
    if (dir_add_entry(parent_inum, new_inum, name) == ERROR) {
        inode_free_blocks(&sym_in);
        sym_in.type = INODE_FREE;
        save_inode(new_inum, &sym_in);
        free_inode_num(new_inum);
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    msg->arg1 = 0;
    Reply(msg, pid);
}

static void handle_readlink(int pid, struct yfs_msg *msg) {
    char pathbuf[MAXPATHNAMELEN];

    // ptr1 = pathname of the symlink, ptr2 = client buf to write target into
    // arg2 = len (max bytes to copy back)
    if (CopyFrom(pid, pathbuf, msg->ptr1, MAXPATHNAMELEN) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    int cwd_inum = msg->arg1;
    int len      = msg->arg2;

    if (len <= 0) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Resolve the pathname with follow_last=0 
    int sym_inum = lookup_path(pathbuf, cwd_inum, 0, 0);
    if (sym_inum == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Verify the inode is actually a symlink
    struct inode sym_in;
    if (load_inode(sym_inum, &sym_in) == ERROR) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }
    if (sym_in.type != INODE_SYMLINK) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Read the target string from the symlink's data blocks
    int targetlen = sym_in.size;
    int to_return = (targetlen < len) ? targetlen : len;  // min(targetlen, len)

    char *tmpbuf = malloc(to_return);
    if (!tmpbuf) {
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    if (inode_read_bytes(&sym_in, 0, tmpbuf, to_return) == ERROR) {
        free(tmpbuf);
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    // Copy the result back into the client's buffer 
    if (CopyTo(pid, msg->ptr2, tmpbuf, to_return) == ERROR) {
        free(tmpbuf);
        msg->arg1 = ERROR; Reply(msg, pid); return;
    }

    free(tmpbuf);

    // Return value is the number of characters placed in buf (capped at len)
    msg->arg1 = to_return;
    Reply(msg, pid);
}