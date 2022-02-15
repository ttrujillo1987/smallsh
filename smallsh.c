/*
* Written by Timothy Trujillo
* Date: 2/2/2022
* Assignment 3: Smallsh
* Program is a custom written shell. Provides a prompt for running commands, allows for comments, expands variable "$$", executes three built-in
* commands (exit, cd, and status), executes other commands by creating new precesses, supports I/O redirection, supports running commands in 
* foreground and background processes, and includes custom handlers for SIGINT and SIGSTP.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

// Define the character that will be used to prompt the user
#define PROMPT ": "

// Define the maximum length of a command line and the max number of arguments
#define MAXCOMM 2048
#define MAXARG 512

// Define variable for monitoring whether or not the process is running in foreground-only mode
// 
// Citation: errno saving technique was adapted on 2/2/2022 from Professor Gambord's response to "Global Variables Okay?" on EdDiscussions
//     https://edstem.org/us/courses/16718/discussion/1067170
volatile static sig_atomic_t fgOnly = 0;

/*====================== structs =============================================================================================================================*/

// Define struct for incoming commands
struct commandLine {
	char* command;
	char* arguments[MAXARG];
	char* extendArgs[MAXARG + 2];
	char* redirection[2];
	int background;
};

// Define struct for PIDs running in the background
struct bgPid {
	int backgroundPid;
	struct bgPid* next;
};

/*====================== sigaction functions ====================================================================================================================*/

/*
* Signal handler for SIGINT when it is recieved by a foreground child process. Causes the child process to terminate when SIGINT is recieved.
* 
* Citation 1: Adapted from Module 5 - Processes II; Exploration: Signal Handling API; Example: Custom Handler for SIGINT
*     https://canvas.oregonstate.edu/courses/1884946/pages/exploration-signal-handling-api?module_item_id=21835981
* Citation 2: errno saving technique adapted on 2/2/2022 from: Kerrisk, Michael. “Chapter 21.” The Linux Programming Interface a Linux Und UNIX System Programming Handbook, 
*     No Starch Press, San Francisco, CA, 2018, p. 427. 
* Citation 3: errno saving technique was also adapted on 2/2/2022 from Professor Gambord's response to "Global Variables Okay?" on EdDiscussions
*     https://edstem.org/us/courses/16718/discussion/1067170
*/
void childSIGINT(int signo) {

	// Save errno  in case any functions here change it
	int save_err;
	save_err = errno;

	int childProc = getpid();
	kill(childProc, SIGTERM);

	// Restore errno
	errno = save_err;

}

/*
* Signal handler for SIGTSTP when it is recieved while sitting at the prompt. Toggles foreground-only mode on and off.
* 
* Citation 1: Adapted from Module 5 - Processes II; Exploration: Signal Handling API; Example: Custom Handler for SIGINT
*     https://canvas.oregonstate.edu/courses/1884946/pages/exploration-signal-handling-api?module_item_id=21835981
* Citation 2: errno saving technique adapted on 2/2/2022 from: Kerrisk, Michael. “Chapter 21.” The Linux Programming Interface a Linux Und UNIX System Programming Handbook,
*     No Starch Press, San Francisco, CA, 2018, p. 427.
* Citation 3: errno saving technique was also adapted on 2/2/2022from Professor Gambord's response to "Global Variables Okay?" on EdDiscussions
*     https://edstem.org/us/courses/16718/discussion/1067170
*/
void parentSIGTSTP(int signo) {

	// Save errno  in case any functions here change it
	int save_err;
	save_err = errno;

	// Check the status of fgOnly and toggles it between states
	if (fgOnly == 0) {
		char* message = "Entering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, 50);
		fgOnly = 1;
	}

	else {
		char* message = "Exiting foreground-only mode\n";
		write(STDOUT_FILENO, message, 30);
		fgOnly = 0;
	}

	// Restore errno
	errno = save_err;

}

/*====================== sigaction structs =============================================================================================================================*/

