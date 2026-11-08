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

int parse_line(char *line, arraylist_t *pipeline){
    if(al_init(pipeline, 4) != 0) return -1;

    char *comment = strchr(line, '#');
    if(comment) *comment = '\0';

    char *cursor = line;
    char *token = next_token(&cursor);

    if(!token) return 1;

    command *curr = command_init();
    if(!curr){
        free(token);
        free_pipeline(pipeline);
        return -1;
    }
    al_append(pipeline, curr);

    while(token != NULL){
        if(strcmp(token, "|") == 0){
            if(al_length(&curr->args) == 0){
                free_pipeline(pipeline);
                return -1;
            }

            al_append(&curr->args, NULL);

            curr = command_init();
            if(!curr){
                free_pipeline(pipeline);
                return -1;
            }
            al_append(pipeline, curr);
            token = next_token(&cursor);
            continue;
        } else if (strcmp(token, "<") == 0){
            free(token);
            char *target = next_token(&cursor);

            if(!target || strcmp(target, "|") == 0 || strcmp(target, "<") == 0 || strcmp(target, ">") == 0 || strcmp(target, ">>") == 0 || curr->input_file){
                if(target) free(target);
                free_pipeline(pipeline);
                return -1;
            }
            curr->input_file = target;
            token = next_token(&cursor);
            continue;
        } else if (strcmp(token, ">") == 0 || strcmp(token, ">>") == 0 ){
            int is_appended = (strcmp(token, ">>") == 0);
            free(token);
            char *target = next_token(&cursor);
            
            if(!target || strcmp(target, "|") == 0 || strcmp(target, "<") == 0 || strcmp(target, ">") == 0 || strcmp(target, ">>") == 0 || curr->output_file){
                if(target) free(target);
                free_pipeline(pipeline);
                return -1;
            }
            curr->output_file = target;
            curr->redirection = is_appended;
            token = next_token(&cursor);
            continue;
        } else {
            al_append(&curr->args, token);
            token = next_token(&cursor);
            continue;
        }
    }

    if(al_length(&curr->args) == 0){
        free_pipeline(pipeline);
        return -1;
    }

    al_append(&curr->args, NULL);
    return 0;
}

int main(void) {
    printf("--- Testing Shell Parser (Ctrl+D to exit) ---\n\n");

    while (1) {
        printf("test-shell$ ");
        fflush(stdout);

        char *line = read_commandline(STDIN_FILENO);
        if (!line) {
            printf("\nEOF reached.\n");
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