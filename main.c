#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include "arraylist.h"
#include <limits.h>
#include <sys/wait.h>


typedef struct{
    arraylist_t args;
    char *input_file;
    char *output_file;
    int redirection;
} command;

/*
 * command_init
 * Allocates and initializes an empty command struct representing one stage
 * in a pipeline, with an initial args list and cleared redirection fields.
 *
 * Arguments: none (void)
 * Returns:   pointer to the newly allocated command, or NULL on failure.
 */
command *command_init(void){
    command *cmd = malloc(sizeof(command));
    if(!cmd) return NULL;
    if(al_init(&cmd->args, 8) != 0){
        free(cmd);
        return NULL;
    }
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->redirection = 0;
    return cmd;
}

/*
 * free_command
 * Releases all heap memory owned by a command struct, including its argument
 * strings, args arraylist, redirection filenames, and the struct itself.
 *
 * Arguments: cmd - pointer to the command to free (no-op if NULL).
 * Returns:   void
 */
void free_command(command *cmd){
    if(!cmd) return;

    while(al_length(&cmd->args) > 0){
        char *arg = (char *)al_pop(&cmd->args);
        free(arg);
    }
    al_destroy(&cmd->args);
    free(cmd->input_file);
    free(cmd->output_file);
    free(cmd);
}

/*
 * free_pipeline
 * Frees every command stage stored inside a parsed pipeline arraylist and
 * destroys the pipeline's backing array.
 *
 * Arguments: pipeline - pointer to the arraylist of command * stages (no-op if NULL).
 * Returns:   void
 */
void free_pipeline(arraylist_t *pipeline){
    if(!pipeline) return;

    while(al_length(pipeline) > 0){
        command *cmd = (command *)al_pop(pipeline);
        free_command(cmd);
    }
    al_destroy(pipeline);
}

/*
 * read_commandline
 * Reads one line of arbitrary length from fd using unbuffered read() calls
 * (1 byte at a time), stopping immediately at '\n' or EOF so bytes belonging
 * to subsequent commands are not consumed early.
 *
 * Arguments: fd - open file descriptor to read from.
 * Returns:   heap-allocated, null-terminated line (excluding '\n'), or NULL
 *            on EOF (with no characters read) or allocation failure.
 */