/*
* Initialize a sigaction struct to ignore SIGINT. This will later be changed for child processes running in the foreground
* 
* Citation: Adapted from Module 5 - Processes II; Exploration: Signal Handling API; Example: Custom Handler for SIGINT
*     https://canvas.oregonstate.edu/courses/1884946/pages/exploration-signal-handling-api?module_item_id=21835981
*/
void initSIGINT(void) {

	// Initialize the SIGINT_ignore struct
	struct sigaction SIGINT_ignore = { { 0 } };

	// Set sa_handler to SIG_IGN to ignore this signal
	SIGINT_ignore.sa_handler = SIG_IGN;
	sigfillset(&SIGINT_ignore.sa_mask);
	SIGINT_ignore.sa_flags = 0;

	// Install the signal handler so smallsh will default to ignoring SIGINT
	sigaction(SIGINT, &SIGINT_ignore, NULL);
}

/*
* Initialize a sigaction struct to ignore SIGTSTP. This will be used for any child process
* 
* Citation: Adapted from Module 5 - Processes II; Exploration: Signal Handling API; Example: Custom Handler for SIGINT
*     https://canvas.oregonstate.edu/courses/1884946/pages/exploration-signal-handling-api?module_item_id=21835981
*/
void initSIGTSTP(void) {

	// Initialize the SIGTSTP_ignore struct
	struct sigaction SIGTSTP_ignore = { { 0 } };

	// Set sa_handler to SIG_IGN to ignore this signal
	SIGTSTP_ignore.sa_handler = SIG_IGN;
	sigfillset(&SIGTSTP_ignore.sa_mask);
	SIGTSTP_ignore.sa_flags = 0;

	// Install the signal handler so child processes can ignore SIGTSTP
	sigaction(SIGTSTP, &SIGTSTP_ignore, NULL);
}

/*
* Initialize a sigaction struct for SIGINT to be used child processes running in the foreground.
* 
* Citation: Adapted from Module 5 - Processes II; Exploration: Signal Handling API; Example: Custom Handler for SIGINT
*     https://canvas.oregonstate.edu/courses/1884946/pages/exploration-signal-handling-api?module_item_id=21835981
*/
void changeSIGINT(void) {
	// Initialize the SIGINT_child struct
	struct sigaction SIGINT_child = { { 0 } };

	// Set sa_handler to childSIGINT to terminate process when SIGINT is recieved
	SIGINT_child.sa_handler = childSIGINT;
	sigfillset(&SIGINT_child.sa_mask);
	SIGINT_child.sa_flags = 0;

	// Install the signal handler so smallsh will terminate process when SIGINT is recieved
	sigaction(SIGINT, &SIGINT_child, NULL);
}

/*
* Initialize a sigaction struct for SIGTSTP to be used by the parent process.
* 
* Citation: Adapted from Module 5 - Processes II; Exploration: Signal Handling API; Example: Custom Handler for SIGINT
*     https://canvas.oregonstate.edu/courses/1884946/pages/exploration-signal-handling-api?module_item_id=21835981
*/
void changeSIGTSTP(void) {
	// Initialize the SIGTSTP_child struct
	struct sigaction SIGTSTP_parent = { { 0 } };

	// Set sa_handler to parentSIGTSTP to enter/exit foreground-only mode when SIGTSTP is recieved
	SIGTSTP_parent.sa_handler = parentSIGTSTP;
	sigfillset(&SIGTSTP_parent.sa_mask);
	SIGTSTP_parent.sa_flags = 0;

	// Install the signal handler so smallsh will terminate process when SIGINT is recieved
	sigaction(SIGTSTP, &SIGTSTP_parent, NULL);
}

/*====================== functions ===========================================================================================================================*/

