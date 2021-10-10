#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAXCHAR 2048
#define MAXARG 512

/* 
Manages background processes
*/
struct bgProcessStack{
    int bgPidCount;         // Num of background processes by PID
    pid_t bgPids[MAXARG];   // For storing PID of each background process
};

/* 
Stores attributes from parsed input 
*/
struct inputAttributes{
    bool activeBackground;      // For keeping track of background processes
    char inputFile[128];        // For storing string to be read from input file 
    char outputFile[128];       // For storing string to be written to output file
    char command[MAXCHAR];      // For storing command string
    int argNum;                 // For counting the num of arguments when a line is parsed
    char *arguments[MAXARG];    // For storing the arguments from after input has been parsed
};

/*
Globals
*/
struct bgProcessStack pidStack;
int fgVal;                       // Last foreground exit status/signal
bool runInForeground = false;       // If foreground commands enabled, set to false by default

/* 
Function declaration
*/
void deleteBgPid(pid_t processId);
int changeDirectory(char* inputBuffer);
bool hasSpecialChar(char *str);
void parseInputStr(char* inputBuffer, struct inputAttributes* obj);
void listOfArgs(struct inputAttributes* obj, char** argsArray);
void handleRedirection(struct inputAttributes* obj);
void forkOff(struct inputAttributes* obj);
void stopSig(int sig);
void childSig(int sig);
void terminateSig(int sig);
void freeInputMem(struct inputAttributes* obj);
void killBgProcess();
void switchModes();

int main(){
    char inputBuffer[MAXCHAR];  // For storing input
    struct inputAttributes *obj;// Instantiate input attributes
    int fgStatus;
    int i;
    char *source;               // Holds source for when pid is parsed
    char *dest;                 // Holds destination for when pid is parsed
    char *assignedPid;          // Holds pid value

    /* 
    A stack for background PIDs
    */
    pidStack.bgPidCount = -1;
    for(i = 0; i < MAXARG; i++){
        pidStack.bgPids[i] = -1;
    }

    /* 
    A struct for initializing stop sig
    */
    struct sigaction StopSignal;
    StopSignal.sa_handler = stopSig;
    StopSignal.sa_flags = 0;

    /* 
    A struct for initializing terminate sig
    */
    struct sigaction TermSignal;
    TermSignal.sa_handler = terminateSig;
    StopSignal.sa_flags = 0;

    /* 
    A struct for initializing child sig
    */
    struct sigaction ChildSignal;
    ChildSignal.sa_handler = childSig;
    StopSignal.sa_flags = 0;

    /* 
    A loop for handling commands & signal handlers
    */  
    do{
        /* 
        Reset the sig handlers
        */
        sigaction(SIGCHLD, &ChildSignal, NULL);
        sigaction(SIGTSTP, &StopSignal, NULL);
        sigaction(SIGINT, &TermSignal, NULL);

        usleep(3000);   // To avoid competition between signals and commands that happen at the same time

        switchModes();   // Switches foreground mode if there is a stop signal

        /* 
        Flushes stdout and stdin; Need to call flush() on stdout after every output so output reaches the screen
        */
        fflush(stdout);
        fflush(stdin);

        /* 
        Print colon symbol as the prompt
        */
        printf(": ");
        memset(inputBuffer, '\0', sizeof(inputBuffer));
        fgets(inputBuffer, sizeof(inputBuffer), stdin); // Fetch user input
        
        /* 
        Flushes buffer to reset stdout/stdin
        */
        fflush(stdout);
        fflush(stdin);

        /* 
        For handling special characters and shell commands
        */
        if(strncmp(inputBuffer, "exit", 4) == 0){ // Recognizes "exit" command and exits from shell.
            killBgProcess();
            exit(0);
        }
        if((assignedPid = strstr(inputBuffer, "testdir$$")) != NULL){       // Expand instance of $$ into pid
            int pidReplacement = (int)getpid();                             // For storing the current pid
            source = malloc(sizeof(char) * 8);                              // Allocate mem for source
            dest = malloc(sizeof(char) * 20);                               // Allocate mem for dest
            strcpy(source, "testdir");                                      // Prep for building name of test dir
            sprintf(dest, "%s%d", source, pidReplacement);                  // Stores concatenated 'testdir' and pid
            char* tempString = malloc(sizeof(char) * strlen(inputBuffer));  // Allocate mem for the str
            *assignedPid = 0;
            strcpy(tempString, inputBuffer);           
            sprintf(inputBuffer, "%s%s", tempString, dest);                 // inputBuffer is replaced with the final str
            free(tempString);                                               // Free up temp string mem
            tempString = NULL;
            strtok(inputBuffer, "$");
        }
        if(strncmp(inputBuffer, "#", 1) == 0){                              // For ignoring the '#' character when user inputs comment
            continue;
        } else if(strncmp(inputBuffer, "\n", 1) == 0){                        // For handling the case where user inputs an empty line
            continue;
        } else if(strncmp(inputBuffer, "cd", 2) == 0){                        // Handles directory change
            changeDirectory(inputBuffer);
        } else if(strncmp(inputBuffer, "status", 6) == 0){                     // Last foreground exit status
            if(WEXITSTATUS(fgVal)){
                fgStatus = WEXITSTATUS(fgVal);                           // See if process has exited
            } else {
                fgStatus = WTERMSIG(fgVal);                              // See if process was terminated by signal
            }
            printf("exit value %d\n", fgStatus);
        } else {
            if(inputBuffer != NULL && strcmp(inputBuffer, "") != 0){
                /* 
                Read input
                */
                obj = malloc(1 * sizeof(struct inputAttributes)); 
                parseInputStr(inputBuffer, obj); // Parse input
                forkOff(obj);                    // handle commands & manage parent/child processes
                freeInputMem(obj);               // Free input mem
            } else {
                continue;                        // Keep looping while true
            }
        }
    } while(true);
    return 0;
}