char *read_commandline(int fd){
    int capacity = 128;
    int len = 0;
    char *buffer = malloc(capacity);
    if(!buffer) return NULL;

    char c;
    ssize_t bytes_read;

    while((bytes_read = read(fd, &c, 1)) > 0){
        if(c == '\n') break;

        if(len + 1 >= capacity){
            capacity *= 2;
            char *new_buffer = realloc(buffer, capacity);
            if(!new_buffer){
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
        }
        buffer[len++] = c;
    }

    if(bytes_read <= 0 && len == 0){
        free(buffer);
        return NULL;
    }

    buffer[len] = '\0';
    return buffer;
}

/*
 * next_token
 * Extracts the next whitespace-delimited token—or standalone operator
 * ("<", ">", ">>", "|")—from a command line and advances *line past it.
 *
 * Arguments: line - address of the caller's scanning cursor into the line.
 * Returns:   heap-allocated token string, or NULL when no tokens remain
 *            (or on allocation failure). Caller owns the returned string.
 */
char *next_token(char **line){
    char *ptr = *line;

    while(*ptr && isspace((unsigned char)*ptr)){
        ptr++;
    }

    if(*ptr == '\0'){
        *line = ptr;
        return NULL;
    }

    if(ptr[0] == '>' && ptr[1] == '>'){
        *line = ptr + 2;
        return strdup(">>");
    } 

    if(*ptr == '<' || *ptr == '>' || *ptr == '|'){
        char operator[2] = { *ptr, '\0'};
        *line = ptr + 1;
        return strdup(operator);
    }

    char *start = ptr;
    while(*ptr && !isspace((unsigned char)*ptr) && *ptr != '<' && *ptr != '>' && *ptr != '|'){
        ptr++;
    } 

    size_t len = ptr - start;
    char *token = malloc(len + 1);
    if(!token) return NULL;
    memcpy(token, start, len);
    token[len] = '\0';

    *line = ptr;
    return token;
}

/*
 * is_wildcard
 * Tests whether a filename matches a single-'*' wildcard pattern (prefix +
 * zero or more characters + suffix). Patterns starting with '*' will not
 * match hidden filenames starting with '.'.
 *
 * Arguments: name    - directory entry filename to test.
 *            pattern - filename pattern containing '*' (no '/' characters).
 * Returns:   1 if name matches pattern, 0 otherwise.
 */
int is_wildcard(const char *name, const char *pattern){
    char *wildcard  = strchr(pattern, '*');
    if(!wildcard) return 0;

    if(pattern[0] == '*' && name[0] == '.'){
        return 0;
    }

    size_t prefix_length = (size_t)(wildcard - pattern);
    char *suffix = wildcard + 1;
    size_t suffix_length = strlen(suffix);
    size_t name_length = strlen(name);

    if(name_length < prefix_length + suffix_length){
        return 0;
    }

    if(strncmp(name, pattern, prefix_length) != 0){
        return 0;
    }

    if (strcmp(name + (name_length - suffix_length), suffix) != 0){
        return 0;
    }

    return 1;
}

/*
 * compare_strings
 * qsort() comparator for sorting an array of char * string pointers in
 * lexicographical order via strcmp().
 *
 * Arguments: a, b - pointers to two char * elements in the array being sorted.
 * Returns:   negative, zero, or positive integer as returned by strcmp().
 */
int compare_strings(const void *a, const void *b){
    return strcmp(*(const char **)a, *(const char **)b);
}

/*
 * append_wildcard_or_argument
 * Appends token to command->args; if token contains '*', expands it against
 * files in the target directory in sorted order, or appends token literally
 * if no files match (or if the directory cannot be opened / path is too long).
 * Takes ownership of token (either storing it or freeing it if expanded).
 *
 * Arguments: command - target command struct whose args list is appended to.
 *            token   - heap-allocated argument or wildcard token.
 * Returns:   void
 */
void append_wildcard_or_argument(command *command, char *token){
    
    // if no wildcard token just append token into argument list 
    if(!strchr(token, '*')){
        al_append(&command->args, token);
        return;
    }

    char directory_path[1024];
    char *pattern;
    char *last_slash = strrchr(token, '/');

    if (last_slash){
        size_t directory_length = (size_t)(last_slash - token);
        if(directory_length >= sizeof(directory_path)){
            al_append(&command->args, token);
            return;
        }
        if(directory_length == 0){
            strcpy(directory_path, "/");
        } else {
            memcpy(directory_path, token, directory_length);
            directory_path[directory_length] = '\0';
        }
        pattern = last_slash + 1;
    } else {
        strcpy(directory_path, ".");
        pattern = token;
    }

    DIR *dir = opendir(directory_path);
    if(!dir){
        al_append(&command->args, token);
        return;
    }

    struct dirent *entry;
    
    arraylist_t matches;
    al_init(&matches, 8);

    while((entry = readdir(dir)) != NULL){

        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
            continue;
        }

        if(is_wildcard(entry->d_name, pattern)){
            char full_path[2048];
            if(last_slash){
                if(strcmp(directory_path, "/") == 0){
                    snprintf(full_path, sizeof(full_path), "/%s", entry->d_name);
                } else {
                    snprintf(full_path, sizeof(full_path), "%s/%s",directory_path, entry->d_name);
                }
            } else {
                snprintf(full_path, sizeof(full_path), "%s", entry->d_name);
            }

            al_append(&matches, strdup(full_path));
        }
    }
    closedir(dir);

    if(al_length(&matches) > 0){
        qsort(matches.data, al_length(&matches), sizeof(char *), compare_strings);

        for (unsigned i = 0; i < al_length(&matches); i++){
            al_append(&command->args, matches.data[i]);
        }

        matches.length = 0;
        al_destroy(&matches);
        free(token);
    } else {
        al_destroy(&matches);
        al_append(&command->args, token);
    }
}


/*
 * parse_line
 * Strips any '#' comment and parses a command line into a pipeline of
 * command structs with NULL-terminated argv arrays, expanded wildcards,
 * and input/output redirection targets ("<", ">", ">>").
 *
 * Arguments: line     - mutable command line string (truncated at '#' if found).
 *            pipeline - uninitialized arraylist_t populated with command *
 *                       stages when the function returns 0.
 * Returns:   0 on valid command/pipeline, 1 if the line is empty or a comment
 *            (pipeline left uninitialized), or -1 on syntax/allocation error
 *            (all partial allocations freed).
 */
