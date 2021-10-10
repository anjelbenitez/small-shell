# small-shell
A small shell written in C that implements a subset of features of other well-known shells, such as bash.

## Features:
1) Provides a prompt for running commands
2) Handle blank lines and comments, which are lines beginning with the # character
3) Provides expansion for the variable $$
4) Executes 3 commands exit, cd, and status via code built into the shell
5) Executes other commands by creating new processes using a function from the exec family of functions
6) Supports input and output redirection
7) Supports running commands in foreground and background processes
8) Implements custom handlers for 2 signals, SIGINT and SIGTSTP

## Compiling and Running:

1) Make sure that shell.c is located in your current working directory.

2) In the terminal, enter "gcc -o shell shell.c" to compile the code and to make an executable file named 'shell' within the same directory.

3) Enter "./shell" into the terminal to run the executable file.

4) Enter command prompts.
