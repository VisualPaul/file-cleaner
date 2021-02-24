#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_PRINTED 40
#define MIN_PERCENTAGE 5.
#define FILE_TYPE_OFFSET 12
#define DIRECTORY_UNLISTABLE 020

struct file {
    struct file *next;
    struct directory *parent;
    char *name;
    off_t size;
    uint8_t type;
};

struct directory {
    struct file file;
    struct file *subdirs;
    uint8_t subdirs_sorted;
};

char *concat_path(const char *path_a, const char *path_b)
{
    size_t size_a = strlen(path_a);
    size_t size_b = strlen(path_b);
    uint8_t add_slash = (path_a[size_a - 1] != '/');
    char *result = malloc(size_a + add_slash + size_b + 1);
    char *p = stpncpy(result, path_a, size_a + 1);
    if (add_slash) {
        *(p++) = '/';
    }
    strncpy(p, path_b, size_b + 1);
    return result;
}

char *get_file_name(const char *path)
{
    const char *result = path;
    uint8_t prev = 0;
    for (const char *p = path; *p; ++p) {
        if (*p == '/') {
            prev = 1;
        } else {
            if (prev) {
                result = p;
            }
            prev = 0;
        }
    }
    return (char *) result;
}

void deallocate_files(struct file *file)
{
    if (!file) {
        return;
    }
    if (file->type == S_IFDIR >> FILE_TYPE_OFFSET) {
        /* directory is both listable and listed */
        struct directory *directory = (struct directory *)file;  /* XXX: sz */
        deallocate_files(directory->subdirs);
    }
    deallocate_files(file->next);
    free(file->name);
    free(file);
}

struct file *reverse(struct file *f)
{
    if (!f) {
        return f;
    }
    struct file *p = f, *x = f->next;
    f->next = NULL;
    while (x) {
        struct file *c = x->next;
        x->next = p;
        p = x;
        x = c;
    }
    return p;
}

struct file *merge(struct file *a, struct file *b)
{
    struct file *result = NULL;
    while (a != NULL || b != NULL) {
        struct file *old_result = result;
        if (b == NULL || a != NULL && a->size > b->size) {
            result = a;
            a = a->next;
        } else {
            result = b;
            b = b->next;
        }
        result->next = old_result;
    }

    return reverse(result);
}

struct file *do_merge_sort(struct file *files, off_t n)
{
    if (n == 0) {
        assert(!files);
        return files;
    } else if (n == 1) {
        assert(files->next == NULL);
        return files;
    }
    off_t m = n / 2;
    struct file *middle = files;
    for (off_t i = 1; i < m; ++i) {
        middle = middle->next;
    }
    struct file *rest = middle->next;
    middle->next = NULL;
    return merge(do_merge_sort(files, m), do_merge_sort(rest, n - m));
}

void check_subdirs(struct directory *directory, off_t original_count)
{
    off_t sorted_count = 0;
    uint64_t max_size = UINT64_MAX;
    for (struct file *cur = directory->subdirs; cur; cur = cur->next) {
        ++sorted_count;
        assert(cur->size <= max_size);
        max_size = cur->size;
    }
    assert(sorted_count == original_count);
}

struct file *sorted_subdirs(struct directory *directory)
{
    if (!directory->subdirs_sorted) {
        off_t count = 0;
        for (struct file *cur = directory->subdirs; cur; cur = cur->next) {
            ++count;
        }
        directory->subdirs = do_merge_sort(directory->subdirs, count);
        check_subdirs(directory, count);
        directory->subdirs_sorted = 1;
    }
    return directory->subdirs;
}

struct file *build_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st)) {
        err(errno, "[WARNING] stat failed: %s\n", path);
        return NULL;
    }
    struct file *file;
    struct directory *directory;
    if (S_ISDIR(st.st_mode)) {
        directory = malloc(sizeof(struct directory));
        file = &directory->file;
    } else {
        file = malloc(sizeof(struct file));
    }

    file->next = NULL;
    file->parent = NULL;
    file->name = malloc(strlen(path) + 1);
    strcpy(file->name, path);
    file->type = (st.st_mode & S_IFMT) >> FILE_TYPE_OFFSET;
    file->size = st.st_size;
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (dir) {
            directory->subdirs = NULL;
            struct dirent *dirent;
            while ((dirent = readdir(dir))) {
                if (strcmp(dirent->d_name, ".") == 0
                    || strcmp(dirent->d_name, "..") == 0) {
                    continue;
                }
                char *subpath = concat_path(path, dirent->d_name);
                struct file *new_file = build_tree(subpath);
                if (new_file) {
                    new_file->parent = directory;
                    new_file->next = directory->subdirs;
                    directory->subdirs = new_file;
                    file->size += new_file->size;
                } else {
                    fprintf(stderr, "[WARNING]: cannot find file %s\n",
                            subpath);
                }
                free(subpath);
            }
            closedir(dir);
            directory->subdirs_sorted = 0;
        } else {
            file->type |= DIRECTORY_UNLISTABLE;
            directory->subdirs = NULL;
        }
    }
    return file;
}