int parse_line(char *line, arraylist_t *pipeline){
    //if we have a comment ignore everything after with terminator
    char *comment = strchr(line, '#');
    if(comment) *comment = '\0';



    char *cursor = line;
    char *token = next_token(&cursor);

    //if we dont have anything then return to skip
    if(!token) return 1;

    //create a pipeline for input
    if(al_init(pipeline, 4) != 0){
        free(token);
        return -1;
    }

    //create new command and add it to the pipeline
    command *curr = command_init();
    if(!curr){
        free(token);
        free_pipeline(pipeline);
        return -1;
    }
    al_append(pipeline, curr);



    while(token != NULL){
        //check pipeline
        if(strcmp(token, "|") == 0){
            free(token);
            // if we dont have arguments before the | then illegal
            if(al_length(&curr->args) == 0){
                free_pipeline(pipeline);
                return -1;
            }
            
            //end command struct for execv()
            al_append(&curr->args, NULL);

            //create next struct in the pipeline
            curr = command_init();
            if(!curr){
                free_pipeline(pipeline);
                return -1;
            }
            al_append(pipeline, curr);
            token = next_token(&cursor);
            continue;
        //input redirection
        } else if (strcmp(token, "<") == 0){
            free(token);
            char *target = next_token(&cursor);

            //make sure we have a redirection target and that target is'nt already taken that is legal if not return -1
            if(!target || strcmp(target, "|") == 0 || strcmp(target, "<") == 0 || strcmp(target, ">") == 0 || strcmp(target, ">>") == 0 || curr->input_file){
                if(target) free(target);
                free_pipeline(pipeline);
                return -1;
            }
            //set redirection target
            curr->input_file = target;
            token = next_token(&cursor);
            continue;
        //handle the output redirection
        } else if (strcmp(token, ">") == 0 || strcmp(token, ">>") == 0 ){

            int is_appended = (strcmp(token, ">>") == 0);
            free(token);
            char *target = next_token(&cursor);
            
            //make sure valid target and output hasnt been set yet
            if(!target || strcmp(target, "|") == 0 || strcmp(target, "<") == 0 || strcmp(target, ">") == 0 || strcmp(target, ">>") == 0 || curr->output_file){
                if(target) free(target);
                free_pipeline(pipeline);
                return -1;
            }
            curr->output_file = target;
            //tells us whether its > or >>
            curr->redirection = is_appended;
            token = next_token(&cursor);
            continue;
        //if wildcard card we expand the '*'
        //if regular arguments just append it to the command struct
        } else {
            append_wildcard_or_argument(curr, token);
            token = next_token(&cursor);
            continue;
        }
    }

    //if we have any trailing pipes thats illegal
    if(al_length(&curr->args) == 0){
        free_pipeline(pipeline);
        return -1;
    }

    al_append(&curr->args, NULL);
    return 0;
}

void print_prompt(void) {
    char cwd[PATH_MAX];

    if(getcwd(cwd, sizeof(cwd)) == NULL) {
        printf("ERROR: getcwd() failed. please run cd\nmysh$ ");
        fflush(stdout);
        return;
    }

    const char *home = getenv("HOME");
    size_t home_len = home ? strlen(home) : 0;

    if (home && home_len > 0 && strncmp(cwd, home, home_len) == 0 && (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
        printf("~%s$ ", cwd + home_len);
    } else {
        printf("%s$ ", cwd);
    }
    fflush(stdout);
    return;
}

void report_status(int status) {
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code != 0) printf("Exited with status %d\n", code);
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("Terminated by signal %d (%s)\n", sig, strsignal(sig));
    }
}

int is_builtin(const char *name) {
    if (strcmp(name, "cd") == 0 || strcmp(name, "pwd") == 0 || strcmp(name, "which") == 0 || strcmp(name, "exit") == 0) {
        return 1;
    }
    return 0;
}