/*
Remove pid when background process ends
*/
void deleteBgPid(pid_t processId){
    int i;
    int pidPos;                                   // Stores pid position in stack

    /* 
    Find the pid of the bg process that ended in the pid stack
    */
    for(i = 0; i < pidStack.bgPidCount + 1; i++){
        if(pidStack.bgPids[i] == processId){
            pidPos = i;
            break;                                 // Once pid has been located, end loop
        }
    }

    /* 
    Shift order of remaining pids in stack
    */
    for(i = pidPos; i < pidStack.bgPidCount + 1; i++){
        pidStack.bgPids[i] = pidStack.bgPids[i+1];
    }
    
    pidStack.bgPidCount--;                         // Decrement pid count
}


/*
Handles moving between directories
*/
int changeDirectory(char* inputBuffer){
    char* homeDirPath = getenv("HOME");            // Fetch home dir path
    char newDirPath[MAXCHAR];                      // Stores new dir path

    inputBuffer[strlen(inputBuffer) -1] = '\0';

    if(strcmp(inputBuffer,"cd") == 0){
        if(chdir(homeDirPath) != 0){               // If not 0, then directory could not be found
            printf("directory:%s not found.\n", homeDirPath);
            return 1;
        }
        return 0;
    }

    memset(newDirPath, '\0', sizeof(newDirPath));   // Clear str & prepare for new dir path
    strtok(inputBuffer, " ");
    strcpy(inputBuffer, strtok(NULL, ""));
    
    /* 
    Handles commands related to directory
    */
    if(inputBuffer[0] == '/'){
        sprintf(newDirPath, "%s%s", homeDirPath, inputBuffer); // Change to new dir from home dir
    }
    else if(strcmp(inputBuffer, "..") == 0){                   // Go back one dir
        strcpy(newDirPath, inputBuffer);
    }
    else if(strcmp(inputBuffer, "~") == 0){                    // Go to home dir
        strcpy(newDirPath, homeDirPath);
    }
    else if(inputBuffer[0] == '.' && inputBuffer[1] == '/'){   // Stay in current working dir
        sprintf(newDirPath, "%s", inputBuffer);
    }
    else{
        sprintf(newDirPath, "%s", inputBuffer);                // Change to new dir path from home dir path
    }
    if(chdir(newDirPath) != 0){                                // If directory not found
        printf("directory:%s not found.\n", newDirPath);
        return 1;
    }
    return 0;
}


/*
Checks for special chars '< > & # &' and if any are found, returns True
*/
bool hasSpecialChar(char *str){
    bool shellOp = false;

    if(str == NULL){        // handles NULL to prevent segfaut
        return true;
    }
    if(str[0] == '&'){      // Search for bg command
        shellOp = true;
    }
    else if(str[0] == '<'){ // Check for input redirection
        shellOp = true;
    }
    else if(str[0] == '>'){ // Check for output redirection
        shellOp = true;
    }
    else if(str[0] == '#'){ // Check for comment
        shellOp = true;
    }
    return shellOp;
}

