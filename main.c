#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <ctype.h>
#include "arraylist.h"


typedef struct{
    arraylist_t args;
    char *input_file;
    char *output_file;
    int redirection;
} command;

command *command_init(){
    command *cmd = malloc(sizeof(command));
    if(!cmd) return NULL;
    al_init(&cmd->args, 8);
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->redirection = 0;
    return cmd;
}

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

void free_pipeline(arraylist_t *pipeline){
    if(!pipeline) return;

    while(al_length(pipeline) > 0){
        command *cmd = (command *)al_pop(pipeline);
        free_command(cmd);
    }
    al_destroy(pipeline);
}

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

int compare_strings(const void *a, const void *b){
    return strcmp(*(const char **)a, *(const char **)b);
}

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
        if(directory_length == 0){
            strcpy(directory_path, "/");
        } else {
            strncpy(directory_path, token, directory_length);
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
        al_destroy(&matches);
        free(token);
    } else {
        al_destroy(&matches);
        al_append(&command->args, token);
    }
}


int parse_line(char *line, arraylist_t *pipeline){

    //create a pipeline for input
    if(al_init(pipeline, 4) != 0) return -1;

    //if we have a comment ignore everything after with terminator
    char *comment = strchr(line, '#');
    if(comment) *comment = '\0';



    char *cursor = line;
    char *token = next_token(&cursor);

    //if we dont have anything then return to skip
    if(!token) return 1;

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

int main(void) {
    printf("====================================================\n");
    printf("         MYSH PARSER & WILDCARD TEST SUITE          \n");
    printf("====================================================\n\n");

    const char *test_cases[] = {
        /* Basic Wildcard Expansions */
        "ls *.c",
        "echo *.h",
        "ls a*.c",
        "cat non_existent_pattern*.xyz",

        /* Directory Path Wildcards */
        "ls /usr/include/std*.h",
        "echo /bin/l*",

        /* Pipeline + Wildcards */
        "cat *.c | grep include | wc -l",

        /* Redirection + Wildcards */
        "grep -n main *.c > results.txt",
        "cat < input.txt | sort >> output.log",

        /* Hidden File Check (should NOT match hidden files like .git or .) */
        "echo *",

        /* Syntax Errors */
        "| ls",
        "cat < | grep foo",
        "echo hi >"
    };

    size_t num_tests = sizeof(test_cases) / sizeof(test_cases[0]);

    printf("--- Running Built-in Test Cases ---\n\n");
    for (size_t t = 0; t < num_tests; t++) {
        printf(">>> TEST %zu: \"%s\"\n", t + 1, test_cases[t]);

        char *line = strdup(test_cases[t]);
        arraylist_t pipeline;
        int status = parse_line(line, &pipeline);

        if (status == 1) {
            printf("    [Result: Empty line or comment ignored]\n");
        } else if (status == -1) {
            printf("    [Result: Syntax error detected (Correct for malformed lines)]\n");
        } else {
            printf("    [Result: Parsed %u command stage(s)]\n", al_length(&pipeline));
            for (unsigned i = 0; i < al_length(&pipeline); i++) {
                command *cmd = (command *)pipeline.data[i];
                char **args = (char **)cmd->args.data;

                printf("      [Stage %u]:\n", i);
                for (int j = 0; args[j] != NULL; j++) {
                    printf("        argv[%d] = \"%s\"\n", j, args[j]);
                }
                if (cmd->input_file) {
                    printf("        stdin  < \"%s\"\n", cmd->input_file);
                }
                if (cmd->output_file) {
                    printf("        stdout %s \"%s\"\n", cmd->redirection ? ">>" : ">", cmd->output_file);
                }
            }
            free_pipeline(&pipeline);
        }

        free(line);
        printf("\n");
    }

    printf("====================================================\n");
    printf("         INTERACTIVE MODE (Ctrl+D to exit)          \n");
    printf("====================================================\n\n");

    while (1) {
        printf("test-shell$ ");
        fflush(stdout);

        char *line = read_commandline(STDIN_FILENO);
        if (!line) {
            printf("\nExiting.\n");
            break;
        }

        arraylist_t pipeline;
        int status = parse_line(line, &pipeline);

        if (status == 1) {
            printf("  -> [Empty line / comment ignored]\n");
        } else if (status == -1) {
            printf("  -> [Syntax Error Detected]\n");
        } else {
            printf("  -> Successfully parsed %u command(s):\n", al_length(&pipeline));

            for (unsigned i = 0; i < al_length(&pipeline); i++) {
                command *cmd = (command *)pipeline.data[i];
                char **args = (char **)cmd->args.data;

                printf("     [Command %u]\n", i);
                for (int j = 0; args[j] != NULL; j++) {
                    printf("       argv[%d] = \"%s\"\n", j, args[j]);
                }
                if (cmd->input_file) {
                    printf("       stdin  < \"%s\"\n", cmd->input_file);
                }
                if (cmd->output_file) {
                    printf("       stdout %s \"%s\"\n", cmd->redirection ? ">>" : ">", cmd->output_file);
                }
            }
            free_pipeline(&pipeline);
        }

        free(line);
        printf("\n");
    }

    return 0;
}