/*
* Prompt user to enter a command. User takes a pointer to memory for storing the user's inpu. Returns 1 if the user enters a command and 0 if the 
* user enters a comment or a blank command.
*/
int promptUser(char *commandLine) {
	int hasChars = 0;
	int i;

	//Initialize commandLine prompt with all null characters
	memset(commandLine, '\0', MAXCOMM);
	
	// Prompt the user for a command
	printf("%s", PROMPT);
	fflush(stdout);

	// Get the command from the user
	fgets(commandLine, MAXCOMM, stdin);

	// Check to see if an argument of all spaces or a comment has been entered
	for (i = 0; i < strlen(commandLine); i++) {
		if (commandLine[i] == '#') {
			return 0;
		}

		// Check to all empty spaces were entered
		else if (commandLine[i] != ' ' && commandLine[i] != '\0' && commandLine[i] != '\n') {
			hasChars = 1;
			break;
		}
	}

	// Return 0 if the user entered a comment or a blank command
	if (commandLine[0] == '#' || commandLine[0] == '\0' || commandLine[0] == '\n' || hasChars == 0) {
		return 0;
	}
	
	// Return 1 if the user entered a command
	else {
		return 1;
	}
}

/*
* Find any instance of '$$' in the command and expand it into the process ID of the smallsh. Takes in the command string and performs expansion in place
*/
void varExpansion(char* commandLine) {
	
	// Get the PID of smallsh and convert it into a string
	int smallshPid = getpid();
	char smallshPidStr[7];
	sprintf(smallshPidStr, "%d", smallshPid);
	
	// Create a temporary placeholder for building the command with '$$' variable expanded 
	char* tempPlaceholder = malloc(sizeof(char) * MAXCOMM);
	memset(tempPlaceholder, '\0', sizeof(char) * MAXCOMM);
	
	int i;
	int j = 0;

	// Create a null-terminated string that can be used to strcat individual characters
	char nextChar[2];
	memset(nextChar, '\0', 2);

	// Iterate through the command and look for instances of '$$'
	for (i = 1; i < strlen(commandLine)+1; i++) {

		// If '$$' is found, append the PID to the placeholder string and set the '$$' instance to '##'. 
		// This will show that these characters have already been used for expansion.
		if (commandLine[i] == '$' && commandLine[j] == '$') {
			strcat(tempPlaceholder, smallshPidStr);
			commandLine[i] = '#';
			commandLine[j] = '#';
			i++;
			j++;
		}

		// Otherwise, append the character to the temp string
		else {
			nextChar[0] = commandLine[j];
			strcat(tempPlaceholder, nextChar);
		}
		j++;
	}

	// Copy the temp string with variables expanded over to the commandLine string.
	strcpy(commandLine, tempPlaceholder);

	free(tempPlaceholder);
}

/*
* Process the command entered by the user. Takes in the string entered by the user and divides it into the command, arguments, redirection (if applicable)
* and whether or not the process should be sent to the background.
*/
struct commandLine* processComm(char* commandLine){
	struct commandLine* currCommand = malloc(sizeof(struct commandLine));

	int i;
	int j = 0;

	// Keep track of index within currCommand->arguments and currCommand->redirection so that new elements can be added to the proper locations
	int iArguments = 0;
	int iRedirections = 0;
	int iExtendArgs = 0;

	// Set both redirection options to null
	currCommand->redirection[0] = '\0';
	currCommand->redirection[1] = '\0';

	// Initialize the arguments array with null values
	for (i = 0; i < MAXARG; i++) {
		currCommand->arguments[i] = '\0';
	}

	// Initialize the extendArgs array with null values
	for (i = 0; i < MAXARG+2; i++) {
		currCommand->extendArgs[i] = '\0';
	}

	// Add a special delimiter temporarily between redirectors and files. This will allow strtok_r to use spaces as delimiters.
	for (i = 1; i < strlen(commandLine); i++) {
		if (commandLine[j] == '<' || commandLine[j] == '>') {
			commandLine[i] = ';';
		}
		j++;
	}

	// Check to see if the input end with an ampersand. If so, change currCommand->background to 1 indicating the command should
	// be run in the background.
	if (commandLine[strlen(commandLine) - 2] == '&' && commandLine[strlen(commandLine) - 3] == ' ') {
		currCommand->background = 1;
		commandLine[strlen(commandLine) - 2] = '\0';
	}
	else {
		currCommand->background = 0;
	}

	// For maintaining context between srttok calls
	char* saveptr;

