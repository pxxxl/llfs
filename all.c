#define FUSE_USE_VERSION 30


#define DEBUG
#define FS_OUTPUT
#define MAX_DIR_HEIGHT 512
#define MAX_DIR_NAME_LENGTH 512
#define MAX_CHILDREN_NODE 512
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <fuse.h>
#include <wait.h>


#ifdef DEBUG
#define DEBUG_PRINT(...) do{ fprintf( stderr, __VA_ARGS__ ); } while( 0 )
#else
#define DEBUG_PRINT(...) do{ } while ( 0 )
#endif

#ifdef FS_OUTPUT
#define FS_PRINT(...) do{ fprintf(stdout, __VA_ARGS__); }while( 0 )
#else
#define FS_PRINT(...) do{ } while( 0 )
#endif

struct dirent * Readdir(DIR * dir);
DIR* Opendir(const char* path);
int Closedir(DIR* dirp);
int Open(char* filename, int flags, mode_t mode);
int Close(int fd);
ssize_t Read(int fd, void* buffer, size_t size);


enum node_type{
  folder,
  file,
  mask
};

typedef struct dir_node{
    enum node_type    type;
    char*             name;
    char*             path;
    struct dir_node*  lower;
    struct dir_node*  next;
    struct dir_node*  last;
    struct dir_node*  root;
    unsigned          num_children;
    struct dir_node** children;
}dir_node_t, *dir_node_p, *const dir_node_cp;

dir_node_p init(const char* upper_path, const char** lower_paths, unsigned lower_path_num);
dir_node_p access_within_layer (const dir_node_t* root, const char* path);
dir_node_p access_cross_layer (const dir_node_t* root, const char* path);
dir_node_p insert_node(const dir_node_t* root, const char* dir_path, const char* name, enum node_type type);
int delete_node(const dir_node_t* root, const char* dir_path, const char* name);
int has_the_same_filename(const dir_node_t* root, const char* dir_path, const char* name);
dir_node_p modify_access(const dir_node_t* root, const char* path);
int rename_node(const dir_node_t* root, const char* dir, const char* name, const char* newname);
int move_node(const dir_node_t* root, const char* name, const char* dir, const char* newdir);
int in_upper_layer(const dir_node_t* root, const char* path);
dir_node_p get_merged_dir(const dir_node_t* root, const char* dir);
void print_dir(const dir_node_t* root, dir_node_p dir);

dir_node_p dir_tree = NULL;