char *find_executable(const char *name){
    if (!name || name[0] == '\0') return NULL;

    if (strchr(name, '/') != NULL) {
        if(access(name, X_OK) == 0) {
            return strdup(name); // need a malloced string so free(path) doesnt mess up later
        }
        return NULL;
    }

    const char *to_search[] = {"/usr/local/bin", "/usr/bin", "/bin"};
    for(int i = 0; i < 3; i++) {
        size_t len = strlen(to_search[i]) + 1 + strlen(name) + 1;

        char *full_path = malloc(len);
        if (!full_path) return NULL;

        snprintf(full_path, len, "%s/%s", to_search[i], name);
        if(access(full_path, X_OK) == 0) {
            return full_path;
        }

        free(full_path);
    }
    return NULL;
}

int setup_redirection(command *cmd, int use_dev_null){
    if(cmd->input_file) {
        int fd = open(cmd->input_file, O_RDONLY);
        if(fd < 0) {
            perror(cmd->input_file);
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    } else if(use_dev_null) {
        int fd = open("/dev/null", O_RDONLY);

        if (fd < 0) {
            perror("/dev/null");
            return -1;
        }

        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }

        close(fd);
    }

    if (cmd->output_file) {
            int flags;

            if (cmd->redirection) {
                flags = O_WRONLY | O_CREAT | O_APPEND;
            } else {
                flags = O_WRONLY | O_CREAT | O_TRUNC;
            }

            int fd = open(cmd->output_file, flags, S_IRUSR | S_IWUSR | S_IRGRP);

            if (fd < 0) {
                perror(cmd->output_file);
                return -1;
            }
            if(dup2(fd, STDOUT_FILENO) < 0) {
                perror("dup2 in setup_redirection");
                close(fd);
                return -1;
            }

            close(fd);
    }
    return 0;
}

int run_builtin(command *cmd, int *should_exit) {
    char **args = (char **)cmd->args.data;

    if(strcmp(args[0], "cd") == 0) {
        if (args[1] != NULL && args[2] != NULL) {
        fprintf(stderr, "ERROR: cd has too many argments\n");
        return 1;
        }

        const char *target;

        if (args[1] != NULL) {
            target = args[1];
        } else {
            target = getenv("HOME");
        }

        if (!target) {
            fprintf(stderr, "ERROR: in cd, there is no HOME");
            return -1;
        }

        if (chdir(target) != 0) {
            perror("cd");
            return 1;
        }

        return 0;
    }

    if(strcmp(args[0], "which") == 0) {
        if(args[1] == NULL) {
            fprintf(stderr, "ERORR: missing arg for which command\n");
            return 1;
        }
        int status = 0;
        for (int i = 1; args[i] != NULL; i++) {
            char *path = find_executable(args[i]);
            if(!path) {
                status = 1;
            } else {
                printf("%s\n", path);
                free(path);
            }
        }
        return status;
    }

    if(strcmp(args[0], "pwd") == 0) {
        if (args[1] != NULL) {
            fprintf(stderr, "ERROR: too many args for pwd\n");
            return 1;
        }

        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("pwd");
            return 1;
        }

        printf("%s\n", cwd);
        return 0;
    }

    if(strcmp(args[0], "exit") == 0) {
        *should_exit = 1;
        return 0;
    }
    return 1;
}