/*
For initializing inputAttributes struct
*/
void parseInputStr(char* inputBuffer, struct inputAttributes* obj){
    char dataBuffer[MAXCHAR]; // temp buffer to store what is in inputBuffer
    char *inputFileName;      // Stores input file descriptor
    char *outputFileName;     // Stores output file descriptor
    char *temp;               // Stores the argument from data buffer

    obj->argNum = 0;
    inputBuffer[strlen(inputBuffer) -1] = '\0';     // Remove newline char

    if(inputBuffer[strlen(inputBuffer) -1] == '&'){ // Check if bg mode
        obj->activeBackground = true;               // Bg mode is on
        inputBuffer[strlen(inputBuffer) -1] = '\0'; // Ignore and remove '&'
    }
    else{
        obj->activeBackground = false;              // Bg mode not on
    }

    /* 
    Parse input for command and store in array
    */
    memset(dataBuffer, '\0', sizeof(dataBuffer));   // Clear dataBuffer
    strcpy(dataBuffer, inputBuffer);                // Copy inputBuffer -> dataBuffer
    strtok(dataBuffer, " ");                        // Fetch just the commands and copy into new command obj
    strcpy(obj->command, dataBuffer);

    /* 
    Parse name of input file
    */
    memset(dataBuffer, '\0', sizeof(dataBuffer));
    strcpy(dataBuffer, inputBuffer);
    inputFileName = strstr(dataBuffer, "<");                                // Find substr after '<' char.
    if(inputFileName != NULL){
        memmove(inputFileName, inputFileName+2, strlen(inputFileName));     // Copy the mem block of the str, but not the operator
        strtok(inputFileName, " ");                                         // Removes space from input file name
        inputFileName[strlen(inputFileName)] = '\0';                        // Add null character at end of str
        strcpy(obj->inputFile, inputFileName);                              // Copy str -> input file obj
    }

    /* 
    Parse name of output file
    */
    memset(dataBuffer, '\0', sizeof(dataBuffer));
    strcpy(dataBuffer, inputBuffer);
    outputFileName = strstr(dataBuffer, ">");       // Find substr after '>' char.
    if(outputFileName != NULL){
        memmove(outputFileName, outputFileName+2, strlen(outputFileName));
        strtok(outputFileName, " ");
        outputFileName[strlen(outputFileName)] = '\0';
        strcpy(obj->outputFile, outputFileName);    // Copy str -> output file obj
    }
     
    /* 
    Parse arguments
    */
    memset(dataBuffer, '\0', sizeof(dataBuffer));
    strcpy(dataBuffer, inputBuffer);
    strtok(dataBuffer, " ");

    temp = strtok(NULL, "");

    if(hasSpecialChar(temp) == false){                      // Check for any special chars/arguments
        strcpy(dataBuffer, temp);                           // If found, Copy argument -> dataBuffer
        strtok(dataBuffer, "<>#");                          // Take special chars before any aguments
        
        strtok(dataBuffer, " ");                            // Remove space from the argument str
        obj->arguments[0] = dataBuffer;                     // Assign what is in dataBuffer as first argument
        obj->argNum = 1;                                    // First argument inputted
        temp = strtok(NULL, " ");                           // Prep for locating any other arguments in input
        while(temp != NULL){
            obj->arguments[obj->argNum] = temp;             // Store rest of arguments and increment argument counter
            obj->argNum++;
            temp = strtok(NULL, " ");
        }
        obj->arguments[obj->argNum] = strtok(NULL, "");     // Fetch final argument
    }
}

/*
Create list of args to pass to execvp
*/
void listOfArgs(struct inputAttributes* obj, char** argsArray){
    int i;

    argsArray[0] = obj->command;                            // Store command as the first argument
    for(i = 0; i < obj->argNum ; i++){                      // Loop through argument array
        if(getenv(obj->arguments[i]) != NULL){              // If environment variable not null
            argsArray[i+1] = getenv(obj->arguments[i]);     // Add it to list as argument
        }
        else if(strcmp(obj->arguments[i], "$$") == 0){      // Checks if '$$' chars needs to be expanded
            sprintf(argsArray[i+1], "%d", getpid());        // Expand '$$' chars into pid
        }
        else{
            argsArray[i+1] = (obj->arguments[i]);           // Curr argument obj gets added to the list as argument
        }
    }
    argsArray[i+1] = NULL;                                  // Close argument array
}