static int has_sub_node(dir_node_p node){
    if(node->type == file || node->num_children == 0){
        return 0;
    }
    return 1;
}
//link the "next" pointer across the folder
static void link_pointer_across_folders(dir_node_p root){
    if(root == NULL || root->num_children == 0){
        return;
    }

    int count = 0;
    int debug_line = 0;
    dir_node_p head = root;

    //get the count of the "has_node" folders
    while(head != NULL){
        dir_node_p cur   = head;
        dir_node_p saver = head;
        head = NULL;
        while(cur != NULL){
            if(has_sub_node(cur)){
                count++;
                if(count == 1){
                    head = cur->children[0];
                }
            }
            cur = cur->next;
        }
        dir_node_p* folders = (dir_node_p*)calloc(count, sizeof(dir_node_p));
        DEBUG_PRINT("link folder... height %d, folders to link count %d\n", debug_line, count);
        debug_line++;

        cur = saver;
        count = 0;
        while(cur != NULL){
            if(has_sub_node(cur)){
                folders[count] = cur;
                count++;
            }
            cur = cur->next;
        }

        for(int i = 0; i < count - 1; i++){
            dir_node_p pri = folders[i + 1];
            dir_node_p pre = folders[i];
            pre->children[pre->num_children - 1]->next = pri->children[0];
            pri->children[0]->last = pre->children[pre->num_children - 1];
            DEBUG_PRINT("link folder... set %s next-> %s\n", pre->children[pre->num_children - 1]->name, pri->children[0]->name);
        }

        count = 0;
        free(folders);
    }
}
//use allocated node and the DIR* which represent the node, build children of the node recursively
static void build_primary_dir_tree_helper(dir_node_p node, DIR* root, dir_node_p root_dir){
    if(root == NULL){
        return;
    }

    //initialize linked list utilities
    struct dirent* ent;
    dir_node_p dumb_head = (dir_node_p)malloc(sizeof(dir_node_t));
    dir_node_p cursor    = dumb_head;
    dir_node_p prior     = NULL;

    node->num_children = 0;

    //content check loop
    while((ent = readdir(root)) != NULL) {
        if (ent->d_type != DT_DIR) {
            //the ent is a file
            prior = (dir_node_p) malloc(sizeof(dir_node_t));
            prior->name = (char *) malloc(sizeof(char) * MAX_DIR_NAME_LENGTH);
            memcpy(prior->name, ent->d_name, MAX_DIR_NAME_LENGTH);
            prior->path = (char*) malloc(sizeof (char) * MAX_DIR_NAME_LENGTH);
            sprintf(prior->path, "%s%s", node->path, prior->name);
            prior->type = file;
            cursor->next = prior;
            prior->last = cursor;
            prior->root = root_dir;
            cursor = prior;
            node->num_children++;
            DEBUG_PRINT("build primary tree... parent: %s, file: %s\n", node->path, prior->name);
        } else {
            //the ent is a folder
            if (strncmp(ent->d_name, ".", 1) == 0) {
                continue;
            } else {
                //handle the node before dir and the dir itself
                prior = (dir_node_p) malloc(sizeof(dir_node_t));
                prior->name = (char *) malloc(sizeof(char) * MAX_DIR_NAME_LENGTH);
                memcpy(prior->name, ent->d_name, MAX_DIR_NAME_LENGTH);
                prior->path = (char*) malloc(sizeof (char) * MAX_DIR_NAME_LENGTH);
                sprintf(prior->path, "%s%s/", node->path, prior->name);
                prior->type = folder;
                cursor->next = prior;
                prior->last = cursor;
                prior->root = root_dir;
                cursor = prior;
                node->num_children++;
                DEBUG_PRINT("build primary tree... parent: %s, folder: %s\n", node->path, prior->name);

                //handle children of the dir
                char dir_name_buffer[MAX_DIR_NAME_LENGTH];
                sprintf(dir_name_buffer, "%s%s/", node->path, prior->name);
                DIR *this_dir = Opendir(dir_name_buffer);
                build_primary_dir_tree_helper(prior, this_dir, root_dir);
                Closedir(this_dir);
            }
        }
    }
    if(prior != NULL)
        prior->next = NULL;
    if(dumb_head->next != NULL)
        dumb_head->next->last = NULL;

    //now attach the children on the node
    node->children = (dir_node_p*)malloc(sizeof(dir_node_t) * node->num_children);
    cursor = dumb_head->next;
    for(int i = 0; i < node->num_children; i++){
        node->children[i] = cursor;
        DEBUG_PRINT("build primary tree... attach %s to %s\n", cursor->name, node->path);
        cursor = cursor->next;
    }

    free(dumb_head);
}
//build dir tree, without linking the layers
static dir_node_p build_dir_tree(const char* path){
    DIR* root = Opendir(path);
    dir_node_p node = (dir_node_p)malloc(sizeof(dir_node_t));
    node->next = NULL;
    node->name = "/";
    node->path = (char*)path;
    node->type = folder;
    node->lower= NULL;
    DEBUG_PRINT("build primary tree... root declared: %s\n", path);

    build_primary_dir_tree_helper(node, root, node);
    //link_pointer_across_folders(node);
    return node;
}
//merge 2 folders recursively, without checking the root dir name. Parameters are dir-trees
static void merge_2_folders(dir_node_p upper, dir_node_p lower){
    upper->lower = lower;
    DEBUG_PRINT("merge folder... %s, %s\n", upper->path, lower->path);
    for(int i = 0; i < upper->num_children; i++){
        for(int j = 0; j < lower->num_children; j++){
            dir_node_p up  = upper->children[i];
            dir_node_p low = lower->children[j];
            if(strcmp(up->name, low->name) == 0 && up->type == low->type){
                up->lower = low;
                if(up->type == folder){
                    merge_2_folders(up, low);
                }else{
                    DEBUG_PRINT("merge file... %s, %s\n", up->path, low->path);
                }
            }
        }
    }
}
//merge the different layers
static void merge_layers(dir_node_p* layers, unsigned num_layers){
    for(int i = 0; i < num_layers - 1; i++){
        merge_2_folders(layers[i], layers[i+1]);
        DEBUG_PRINT("complete merge of %s, %s\n\n", layers[i]->path, layers[i+1]->path);
    }
}

//static void pout(char c, int times){for(int i = 0; i< times; i++)fprintf(stdout, "%c", c);}
//static void perr(char c, int times){for(int i = 0; i< times; i++)fprintf(stderr, "%c", c);}