	// First token is the command
	char* token = strtok_r(commandLine, " ,'\n'", &saveptr);
	currCommand->command = calloc(strlen(token) + 1, sizeof(char));
	currCommand->extendArgs[iExtendArgs] = calloc(strlen(token) + 1, sizeof(char));
	strcpy(currCommand->command, token);

	// // Add the command to the extended arguments array that will be used to pass the arguments list to execvp()
	strcpy(currCommand->extendArgs[iExtendArgs], token);
	iExtendArgs += 1;

	token = strtok_r(NULL, " ,'\n'", &saveptr);

	// Process the arguments and redirections if applicable
	while (token != NULL) {

		// Check if the token is a redirection
		if (token[0] == '<' || token[0] == '>') {
			token[1] = ' ';
			currCommand->redirection[iRedirections] = calloc(strlen(token) + 1, sizeof(char));
			strcpy(currCommand->redirection[iRedirections], token);
			iRedirections += 1;
		}

		// Otherwise, it will be categorized as an argument
		else {
			currCommand->arguments[iArguments] = calloc(strlen(token) + 1, sizeof(char));
			currCommand->extendArgs[iExtendArgs] = calloc(strlen(token) + 1, sizeof(char));
			strcpy(currCommand->arguments[iArguments], token);

			// Add the argument to the extended arguments array that will be used to pass the arguments list to execvp()
			strcpy(currCommand->extendArgs[iExtendArgs], token);
			iArguments += 1;
			iExtendArgs += 1;
		}
		token = strtok_r(NULL, " ,'\n'", &saveptr);
	}

	// Add NULL as the last element of the extendedArgs array
	currCommand->extendArgs[iExtendArgs] = NULL;

	return currCommand;
}

/*
* Function performs cleanup before exiting the program. Takes in the list of background processes and kills them all. It then returns 0 to main which
* causes the loop to exit and terminates the program.
*/
int exitCheck(struct bgPid* bgList) {
	
	while (bgList != NULL) {
		if (bgList->backgroundPid != '\0') {
			kill(bgList->backgroundPid, SIGKILL);
		}

		bgList = bgList->next;
	}

	// Return 0 to the runSmallsh variable in main which will cause the shell to terminate
	return 0;

}

/*
* Function changes the working directory of smallsh. If there are no arguments, the directory will be changed to the one
* specified in the HOME environment variable. This function can also process a single argument which is the (relative or absolute)
* path of a directory to change to. This function takes in the current command as an argument
* 
* Citation: Adapted from Module 4 - Processes; Exploration: Environment; Example 
*     https://canvas.oregonstate.edu/courses/1884946/pages/exploration-environment?module_item_id=21835975
*/
void changeDir(struct commandLine* currCommand) {

	// If no argument is passed, set the current working directory to HOME
	if (currCommand->arguments[0] == NULL) {
		chdir(getenv("HOME"));

	}
	// If an argument is passed, change the current working directory to the path in the argument
	else {
		chdir(currCommand->arguments[0]);

	}
}

/*
* Function checks the exit status or termination signal of the last foreground process run by the shell. Takes in the 
* exitStatus variable.
* 
* Citation: Adapted from Module 4 - Processes; Exploration: Process API - Monitoring Child Processes; Example in Interpreting the Termination Status section
*     https://canvas.oregonstate.edu/courses/1884946/pages/exploration-process-api-monitoring-child-processes?module_item_id=21835973
*/
void checkStatus(int exitStatus) {

	// Variable used for printing exit status/signal
	int pExitStatus;

	// Checks to see if the process terminated normally
	if (WIFEXITED(exitStatus) != 0) {

		// If the process terminated normally, get the exit status value
		pExitStatus = WEXITSTATUS(exitStatus);
		printf("exit value %d\n", pExitStatus);
		fflush(stdout);
	}

	// Otherwise, the process terminated abnormally
	else if (WIFSIGNALED(exitStatus) != 0) {

		// Get the signal number that caused the child to terminate abnormally
		pExitStatus = WTERMSIG(exitStatus);
		printf("terminated by signal %d\n", pExitStatus);
		fflush(stdout);
	}
	else {
		printf("%d\n", 0);
		fflush(stdout);
	}
}