int execute_pipeline(arraylist_t *pipeline, int interactive, int *should_exit, int *last_status) {
    int count = (int) al_length(pipeline);
    
    if (count == 1) {
        command *cmd = (command *) pipeline->data[0];
        char **args = (char **)cmd->args.data;

        if (is_builtin(args[0])) {
            int saved_in = -1;
            int saved_out = -1;

            if(cmd->input_file) {
                saved_in = dup(STDIN_FILENO);
            }

            if(cmd->output_file) {
                saved_out = dup(STDOUT_FILENO);
            }

            if (setup_redirection(cmd, 0) == 0) {
                run_builtin(cmd, should_exit);
                fflush(stdout);
            }

            if(saved_in != -1) {
                dup2(saved_in, STDIN_FILENO);
                close(saved_in);
            }

            if(saved_out != -1) {
                dup2(saved_out, STDOUT_FILENO);
                close(saved_out);
            }

            return 0;
        } else {
            fflush(NULL);
            pid_t pid = fork();

            if(pid < 0) {
                perror("fork");
                return 0;
            }

            if (pid == 0) {
                if (setup_redirection(cmd, !interactive) < 0) {
                    _exit(1);
                }

                char *path = find_executable(args[0]);
                if(!path) {
                    fprintf(stderr, "%s: command not found\n", args[0]);
                    _exit(1);
                }

                execv(path, args);
                perror(args[0]);
                free(path);
                _exit(1);
            }

            waitpid(pid, last_status, 0);
            return 1;
        }
    } else if (count > 1){
        pid_t *pids = malloc(count * sizeof(pid_t));
        if (!pids) {
            perror("mallocing children");
            return 0;
        }

        int prev_read = -1;
        fflush(NULL);

        for (int i = 0; i < count; i++) {
            command *cmd = (command *) pipeline->data[i];
            char **args = (char **)cmd->args.data;

            if(strcmp(args[0], "exit") == 0) {
                *should_exit = 1;
            }

            int pfd[2] = {-1, -1};

            if(i < count - 1) {
                if (pipe(pfd) < 0) {
                    perror("pipe");
                    if (prev_read != -1) close(prev_read);
                    for(int j = 0; j < i; j++) {
                        waitpid(pids[j], NULL, 0);
                    }
                    free(pids);
                    return 0;
                }
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                if(prev_read != -1) close(prev_read);
                if(i < count - 1) {
                    close(pfd[0]);
                    close(pfd[1]);
                }
                for(int j = 0; j < i; j++) {
                    waitpid(pids[j], NULL, 0);
                }
                free(pids);
                return 0;
            }

            if (pid == 0) {
                if (prev_read != -1) {
                    dup2(prev_read, STDIN_FILENO);
                    close(prev_read);
                }

                if (i < count - 1) {
                    dup2(pfd[1], STDOUT_FILENO);
                    close(pfd[0]);
                    close(pfd[1]);
                }

                if (setup_redirection(cmd, i == 0 && !interactive) < 0) {
                    _exit(1);
                }

                if (is_builtin(args[0])) {
                    int fakeExit = 0;
                    int commandCode = run_builtin(cmd, &fakeExit);
                    fflush(stdout);
                    _exit(commandCode);
                } else {
                    char *path = find_executable(args[0]);
                    if (!path) {
                        fprintf(stderr, "%s: command wasn't found\n", args[0]);
                        _exit(1);
                    }
                    execv(path, args);
                    perror(args[0]);
                    free(path);
                    _exit(1);
                }
            }

            pids[i] = pid;

            if(prev_read != -1) {
                close(prev_read);
            }
            if(i < count - 1) {
                close(pfd[1]);
                prev_read = pfd[0];
            }
        }

        for(int i = 0; i < count; i++) {
            int status = 0;

            waitpid(pids[i], &status, 0);

            if (i == count - 1) {
                *last_status = status;
            }
        }

        free(pids);
        return 1;
    } else {
        *should_exit = 1;
        return 0;
    }
}

int main (int argc, char **argv) {
    int input_fd;
    int interactive;

    switch (argc) {
        case 1:
            input_fd = STDIN_FILENO;
            interactive = isatty(input_fd);
            break;
        case 2:
            input_fd = open(argv[1], O_RDONLY);
            interactive = 0;
            if (input_fd < 0) {
                perror("File open failed");
                return EXIT_FAILURE;
            }
            break;
        default:
            fprintf(stderr, "ERROR: Incorrect number of args. Either 0 args, or 1 arg with the input file.\n");
            return EXIT_FAILURE;
    }

    if (interactive) {
        printf("Hello Sir, welcome to jarvissh!\n");
    }

    int should_exit = 0;
    while (!should_exit) {
        if (interactive) {
            print_prompt(); 
        }

        char *line = read_commandline(input_fd);
        if (line == NULL) {
            break;
        }

        arraylist_t pipeline;
        int parse_status = parse_line(line, &pipeline);

        if (parse_status == 1) {
            free(line);
            continue;
        }
        if (parse_status == -1) {
            fprintf(stderr, "syntax error: %s\n", line);
            free(line);
            continue;
        }

        free(line);

        int last_status = 0;
        int ran_child = execute_pipeline(&pipeline, interactive, &should_exit, &last_status);

        if (interactive && ran_child) report_status(last_status);

        free_pipeline(&pipeline);
    }

    if (interactive) {
        printf("\nGoodnight Sir, exiting jarvissh.\n");
    }

    if (input_fd != STDIN_FILENO) {
        close(input_fd);
    }

    return EXIT_SUCCESS;
}