dir_node_p init(const char* upper_path, const char** lower_paths, unsigned lower_path_num){
    char** dirs = (char**)calloc(lower_path_num + 1, sizeof(char*));
    dir_node_p* dir_trees = (dir_node_p*)calloc(lower_path_num + 1, sizeof(dir_node_p));

    dirs[0] = (char*)upper_path;
    for(int i = 1; i <= lower_path_num; i++){
        dirs[i] = (char*)lower_paths[i - 1];
    }

    for(int i = 0; i < lower_path_num + 1; i++){
        dir_trees[i] = build_dir_tree(dirs[i]);
        DEBUG_PRINT("\n");
    }

    merge_layers(dir_trees, lower_path_num + 1);
    DEBUG_PRINT("init complete\n");
    return dir_trees[0];
}
//do not access the mask node
dir_node_p access_within_layer (const dir_node_t* root, const char* path){
    char source[MAX_DIR_HEIGHT][MAX_DIR_NAME_LENGTH];
    char buffer[MAX_DIR_HEIGHT][MAX_DIR_NAME_LENGTH];
    strcpy(source[0], path);
    int count = 0;
    do{
        strcpy(buffer[count], basename(source[count]));
        strcpy(source[count + 1], dirname(source[count]));
        count++;
    }while(strcmp(buffer[count - 1], "/") != 0);

    const dir_node_t* cursor = root;
    for(int i = count - 2; i >= 0; i--){
        int found = 0;
        for(int j = 0; j < cursor->num_children; j++){
            dir_node_p current = cursor->children[j];
            if(strcmp(current->name, buffer[i]) == 0 && (current->type == folder || i == 0)){
                cursor = current;
                found = 1;
                break;
            }
        }
        if(!found){
            DEBUG_PRINT("access within layer: not found %s\n", path);
            return NULL;
        }
    }
    DEBUG_PRINT("access within layer: %s\n", path);
    return (dir_node_p)cursor;
}
dir_node_p access_cross_layer (const dir_node_t* root, const char* path){
    for(dir_node_p root_cur = (dir_node_p)root; root_cur != NULL; root_cur = root_cur->lower){
        dir_node_p p = access_within_layer(root_cur, path);
        if(p == NULL){
            continue;
        }else{
            DEBUG_PRINT("access across layer: %s\n", path);
            return p;
        }
    }
    DEBUG_PRINT("access across layer: not found %s\n", path);
    return NULL;
}