/*
* Preprocess redirection: Open files necessary for the redirections.
* 
* Citation: Adapted from Module 5 - Processes II; Exploration: Processes and I/O; Example: Redirecting both Stdin and Stdout
*     https://canvas.oregonstate.edu/courses/1884946/pages/exploration-processes-and-i-slash-o?module_item_id=21835982
*/
void procRedirect(struct commandLine* currCommand, int index) {

	int redirectFrom;
	int redirectTo;
	int result;

	// For maintaining context between srttok calls
	char* saveptr;

	// First token is the direction of the redirect
	char* token = strtok_r(currCommand->redirection[index], " ", &saveptr);

	if (token[0] == '<') {
		token = strtok_r(NULL, " , '\n'", &saveptr);
		
		// Open file that will be the source
		redirectFrom = open(token, O_RDONLY);
		if (redirectFrom == -1) {
			printf("cannot open %s for input\n", token);
			exit(1);
		}

		// Redirect stdin to the source file
		result = dup2(redirectFrom, 0);
		if (result == -1) {
			printf("source redirection failed");
			exit(2);
		}
	}
	else {
		token = strtok_r(NULL, " , '\n'", &saveptr);

		// Open file that will be the destination
		redirectTo = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (redirectTo == -1) {
			printf("cannot open %s for output\n", token);
			exit(1);
		}

		// Redirect stdout to the destination file
		result = dup2(redirectTo, 1);
		if (result == -1) {
			printf("target redirection failed");
			exit(2);
		}
	}
}

/*
* Function performs default redirection for background processes. Will use dev/null for both input and output. Will be run before any 
* redirection specified in the command so that it can be overwritten if necessary. Takes in the current command as an argument.
* 
* Citation: Adapted from Module 5 - Processes II; Exploration: Processes and I/O; Example: Redirecting both Stdin and Stdout
*     https://canvas.oregonstate.edu/courses/1884946/pages/exploration-processes-and-i-slash-o?module_item_id=21835982
*/
void bgRedirect(struct commandLine* currCommand) {
	// Open file that will be the source
	int redirectFrom = open("/dev/null", O_RDONLY);
	if (redirectFrom == -1) {
		printf("source open failed");
		exit(1);
	}

	// Redirect stdin to the source file
	int result = dup2(redirectFrom, 0);
	if (result == -1) {
		printf("source dup failed");
		exit(2);
	}

	// Open file that will be the destination
	int redirectTo = open("/dev/null", O_WRONLY);
	if (redirectTo == -1) {
		printf("target open failed");
		exit(1);
	}

	// Redirect stdout to the destination file
	result = dup2(redirectTo, 1);
	if (result == -1) {
		printf("target dup2 failed");
		exit(2);
	}
}

/*
* Function adds pid to the list of jobs running in the background so that periodic checks can be done on completed
* background jobs. Function takes in the pid that needs to be added along with the head of the list. 
*/
void addToBgList(int spawnpid, struct bgPid* bgList) {

	// Iterate through the linked list to add the child pid to the first available node
	while (bgList != NULL) {

		// If an empty node is found, add the child pid to the node and exit the loop
		if (bgList->backgroundPid == '\0') {
			bgList->backgroundPid = spawnpid;
			return;
		}

		// If the end of the list is found without finding any empty nodes, create a new node for the child pid
		if (bgList->next == NULL) {
			struct bgPid* currChild = malloc(sizeof(struct bgPid));

			currChild->backgroundPid = spawnpid;
			currChild->next = NULL;
			bgList->next = currChild;
			return;
		}

		bgList = bgList->next;
	}

}

/*
* Free the linked list of background child pids. Takes in the head of the linked list as an argument and frees each node of the list.
* 
* Citation: Adapted on 2/1/2022 from Canvas post from Professor Gambord:
* https://edstem.org/us/courses/16718/discussion/1020375
*/
void freeBgPidList(struct bgPid* bgList) {
	if (bgList != NULL) {
		freeBgPidList(bgList->next);
		free(bgList);
	}
}

