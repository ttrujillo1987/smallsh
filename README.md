Program is a custom written shell. Provides a prompt for running commands, allows for comments, expands variable "$$", executes three built-in
commands (exit, cd, and status), executes other commands by creating new processes, supports I/O redirection, supports running commands in 
foreground and background processes, and includes custom handlers for SIGINT and SIGSTP.

To compile and run this program:

1. Navigate to the folder holding smallsh.

2. To compile the program and create an executable file named "smallsh", use the following command:

	gcc --std=gnu99 -o smallsh smallsh.c

3. Now, to run the program, use the following command

	./smallsh 

The program should now be running. 