//1. dir_path must be accessible at the upper layer
dir_node_p insert_node(const dir_node_t* root, const char* dir_path, const char* name, enum node_type type){
    dir_node_p fold = access_within_layer(root, dir_path);
    if(fold == NULL || has_the_same_filename(root, dir_path, name)){
        DEBUG_PRINT("insert node: failed, dir path is %s\n", dir_path);
        return NULL;
    }
    for(int i = 0; i < fold->num_children; i++){
        dir_node_p p = fold->children[i];
        if(strcmp(name, p->name) == 0){
            p->num_children = 0;
            p->children = NULL;
            p->type = type;
            return p;
        }
    }
    fold->num_children++;
    if(fold->num_children == 1){
        fold->children = (dir_node_p*) calloc(fold->num_children, sizeof (dir_node_p));
    }else{
        fold->children = (dir_node_p*) reallocarray(fold->children, fold->num_children, sizeof (dir_node_p));
    }
    dir_node_p target = (dir_node_p) malloc(sizeof (dir_node_t));
    target->num_children = 0;
    target->children = NULL;
    target->type = type;
    target->name = (char *) malloc(sizeof(char) * MAX_DIR_NAME_LENGTH);
    memcpy(target->name, name, MAX_DIR_NAME_LENGTH);
    target->path = (char*) malloc(sizeof (char) * MAX_DIR_NAME_LENGTH);
    target->root = fold->root;
    sprintf(target->path, "%s%s", fold->path, name);

    fold->children[fold->num_children - 1] = target;

    DEBUG_PRINT("insert node: %s, name is %s\n", dir_path, name);
    return target;
}
int delete_node(const dir_node_t* root, const char* dir_path, const char* name){
    char buffer[MAX_DIR_NAME_LENGTH];
    sprintf(buffer, "%s/%s", dir_path, name);
    DEBUG_PRINT("delete node: node path is %s\n", buffer);
    dir_node_p real_node = access_cross_layer(root, buffer);
    dir_node_p upper_node = access_within_layer(root, buffer);

    if(real_node == upper_node){
        real_node->type = mask;
        for(int i = 0; i < real_node->num_children; i++){
            free(real_node->children[i]);
        }
        free(real_node->children);
        real_node->children = NULL;
        real_node->num_children = 0;
        DEBUG_PRINT("delete node: in upper layer %s, name is %s\n", dir_path, name);
        return 0;
    }else{
        dir_node_p p = modify_access(root, buffer);
        p->type = mask;
        DEBUG_PRINT("delete node: in lower layers %s, name is %s\n", dir_path, name);
        return 1;
    }
}
int has_the_same_filename(const dir_node_t* root, const char* dir_path, const char* name){
    dir_node_p p = access_cross_layer(root, dir_path);
    for(int i = 0; i < p->num_children; i++){
        if(strcmp(name, p->children[i]->name) == 0 && p->children[i]->type != mask){
            DEBUG_PRINT("has the same file name: dir %s, name is %s\n", dir_path, name);
            return 1;
        }
    }
    return 0;
}
dir_node_p modify_access(const dir_node_t* root, const char* path){
    dir_node_p up_p = access_within_layer(root, path);
    dir_node_p low_p = access_cross_layer(root, path);
    if(up_p == low_p){
        DEBUG_PRINT("modify access: in upper layer %s\n", path);
        return up_p;
    }else{
        char buf1[MAX_DIR_NAME_LENGTH];
        char buf2[MAX_DIR_NAME_LENGTH];
        strcpy(buf1, path);
        strcpy(buf2, path);
        char* dir = dirname(buf1);
        char* bas = basename(buf2);
        insert_node(root, dir, bas, low_p->type);
        dir_node_p new_p = access_within_layer(root, path);
        DEBUG_PRINT("modify access: in lower layer %s\n", path);
        return new_p;
    }
}
int rename_node(const dir_node_t* root, const char* dir, const char* name, const char* newname){
    if(has_the_same_filename(root, dir, newname)){
        DEBUG_PRINT("rename node: error, has same file name in dir %s, new name is %s\n", dir, newname);
        return -1;
    }
    char path[MAX_DIR_NAME_LENGTH];
    sprintf(path, "%s%s", dir, name);
    if(!in_upper_layer(root, path)){
        DEBUG_PRINT("rename node: error, not allowed to rename a lower node, dir %s, name is %s, new name is %s\n",dir, name, newname);
    }
    dir_node_p p = modify_access(root, path);
    if(p == NULL){
        return -1;
    }
    dir_node_p dir_node = access_cross_layer(root, dir);
    strcpy(p->name, newname);
    sprintf(p->path, "%s%s", dir_node->path, newname);
    delete_node(root, dir, name);
    DEBUG_PRINT("rename node: dir %s, new name is %s\n", dir, newname);
    return 0;
}
int move_node(const dir_node_t* root, const char* name, const char* dir, const char* newdir){
    if(has_the_same_filename(root, newdir, name)){
        DEBUG_PRINT("move node: failed, has same file name in new dir, dir is %s, newdir is %s\n", dir, newdir);
        return -1;
    }
    char path[MAX_DIR_NAME_LENGTH];
    sprintf(path, "%s%s", dir, name);
    dir_node_p p = access_within_layer(root, path);
    if(in_upper_layer(root, dir) && in_upper_layer(root, newdir) && p != NULL){
        insert_node(root, newdir, name, p->type);
        dir_node_p newnode = access_cross_layer(root, path);
        newnode->children = p->children;
        newnode->num_children = p->num_children;
        p->children = (dir_node_p*) calloc(p->num_children, sizeof (dir_node_p));
        for(int i = 0; i < p->num_children; i++){
            p->children[i] = (dir_node_p)malloc(sizeof (dir_node_t));
        }
        delete_node(root, dir, name);
        DEBUG_PRINT("move node: ddir is %s, newdir is %s, name is %s\n", dir, newdir, name);
        return 0;
    }else{
        DEBUG_PRINT("move node: failed, dst or src node are not in upper layer, dir is %s, newdir is %s\n", dir, newdir);
        return -1;
    }

}
int in_upper_layer(const dir_node_t* root, const char* path){
    dir_node_p up_p = access_within_layer(root, path);
    dir_node_p low_p = access_cross_layer(root, path);
    return up_p == low_p;
}
dir_node_p get_merged_dir(const dir_node_t* root, const char* dir){
    DEBUG_PRINT("get merged dir : dir is %s\n", dir);
    dir_node_p parent = (dir_node_p)malloc(sizeof (dir_node_t));
    parent->children = (dir_node_p*)calloc(sizeof (dir_node_p) , MAX_CHILDREN_NODE);
    size_t i = 0;
    const dir_node_t* cur = root;
    for(;cur!=NULL;cur = cur->lower){
        dir_node_p dirnode = access_within_layer(cur, dir);
        if(dirnode == NULL){
            continue;
        }
        for(int j = 0; j < dirnode->num_children; j++){
            int same = 0;
            for(int l = 0; l < i; l++){
                if(strcmp(parent->children[l]->name, dirnode->children[j]->name) == 0){
                    same = 1;
                    break;
                }
            }
            if(same){
                continue;
            }else{
                parent->children[i] = dirnode->children[j];
                i++;
            }
        }
    }
    parent->num_children = i;
    return parent;
}
void print_dir(const dir_node_t* root, dir_node_p dir){
    for(int i = 0; i < dir->num_children; i++){
        if(dir->children[i]->type != mask)
            fprintf(stdout, "%s ", dir->children[i]->name);
    }
    fprintf(stdout, "\n");
}