/*
* Free the current command line. Takes in the command line as an argument.
* 
* Citation: Adapted on 2/1/2022 from Canvas post from Professor Gambord:
* https://edstem.org/us/courses/16718/discussion/1020375
*/
void freeCurrCommand(struct commandLine* currCommand) {
	if (currCommand != NULL) {

		int i;

		free(currCommand->command);

		// Iterate through the memory allocated for arguments and free memory used for any individual arguments
		for (i = 0; i < MAXARG; i++) {
			if (currCommand->arguments[i] != '\0') {
				free(currCommand->arguments[i]);
			}
		}

		// Iterate through the memory allocated for the extended arguments array and free memory used for any individual arguments
		for (i = 0; i < MAXARG + 2; i++) {
			if (currCommand->extendArgs[i] != '\0') {
				free(currCommand->extendArgs[i]);
			}
		}

		// Iterate through the memory allocated for redirection and free memory used for any individual redirections
		for (i = 0; i < 2; i++) {
			if (currCommand->redirection[i] != '\0') {
				free(currCommand->redirection[i]);
			}
		}
		
		// Free the memory allocated for the entire commandLine struct
		free(currCommand);
	}
}

/*
* Execute commands that are not built-in using fork(), exec() and waitpid(). Function takes in a commandLine struct as
* an argument.
* 
* Citation 1: Adapted from Module 4 - Processes; Exploration: Process API - Executing a New Program.
*     https://canvas.oregonstate.edu/courses/1884946/pages/exploration-process-api-executing-a-new-program?module_item_id=21835974
* Citation 2: Default action adapted from Module 4 - Processes; Exploration: Process API - Monitoring Child Processes
*     https://canvas.oregonstate.edu/courses/1884946/pages/exploration-process-api-monitoring-child-processes?module_item_id=21835973
* Citation 3: sigprocmask technique adapted on 2/1/222 from: Kerrisk, Michael. “Chapter 20.” The Linux Programming Interface a Linux Und UNIX System Programming Handbook,
*     No Starch Press, San Francisco, CA, 2018, p. 410-411.
*/
int otherCommand(struct commandLine* currCommand, struct bgPid* bgList) {

	sigset_t blockSIGTSTP;
	sigset_t prevMask;
	
	// Have the child process ignore SIGTSTP if not in foreground-only mode
	if (fgOnly == 1) {
		sigemptyset(&blockSIGTSTP);
		sigaddset(&blockSIGTSTP, SIGTSTP);

		sigprocmask(SIG_BLOCK, &blockSIGTSTP, &prevMask);
	}
	
	// Create a holder for the head of the linked list. Used for resetting pointer to beginning of the list
	struct bgPid* head = bgList;
	
	// Create variables for holding the child status for use during waitpid()
	int childStatus;
	int bgChildStatus;

	// Initialize spawnpid with an arbitrary id
	pid_t spawnpid = -5;

	int childDone;

	// fork() a new child process
	spawnpid = fork();

	switch (spawnpid) {

		// If fork() fails, set the exit status to -1
	case -1:
		perror("fork()\n");
		childStatus = -1;
		exit(-1);
		break;

		// If the fork succeeds, execute the command in the child process
	case 0:

		// Allow SIGINT to terminate process if it is running in the foreground
		if (currCommand->background == 0) {
			changeSIGINT();
		}

		if (fgOnly == 0) {
			initSIGTSTP();
		}

		// Check to see if the process should be run in the background
		if (currCommand->background == 1) {
			bgRedirect(currCommand);
		}

		// Check for redirection
		if (currCommand->redirection[0] != NULL) {
			procRedirect(currCommand, 0);
		}

		// Check for a second redirection
		if (currCommand->redirection[1] != NULL) {
			procRedirect(currCommand, 1);
		}

		// Replace the current program with the command program
		execvp(currCommand->command, currCommand->extendArgs);

		// execvp only returns if there's an error
		printf("%s: no such file or directory\n", currCommand->command);
		childStatus = 1;
		exit(1);
		break;

		// Have the parent process wait for the child process to complete
	default:

		// Reset pointer to the head of the linked list
		bgList = head;

		// Check the pids running in the background to see if any have completed
		while (bgList != NULL) {

			// If the node holds a pid, check it to see if the job has finished and clean up
			if (bgList->backgroundPid != '\0') {
				childDone = waitpid(bgList->backgroundPid, &bgChildStatus, WNOHANG);

				// If the child pid has finished, print message and remove it from linked list
				if (childDone != 0) {
					printf("background pid %d is done: ", bgList->backgroundPid);
					checkStatus(bgChildStatus);
					bgList->backgroundPid = '\0';
				}
			}

			bgList = bgList->next;
		}

		// Reset pointer to the head of the linked list
		bgList = head;

		// Check if the current command should be run in the background and that the process is NOT in foreground-only mode
		if (currCommand->background == 1 && fgOnly == 0) {


			// If it should, print message and run waitpid with WNOHANG so the process can run in the background
			printf("background pid is %d\n", spawnpid);
			fflush(stdout);
			childDone = waitpid(spawnpid, &bgChildStatus, WNOHANG);

			// If the child pid has finished, print message and remove it from linked list
			if (childDone != 0) {
				printf("background pid %d is done: ", spawnpid);
				checkStatus(bgChildStatus);
			}
			else {
				// Otherwise, add the child pid to the list of jobs running in the background
				addToBgList(spawnpid, bgList);
			}
		}

		// If the process should be run in the foreground or if process IS in foreground-only mode, wait to prompt user until process is complete
		if (currCommand->background == 0 || fgOnly == 1) {

			waitpid(spawnpid, &childStatus, 0);

			// Check to see if the process was terminated by SIGINT. 
			if (childStatus == 2) {

				// If it was, print message
				printf("terminated by signal %d\n", childStatus);
				fflush(stdout);
			}

			if (fgOnly == 1) {
				sigprocmask(SIG_SETMASK, &prevMask, NULL);
			}
		}

		break;
	}

	// Return the child status so it can be used when checking the status
	return childStatus;
}