void build_size_representation(char *str, off_t size)
{   /* Maximum string size is 3 + 1 + 2 + 2 + 1 = 10*/
    const static char PREFIXES[] = " kMGTPEZY";
    double d_size = size;
    uint32_t prefix_count = 0;
    while (d_size > 1000.) {
        d_size /= 1000.;
        prefix_count += 1;
    }

    sprintf(str, "%.2f%cB", d_size, PREFIXES[prefix_count]);
}

    char *trim_name(const char *name)
{
    if (name[0] == '.' && name[1] == '/') {
        return (char *) (name + 2);
    } else {
        return (char *) name;
    }
}

char *extract_name(char *line) {
    char *start = line;
    char *end = line;
    while (*end) {
        ++end;
    }
    --end;
    while (*start && isspace(*start)) {
        ++start;
    }
    if (!*start) {
        return start;  /* empty line */
    }
    while (isspace(*end)) {
        --end;
    }
    end[1] = '\0';
    return start;

}

void print_node(struct file *f)
{
    char size[10];
    build_size_representation(size, f->size);
    printf("%s: %s\n", trim_name(f->name), size);
    if (f->type == S_IFDIR >> FILE_TYPE_OFFSET) {
        struct directory *d = (struct directory *)f;
        uint32_t printed = 0;
        double explained = 0;
        printf("%64s %8s %6s\n", "file name", "size", "%");
        for (uint32_t i = 0; i < 80; ++i) {
            putchar('-');
        }
        putchar('\n');
        for (struct file *cur = sorted_subdirs(d); cur; cur=cur->next) {
            if (++printed > MAX_PRINTED || explained > 100. - MIN_PERCENTAGE) {
                break;
            }
            build_size_representation(size, cur->size);
            double percentage = 100. * (double) cur->size / (double) f->size;
            explained += percentage;
            printf("%64s %8s %5.1f%%\n", get_file_name(cur->name),
                   size, percentage);
        }
    }
}

struct file *next_entity(struct file *f, const char *s)
{
    if (strcmp("..", s) == 0) {
        return &f->parent->file;
    }
    if (f->type == S_IFDIR >> FILE_TYPE_OFFSET) {
        struct directory *d = (struct directory *)f;
        for (struct file *cur = d->subdirs; cur; cur = cur->next) {
            if (strcmp(get_file_name(cur->name), s) == 0) {
                return cur;
            }
        }
    }

    return NULL;
}

int main(int argc, char **argv)
{
    const char *base_path;
    int exit_code = 0;
    if (argc == 1) {
        base_path = ".";
    } else if (argc == 2) {
        base_path = argv[1];
    } else {
        fprintf(stderr, "[ERROR] incorrect arguments\n");
        exit_code = 1;
        goto exit_vanilla;
    }
    int original_wd_fd = open(".", O_RDONLY);
    if (original_wd_fd == -1) {
        err(errno, "can't open current directory");
        exit_code = 1;
        goto exit_vanilla;
    }
    printf("[INFO] building tree, please wait\n");
    struct file *tree = build_tree(base_path);
    if (!tree) {
        fprintf(stderr, "[ERROR] failed to build a tree, check path\n");
        exit_code = 1;
        goto exit_original_fd;
    }
    struct file *cur = tree;
    char *line = NULL;
    for (;;) {
        chdir(cur->name);
        print_node(cur);
        line = readline("> ");
        if (!line) {
            break;
        }
        char *name = extract_name(line);
        struct file *nxt = next_entity(cur, name);
        if (!nxt) {
            fprintf(stderr, "[ERROR] no such file: %s\n", name);
        }
        free(line);
        if (nxt) {
            cur = nxt;
        }
    }

exit_tree:
    deallocate_files(tree);
exit_original_fd:
    fchdir(original_wd_fd);
    close(original_wd_fd);
exit_vanilla:
    return exit_code;
}