struct dirent* Readdir(DIR* dir){
    struct dirent* result = readdir(dir);
    if(result == NULL){
        write(STDERR_FILENO, "error : Readdir\n", 17);
        exit(1);
    }
    return result;
}

DIR* Opendir(const char* path){
    DIR* result = opendir(path);
    if(result == NULL){
        write(STDERR_FILENO, "error : Opendir\n", 17);
        write(STDERR_FILENO, path, 65535);
        exit(1);
    }
    return result;
}

int Closedir(DIR* dirp){
    int result = closedir(dirp);
    if(result == -1){
        write(STDERR_FILENO, "error : Closedir\n", 18);
        exit(1);
    }
    return result;
}

int Open(char* filename, int flags, mode_t mode){
    int fd = open(filename, flags, mode);
    if(fd == -1){
        char error_description[512];
        sprintf(error_description, "error : Open , %s", filename);
        write(STDERR_FILENO, error_description, strlen(error_description));
        exit(1);
    }
    return fd;
}

int Close(int fd){
    int fd_n = close(fd);
    if(fd == -1){
        char* error_description = "error : Close";
        write(STDERR_FILENO, error_description, strlen(error_description));
        exit(1);
    }
    return fd_n;
}

ssize_t Read(int fd, void* buffer, size_t size){
    ssize_t s = read(fd, buffer, size);
    if(s == -1){
        char* error_description = "error : Read";
        write(STDERR_FILENO, error_description, strlen(error_description));
        exit(1);
    }
    return s;
}



