/*====================== main function =======================================================================================================================*/


/*
* Function to control the flow of the program. Initializes necessary variables and starts a loop to continue prompting the user for commands until an exit
* command is recieved.
*/
int main(void) {

	// Ignore SIGINT signal. This will later be changed for child processes running in the foreground
	initSIGINT();

	// Set arbitrary int to keep shell running until terminated.
	int runSmallsh = -5;

	// Variable to hold the exit status of the last foreground process run by the shell
	int exitStatus = 0;

	// Variable for tracking whether a valis comman has been entered
	int isCommand;

	// Initialize the linked list that will be used for storing pids running in the background
	struct bgPid* head = malloc(sizeof(struct bgPid));
	head->backgroundPid = '\0';
	head->next = NULL;

	while (runSmallsh == -5){

		// Give commandLine a size of MAXCOMM. This makes the program easy to update if MAXCOMM ever needs to change
		char* commandLine = malloc(sizeof(char) * MAXCOMM);

		// Set SIGTSTP to enter/exit foreground-only mode
		changeSIGTSTP();

		isCommand = 0;

		// Prompt the user for command
		isCommand = promptUser(commandLine);

		// Perform any necessary expansions
		varExpansion(commandLine);

		// If a command was entered, process it
		if (isCommand == 1) {
			struct commandLine* currCommand = processComm(commandLine);

			// If the user entered the 'exit' command, call the exitCheck function
			if (strcmp(currCommand->command, "exit") == 0) {
				runSmallsh = exitCheck(head);
			}

			// If the user entered the 'cd' command, call the changeDir function
			else if (strcmp(currCommand->command, "cd") == 0) {
				changeDir(currCommand);
			}

			// If the user entered the 'status' command, call the checkStatus function
			else if (strcmp(currCommand->command, "status") == 0) {
				checkStatus(exitStatus);
			}
			// Otherwise use fork(), exec(), and waitpid() to execute other commands
			else {
				exitStatus = otherCommand(currCommand, head);
			}

			// Free the memory allocated for the command
			freeCurrCommand(currCommand);

		}

		free(commandLine);

	}

	// Free the list of pids running in the background
	freeBgPidList(head);
	
	return EXIT_SUCCESS;
}