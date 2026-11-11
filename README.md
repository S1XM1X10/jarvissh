Akshit Garg - ag2578
Johnathan Persaud - jlp419

Our interactive shell uses a function that parses tokens to determine whether a given expression is legal before execution. For pipes, we create an ArrayList of commands. The command struct stores an ArrayList of arguments, an input file name, and an output file name, as well as a flag to check whether or not we need to turn ‘>’  into ‘>>’. The pipeline has an ArrayList of commands. These data structures are then fed into parse_line, which will check for any illegal arguments and then return a success code.

read_commandline: This function reads while there are bytes to read; it then reads into a buffer, which can be reallocated based on the input size using realloc(), and returns the buffer at the end.

next_token: This function searches a given line and returns the next valid token in its string and will then return that token as  a char

Is_wildcard: will take a name and pattern, and will check if the given name is a wildcard; if not, return 0. The function will ensure that the name matches the pattern in all directions, and if not, will return 0 to indicate it doesn't match.

append_wildcard_or_argument: If the argument isn't a wildcard, then we append the token to the command args. If it's a wildcard, we check whether we need to search a specific directory or can search anywhere. We then read through the directories and collect matches to the wildcard into an ArrayList. We then use qsort to sort matches, append these patches to the command args ArrayList, and delete the ArrayList matches.  

parse_line: This function checks for comments and appends the line to terminate at that point. We create a cursor to follow as we parse through the line and create a token to check what our first action will be. We create an initial command and a pipeline ArrayList. First, we check whether the token is a pipe; if so, we need to ensure there is an argument before it. If not, it's illegal; otherwise, we can proceed and append the current command to the pipeline.  We then check for input redirection; in this case, we go into the command and change its input file. If an input file is already assigned, we return -1 for failure. We then check for output redirection and the cases of ‘>’ or ‘>> ’. If an output file is already assigned, we return -1 as a failure; otherwise, we proceed to assign the output file to the command. In the case we have a token that isn't ‘|’, ‘<’,‘>’, or ‘>>’ we use append_wildcard_argument to add it to the command->args ArrayList. At the end, we check for trailing pipes and return -1 in those cases as well. It should be noted that any checks for tokens ‘|’, ‘<’,‘>’, or ‘>>’ also check to make sure that the future token isn't another token in this set and will return -1 if it is. 


Our shell runs in two possible modes, interactive and batch. Interactive is triggered if you run ./mysh with no arguments and do not use redirection/pipes to change its input stream. Batch mode runs if you run ./mysh with zero args and have its input stream not be the terminal, or if you run ./mysh with one arg. More than one arg will trigger an error.

Our shell treats every line you pass it as a pipeline, and the main distinction is whether or not your pipeline has one command or more than one command. This distinction is made within execute_pipeline. When we get a line with read_commandline(), we then pass it into parse_line(), which will deal with wildcards as well as construct our pipeline arraylist. After that, we simply run with execute_pipeline, which contains logic for checking b/w built in or not, handling redirection, and handling pipelines of length > 1. 


For more info on the helper functions and any other functions, we added function comment headers for all functions in main.c that you can check. 



TESTING

we did all the simple tests that were present in the writeup, and some of our own, simply just testing things like the built in commands, and other non built ins, and exit within a pipeline manually in interactive mode. To test batch mode, we made a file batchtest.txt that went into a different folder on my computer, and it worked fine.