static int do_getattr( const char *path, struct stat *st ){
	DEBUG_PRINT("getattr called: path=%s\n", path);
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    if(cursor == NULL){
    	DEBUG_PRINT("getattr: cursor == NULL\n");
        errno = ENOENT;
    	return -ENOENT;
    }
    int stat = lstat(cursor->path, st);
    DEBUG_PRINT("getattr: stat=%d, file actual path is %s\n", stat, cursor->path);
    return stat;
}
static int do_readlink(const char *path, char *link, size_t size)
{
	DEBUG_PRINT("readlink called: path=%s, link=%s", path, link);
    size_t stat;
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    stat = readlink(cursor->path, link, size - 1);
    if (stat >= 0) {
        link[stat] = '\0';
        stat = 0;
    }

    return (int)stat;
}
static int do_mknod(const char *path, mode_t mode, dev_t dev){
	DEBUG_PRINT("mknod called: path=%s\n", path);
    int stat;
    char buffer[MAX_DIR_NAME_LENGTH];
    char buffer2[MAX_DIR_NAME_LENGTH];
    strcpy(buffer, path);
    strcpy(buffer2, path);
    char* dir = dirname(buffer);
    char* bas = basename(buffer2);
    if(has_the_same_filename(dir_tree, dir, bas)){
        DEBUG_PRINT("mknod: cannot mknod at %s, may because has the same file name\n", path);
        return 0;
    }
    dir_node_p cursor = insert_node(dir_tree, dir, bas, file);
    if(cursor == NULL){
        DEBUG_PRINT("mknod: cannot mknod at %s, may because in lower layer\n", path);
        return 0;
    }

    if (S_ISREG(mode)) {
        stat = open(cursor->path, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (stat >= 0)
            stat = close(stat);
    } else{
        if (S_ISFIFO(mode))
            stat = mkfifo(cursor->path, mode);
        else
            stat = mknod(cursor->path, mode, dev);
    }
    return stat;
}
static int do_mkdir(const char *path, mode_t mode)
{
    //char buf[256];
    //mkdir("/home/cozard/y/")
	DEBUG_PRINT("mkdir called: path=%s\n", path);
    char buffer[MAX_DIR_NAME_LENGTH];
    char buffer2[MAX_DIR_NAME_LENGTH];
    strcpy(buffer, path);
    strcpy(buffer2, path);
    char* dir = dirname(buffer);
    char* bas = basename(buffer2);
    if(has_the_same_filename(dir_tree, dir, bas)){
        DEBUG_PRINT("mkdir: cannot mkdir at %s, may because has the same file name\n", path);
        return 0;
    }

    dir_node_p cursor = insert_node(dir_tree, dir, bas, folder);
    if(cursor == NULL){
        DEBUG_PRINT("mkdir: cannot mkdir at %s, may because in lower layer\n", path);
        return 0;
    }
    DEBUG_PRINT("mkdir at %s\n", cursor->path);
    return mkdir(cursor->path, mode);
}
static int do_unlink(const char *path)
{
	DEBUG_PRINT("unlink called: path=%s\n", path);
    char buffer[MAX_DIR_NAME_LENGTH];
    char buffer2[MAX_DIR_NAME_LENGTH];
    strcpy(buffer, path);
    strcpy(buffer2, path);
    char* dir = dirname(buffer);
    char* bas = basename(buffer2);
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    if(cursor == NULL){
        DEBUG_PRINT("unlink: cannot unlink at %s, invalid\n", path);
        return -ENOENT;
    }
    int res = delete_node(dir_tree, dir, bas);
    if(res != 0){
        DEBUG_PRINT("unlink: didn't actually delete the lower node, actual path = %s, hide it\n", cursor->path);
        return 0;
    }
    DEBUG_PRINT("unlink: delete the node, actual path = %s\n", cursor->path);
    return unlink(cursor->path);
}
static int do_rmdir(const char *path)
{
	DEBUG_PRINT("rmdir called: path=%s", path);
    char buffer[MAX_DIR_NAME_LENGTH];
    char buffer2[MAX_DIR_NAME_LENGTH];
    strcpy(buffer, path);
    strcpy(buffer2, path);
    char* dir = dirname(buffer);
    char* bas = basename(buffer2);
    int need = !delete_node(dir_tree, dir, bas);
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    if(need){
        return rmdir(cursor->path);
    }
    return 0;
}
static int do_symlink(const char *path, const char *link)
{
	DEBUG_PRINT("symlink called: path=%s, link=%s", path, link);
    char buffer[MAX_DIR_NAME_LENGTH];
    char buffer2[MAX_DIR_NAME_LENGTH];
    strcpy(buffer, link);
    strcpy(buffer2, link);
    char* dir = dirname(buffer);
    char* bas = basename(buffer2);
    dir_node_p res = insert_node(dir_tree, dir, bas, file);
    if(res == NULL){
        return 0;
    }
    dir_node_p cursor = access_cross_layer(dir_tree, link);
    return symlink(path, cursor->path);
}
static int do_rename(const char *path, const char *newpath)
{
	DEBUG_PRINT("rename called: path=%s, newpath=%s", path, newpath);
    dir_node_p old = access_cross_layer(dir_tree, path);
    char buffer1[MAX_DIR_NAME_LENGTH];
    char buffer2[MAX_DIR_NAME_LENGTH];
    strcpy(buffer1, path);
    strcpy(buffer2, path);
    char* dir = dirname(buffer1);
    char* bas = basename(buffer2);
    char buffer3[MAX_DIR_NAME_LENGTH];
    char buffer4[MAX_DIR_NAME_LENGTH];
    strcpy(buffer3, newpath);
    strcpy(buffer4, newpath);
    char* ndir = dirname(buffer3);
    char* nbas = basename(buffer4);
    int success = 1;
    if(strcmp(nbas, bas) != 0){
        if(rename_node(dir_tree, dir, bas, nbas) == -1){
            success = 0;
        }
    }
    if(strcmp(ndir, dir) != 0){
        if(move_node(dir_tree, bas ,dir, ndir) == -1){
            success = 0;
        }
    }
    if(success && old != NULL){
        dir_node_p p = access_cross_layer(dir_tree, newpath);
        return rename(old->path, p->path);
    }
    return 0;
}
int do_link(const char *path, const char *newpath)
{
	DEBUG_PRINT("link called: path=%s, newpath=%s", path, newpath);
    char buffer[MAX_DIR_NAME_LENGTH];
    char buffer2[MAX_DIR_NAME_LENGTH];
    strcpy(buffer, newpath);
    strcpy(buffer2, newpath);
    char* dir = dirname(buffer);
    char* bas = basename(buffer2);
    dir_node_p res = insert_node(dir_tree, dir, bas, file);
    if(res == NULL){
        return 0;
    }
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    dir_node_p newcur = access_cross_layer(dir_tree, newpath);
    if(cursor != NULL && newcur != NULL){
        return link(cursor->path, newcur->path);
    }
    return 0;
}
static int do_chmod(const char *path, mode_t mode){
	DEBUG_PRINT("chmod called: path=%s\n", path);
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    return chmod(cursor->path, mode);
}
static int do_chown(const char *path, uid_t uid, gid_t gid){
	DEBUG_PRINT("chown called: path=%s\n", path);
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    return chown(cursor->path, uid, gid);
}
static int do_truncate(const char *path, off_t newsize)
{
    DEBUG_PRINT("truncate called: path=%s\n", path);
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    DEBUG_PRINT("truncate called: actual path=%s\n", cursor->path);
    return truncate(cursor->path, newsize);
}
static int do_utime(const char *path, struct utimbuf *ubuf)
{
	DEBUG_PRINT("utime called: path=%s\n", path);
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    return utime(cursor->path, ubuf);
}
static int do_open(const char *path, struct fuse_file_info *fi)
{
	DEBUG_PRINT("open called: virtual path=%s\n", path);
    int stat = 0;
    int fd;
    if((fi->flags&O_ACCMODE) == O_RDONLY){
        dir_node_p cursor = access_cross_layer(dir_tree, path);
        if(cursor == NULL){
            DEBUG_PRINT("open: virtual path invalid, path=%s\n", path);
            return -ENOENT;
        }
        DEBUG_PRINT("open: mode is read only, actual path=%s\n", cursor->path);
        fd = open(cursor->path, fi->flags);
    }else{
        dir_node_p old_cur = access_cross_layer(dir_tree, path);
        dir_node_p cursor = modify_access(dir_tree, path);
        if(cursor == NULL){
            DEBUG_PRINT("open: virtual path invalid, path=%s\n", path);
            return -ENOENT;
        }
        if(cursor->root != dir_tree){
                DEBUG_PRINT("open: going to copy the file %s\n", cursor->path);
                pid_t pid;
                if((pid = fork()) == 0){
                    execl("/bin/cp", "-p", old_cur->path, cursor->path, (char *)0);
                    exit(0);
                }else{
                    int a = 0;
                    wait(&a);
                }
        }
        DEBUG_PRINT("open: no need to mknod %s\n", cursor->path);
        DEBUG_PRINT("open: mode is read&write, actual path=%s\n", cursor->path);
        fd = open(cursor->path, fi->flags);
    }
    if (fd < 0){
        stat = -errno;
    }
    fi->fh = fd;
    return 0;
}
static int do_read( const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	DEBUG_PRINT("read called\n");
    return pread(fi->fh, buffer, size, offset);
}
static int do_write( const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	DEBUG_PRINT("write called\n");
    return pwrite(fi->fh, buffer, size, offset);
}
static int do_statfs(const char *path, struct statvfs *statv){
	DEBUG_PRINT("statfs called: path=%s", path);
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    return statvfs(cursor->path, statv);
}
static int do_flush(const char *path, struct fuse_file_info *fi){
	DEBUG_PRINT("flush called: path=%s", path);
    return 0;
}
static int do_release(const char *path, struct fuse_file_info *fi){
	DEBUG_PRINT("release called: path=%s", path);
    return close(fi->fh);
}
static int do_fsync(const char *path, int datasync, struct fuse_file_info *fi){
    DEBUG_PRINT("fsync called: path=%s", path);
    return fsync(fi->fh);
}
static int do_opendir(const char *path, struct fuse_file_info *fi)
{
	DEBUG_PRINT("opendir called: path=%s", path);
    int stat = 0;
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    DIR* dp = opendir(cursor->path);
    if(dp == NULL){
        stat = -errno;
    }
    fi->fh = (intptr_t) dp;
    return stat;
}
static int do_readdir( const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi )
{
	DEBUG_PRINT("readdir called: path=%s", path);
    filler( buffer, ".", NULL, 0 ); // Current Directory
    filler( buffer, "..", NULL, 0 ); // Parent Directory

    dir_node_p cursor = get_merged_dir(dir_tree, path);
    for(int i = 0; i < cursor->num_children; i++){
        if(cursor->children[i]->type != mask){
            filler( buffer, cursor->children[i]->name, NULL, 0);
        }
    }

    return 0;
}
static int do_releasedir(const char *path, struct fuse_file_info *fi)
{
	DEBUG_PRINT("releasedir called: path=%s", path);
    closedir((DIR *) (uintptr_t) fi->fh);
    return 0;
}
static int do_fsyncdir(const char *path, int datasync, struct fuse_file_info *fi)
{
	DEBUG_PRINT("fsyncdir called: path=%s", path);
    return 0;
}
static void *do_init(struct fuse_conn_info *conn)
{
	DEBUG_PRINT("init called");
    return 0;
}
static void do_destroy(void *userdata){
	DEBUG_PRINT("destroy called");
}
static int do_access(const char *path, int mask){
	DEBUG_PRINT("access called: path=%s\n", path);
    int stat = 0;
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    stat = access(cursor->path, mask);
    if (stat < 0)
        stat = -errno;
    return stat;
}
static int do_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi){
    DEBUG_PRINT("ftruncate called: path=%s", path);
    int stat = 0;
    dir_node_p cursor = access_cross_layer(dir_tree, path);
    stat = ftruncate(fi->fh, offset);
    if (stat < 0)
        stat = -errno;
    return stat;
}
static int do_fgetattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{
	DEBUG_PRINT("fgetattr called: path=%s", path);
    int stat = 0;
    if (!strcmp(path, "/"))
        return do_getattr(path, statbuf);

    stat = fstat(fi->fh, statbuf);
    if (stat < 0)
        stat = -errno;
    return stat;
}