/*
For redirecting input and output
*/
void handleRedirection(struct inputAttributes* obj){
    int inputFileDescriptor = STDIN_FILENO;
    int outputFileDescriptor = STDOUT_FILENO;

    if(obj->inputFile[0] != '\0'){                           // Make sure input file is not empty, then open it for read only
        inputFileDescriptor = open(obj->inputFile, O_RDONLY);

        if(inputFileDescriptor < 0){                         // If error opening file, print err message
            printf("cannot open %s for input\n", obj->inputFile);
            exit(1);
        }
        dup2(inputFileDescriptor, 0);                        // Call dup2() for input redirection
        close(inputFileDescriptor);                          // Close the file descriptor
    }
    if(obj->outputFile[0] != '\0'){                          // If output file not empty, open for creating/truncating and assign permissions
        outputFileDescriptor = open(obj->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if(outputFileDescriptor < 0){                        // If error opening output file, print err message
            printf("error opening or creating file\n");
            exit(1);
        }
        dup2(outputFileDescriptor, 1);                       // Call dup2() for output redirection
        close(outputFileDescriptor);                         // Close the file descriptor
    }
}

/*
Fork off child process
*/
void forkOff(struct inputAttributes* obj){
    pid_t pid = fork();
    pid_t topOfBgPid;                                                           // Top of background pid stack
    char *argList[MAXARG];
    int procVal;

    switch(pid){
        // if -1, then an error has occured when forking
        case -1:
            printf("error when forking\n");
            exit(1);
            break;

        // If 0, then we have a child process
        case 0:
            handleRedirection(obj);
            listOfArgs(obj, argList);                                           // Create list of arguments with obj
            execvp(obj->command, argList);                                      // Replace the current process with obj command
            printf("%s: no such file or directory\n", argList[0]);
            exit(1);
            break;

        // we have a parent process
        default:
            if(obj->activeBackground == true && runInForeground == false){      // If in bg mode
                // Add pid of bg process to pid stack
                pidStack.bgPids[++(pidStack.bgPidCount)] = pid;
                // Fetch and return pid at top of stack
                topOfBgPid = pidStack.bgPids[pidStack.bgPidCount];
                printf("background pid is %d\n", topOfBgPid);
            }
            else{
                // Wait for child process to end if bg mode not on
                waitpid(pid, &procVal, 0);
                fgVal = procVal;
            }
            break;
    }
}

/*
Stop signal to switch back and forth between foreground mode
*/
void stopSig(int sig){  
    if(runInForeground == false){ // If not already in foreground mode
        char* message = ("\nEntering foreground-only mode (& is now ignored)\n");
        write(STDOUT_FILENO, message, 50);
        runInForeground = true;   // Status of foreground mode is switched to on/true
    }
    else{
        // For exiting foreground mode
        char* message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 31);
        runInForeground = false;  // Status of foreground mode is switche back to off/false
    }
}

/*
Handles ending of child process
*/
void childSig(int sig){
    pid_t cPid;
    int cStatus;
    int i;
    // Look for pid of exited/terminated process in stack
    for(i = 0; i < pidStack.bgPidCount + 1; i++){
        cPid = waitpid(pidStack.bgPids[i], &cStatus, WNOHANG);         // Find pid of child

        if((cStatus == 0 || cStatus == 1) && cPid != 0 ){              // If process exited
            fprintf(stdout, "\nBackground pid %d is done: exit value %d\n", cPid,cStatus);
            deleteBgPid(cPid);                                         // Removes child's pid from stack
        }
        else if(cPid != 0){                                            // If process terminated
            fprintf(stdout, "\nBackground pid %d is done: terminated by signal %d\n", cPid, cStatus);
            deleteBgPid(cPid);
        }
    }
}

/*
Handles termination of process
*/
void terminateSig(int sig){
    printf("\nterminated by signal %d\n", sig);                         // Outputs signal that terminated process
}

/*
Frees input obj memory
*/
void freeInputMem(struct inputAttributes* obj)
{
    obj->activeBackground = false;                                      // Restore bg mode status to default

    // clear inputAttributes
    memset(obj->inputFile, '\0', sizeof(obj->inputFile));
    memset(obj->outputFile, '\0', sizeof(obj->outputFile));
    memset(obj->command, '\0', sizeof(obj->command));

    free(obj);
}

/*
Kill all bg processes and exit
*/
void killBgProcess(){
    int i;
    // loop through pids in stack
    for(i = 0; i < pidStack.bgPidCount + 1; i++){
        kill(pidStack.bgPids[i], SIGINT);           // kill process
        usleep(2000);                               // To avoid competition between signals and commands that happen at the same time
    }
}

/*
Switch foreground mode if there is signal
*/
void switchModes(){
    // There is a stop signal, so exit from foreground-only mode if already in it
    if(WTERMSIG(fgVal) == 11 && runInForeground == true){

        printf("\nExiting foreground-only mode\n");
        runInForeground = false;                     // Switch foreground-only status to off/false
    }

    // There is a stop signal, so enter foreground-only mode if not already in it
    else if(WTERMSIG(fgVal) == 11 && runInForeground == false){

        printf("\nEntering foreground-only mode (& is now ignored)\n");
        runInForeground = true;                      // Switch foreground-only status to on/true
    }
}