struct fuse_operations do_oper = {
        .getattr = do_getattr,
        .readlink = do_readlink,
        .getdir = NULL,
        .mknod = do_mknod,
        .mkdir = do_mkdir,
        .unlink = do_unlink,
        .rmdir = do_rmdir,
        .symlink = do_symlink,
        .rename = do_rename,
        .link = do_link,
        .chmod = do_chmod,
        .chown = do_chown,
        .truncate = do_truncate,
        .utime = do_utime,
        .open = do_open,
        .read = do_read,
        .write = do_write,
        .statfs = do_statfs,
        .flush = do_flush,
        .release = do_release,
        .fsync = do_fsync,
        .opendir = do_opendir,
        .readdir = do_readdir,
        .releasedir = do_releasedir,
        .fsyncdir = do_fsyncdir,
        .init = do_init,
        .destroy = do_destroy,
        .access = do_access,
        .ftruncate = do_ftruncate,
        .fgetattr = do_fgetattr
};

int entrance(int argc, char* argv[], struct fuse_operations* oper){
    if ((getuid() == 0) || (geteuid() == 0)) {
        fprintf(stderr, "Running EZFS as root opens unnacceptable security holes\n");
        return 1;
    }
    fprintf(stderr, "Fuse library version %d.%d\n", FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);
    char** buffer = (char**)malloc(512 * sizeof (char*));
    dir_node_p dir = NULL;
    for(int i = 0; i < argc; i++){
        if(strcmp(argv[i], "-mergestart") == 0){
            int num_layer = 0;
            for(int j = 1; strcmp(argv[i + j], "-mergeend"); j++){
                buffer[j - 1] = (char*)malloc(512 * sizeof (char));
                strcpy(buffer[j - 1], argv[i + j]);
                num_layer = j;
            }
            if(num_layer != 0){
                DEBUG_PRINT("going to call init, upper=%s, num lower layer=%d\n", buffer[0], num_layer-1);
                for(int i = 0; i < num_layer-1;i++){
                    DEBUG_PRINT("(buffer+1)[%d]=%s\n", i, (buffer+1)[i]);
                }

                dir = init(buffer[0], (const char**)buffer + 1, num_layer - 1);
                argc -= num_layer + 2;
            }
            break;
        }
    }
    dir_tree = dir;

    return fuse_main(argc, argv, oper, NULL);
}

int main(int argc, char* argv[]) {
    return entrance(argc, argv, &do_oper);
}
//./fs -f /home/cozard/ms/ -mergestart /home/cozard/y/ /home/cozard/t/ -mergeend

//gcc all.c -o fs `pkg-config fuse --cflags --libs`

//./fs -f /home/cozard/test/mntA/ -mergestart /home/cozard/test/A/ /home/cozard/test/lower1/ /home/cozard/test/lower2/ -mergeend
//./fs -f /home/cozard/test/mntB/ -mergestart /home/cozard/test/B/ /home/cozard/test/lower1/ /home/cozard/test/lower2/ -mergeend
