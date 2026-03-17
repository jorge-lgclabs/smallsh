#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

// Global defines
#define INPUT_LENGTH 2048
#define MAX_ARGS	 512
#define MAX_PIDS     20

// Struct definitions
struct command_line {
    char *argv[MAX_ARGS + 1];
    int argc;
    char *input_file;
    char *output_file;
    bool is_bg;
};

// Global variables
int LAST_STATUS = 0;
int FG_FLAG = 0;
pid_t PID_STORE[MAX_PIDS];

struct command_line *parse_input();
void init_pid_store();
void add_pid(pid_t new_pid);
void remove_pid(pid_t old_pid);
void reap_pids();
void wait_and_reap(pid_t child_pid, int retry_count);
void reap_nohang(pid_t child_pid);
void set_ignore_SIGINT();
void set_custom_SIGINT();
void handle_SIGINT(int signo);
void set_ignore_SIGTSTP();
void set_custom_SIGTSTP();
void handle_SIGTSTP(int signo);
void print_command(struct command_line *curr_command);
int ignore_command(struct command_line *curr_command);
int process_command(struct command_line *curr_command);
int execute_command_foreground(struct command_line *curr_command);
int execute_command_background(struct command_line *curr_command);
int builtin_exit();
int builtin_status();
int builtin_cd(struct command_line *curr_command);

int main(void) {
    set_ignore_SIGINT();
    set_custom_SIGTSTP();
    init_pid_store();
    while(true)
    {
        fflush(stdout);
        reap_pids();
        struct command_line *curr_command = parse_input();
        if (!ignore_command(curr_command)) {
            process_command(curr_command);
        }
    }
    return EXIT_SUCCESS;
}
// Initializes the array which stores the PIDs
void init_pid_store() {
    for (int i = 0; i < MAX_PIDS; i++) {
        PID_STORE[i] = -1;
    }
}
// Function to add a PID to the PID storage
void add_pid(pid_t new_pid) {
    for (int i = 0; i < MAX_PIDS; i++) {
        if (PID_STORE[i] == -1) {
            PID_STORE[i] = new_pid;
            return;
        }
    }
    printf("too many PIDs");
    exit(EXIT_FAILURE);
}
// Function to remove a PID from the PID storage
void remove_pid(pid_t old_pid) {
    for (int i = 0; i < MAX_PIDS; i++) {
        if (PID_STORE[i] == old_pid) {
            PID_STORE[i] = -1;
            return;
        }
    }
    printf("PID not found\n");
}
// Function to reap all potentially zombified child processes
void reap_pids() {
    for (int i = 0; i < MAX_PIDS; i++) {
        if (PID_STORE[i] != -1) {
            reap_nohang(PID_STORE[i]);
        }
    }
}
// Function which waits for a child process to end and sets the exit status
void wait_and_reap(pid_t child_pid, int retry_count) {
    int exit_status;
    int result = waitpid(child_pid, &exit_status, 0);
    if (result == -1) {  // if waitpid had an error not caused by child SIGINT
        //printf("waitpid error. here is the state of FG_FLAG: %d\n", FG_FLAG);
        perror("waitpid");
    }
    else {
        if (exit_status == 256) {
            LAST_STATUS = 1;
        }
        else if (exit_status == 2) {
            LAST_STATUS = 2;
            builtin_status();
        }
        else {
            LAST_STATUS = 0;
        }
    }
}
// Function which attempts to reap child process but will not wait for it, removes PID from storage upon success
void reap_nohang(pid_t child_pid) {
    int exit_status;
    int result = waitpid(child_pid, &exit_status, WNOHANG);
    //printf("result: %d\nexit_status: %d\n", result, exit_status);
    switch (result) {
        case -1:
            perror("waitpid");
            break;
        case 0:
            return;
        default:
            remove_pid(child_pid);
            printf("background pid %d is done: ", child_pid);
            if ((exit_status == 0) || (exit_status == 1)) {
                LAST_STATUS = exit_status;
                printf("exit status %d\n", exit_status);
            }
            else {
                LAST_STATUS = exit_status;
                printf("terminated by signal %d\n", exit_status);
            }
    }
}
// Signal handler for custom SIGINT
void handle_SIGINT(int signo){
    exit(2);
}
// Sets the calling process to ignore SIGINT
void set_ignore_SIGINT() {
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);
}
// Sets the calling process to handle SIGINT with custom handler
void set_custom_SIGINT() {
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = handle_SIGINT;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);
}
// Custom handler for SIGTSTP (0 = allow background commands, 1 = disallow background commands)
void handle_SIGTSTP(int signo) {
    char *turn_on_message = "Entering foreground-only mode (& is now ignored)\n";
    char *turn_off_message = "Exiting foreground-only mode\n";
    if (FG_FLAG == 0) {  // toggle from on to off
        FG_FLAG = 1;
        write(STDOUT_FILENO, turn_on_message,49);
    }
    else {
        FG_FLAG = 0;
        write(STDOUT_FILENO, turn_off_message,29);
    }
}
// Sets the calling process to ignore SIGTSTP
void set_ignore_SIGTSTP() {
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = SIG_IGN;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = 0;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}
// Sets the calling process to handle custom SIGTSTP
void set_custom_SIGTSTP() {
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}
// Function which parses the users input (adapted from assignment page)
struct command_line *parse_input() {
    char input[INPUT_LENGTH];
    struct command_line *curr_command = (struct command_line *) calloc(1, sizeof(struct command_line));

    // Get input
    printf(": ");
    fflush(stdout);
    if (fgets(input, INPUT_LENGTH, stdin) == NULL) { // to deal with interrupts from SIGTSTP
        curr_command->argc = 0;  // return the equivalent of a "blank line" so it is ignored
        return curr_command;
    }

    // Tokenize the input
    char *token = strtok(input, " \n");
    while(token){
        if(!strcmp(token,"<")){
            curr_command->input_file = strdup(strtok(NULL," \n"));
        } else if(!strcmp(token,">")){
            curr_command->output_file = strdup(strtok(NULL," \n"));
        } else if(!strcmp(token,"&")){
            curr_command->is_bg = true;
        } else{
            curr_command->argv[curr_command->argc++] = strdup(token);
        }
        token=strtok(NULL," \n");
    }
    return curr_command;
}
// Function to print out received commands, for debugging
void print_command(struct command_line *curr_command) {
    printf("******* -- INPUT -- *******\n");
    printf("argc: %i\n", curr_command->argc);
    for (int i = 0; i < curr_command->argc; i++) {
        printf("argv[%i] = %s\n", i, curr_command->argv[i]);
    }
    printf("input_file: %s\n", curr_command->input_file);
    printf("output_file: %s\n", curr_command->output_file);
    printf("bg: %hhd\n", curr_command->is_bg);
    printf("******* -- OUTPUT -- ******\n");
    fflush(stdout);
}
// Function to let the main loop know whether to ignore the command or not
int ignore_command(struct command_line *curr_command) {
    // test for blank line
    if (curr_command->argc == 0) {
        return 1;  // True, ignore command
    }
    // test for comment line
    char first_char[1];
    first_char[0] = curr_command->argv[0][0];
    if (!strcmp(first_char, "#")) {
        return 1;  // True, ignore command
    }
    return 0;  // default False, no commands to ignore
}
// Function serves as first gate in processing a valid command
int process_command(struct command_line *curr_command) {
    char *first_command = curr_command->argv[0];

    if (!strcmp(first_command, "exit")) {  // built-in exit command
        return builtin_exit();
    }
    if (!strcmp(first_command, "status")) {  // built-in status command
        return builtin_status();
    }
    if (!strcmp(first_command, "cd")) {  // built-in cd command
        return builtin_cd(curr_command);
    }
    if ((curr_command->is_bg) && (FG_FLAG == 0)) {
        //printf("background process triggered, FG_FLAG: %d\n", FG_FLAG);
        return execute_command_background(curr_command);  // execute normal command was background
    }
    //printf("foreground process triggered, FG_FLAG: %d\n", FG_FLAG);
    return execute_command_foreground(curr_command);  // execute normal command was foreground
}
// Function which handles the built-in 'exit' command
int builtin_exit() {
    int exit_status;
    for (int i = 0; i < MAX_PIDS; i++) {
        if (PID_STORE[i] != -1) {
            int killed = kill(PID_STORE[i], SIGKILL);
            if (!killed) {
                //printf("killed pid %i\n", PID_STORE[i]);
                wait_and_reap(PID_STORE[i], 0);
            }
            else {
                perror("kill failed");
            }
        }
    }
    exit(EXIT_SUCCESS);
}
// Function which handles the built-in 'status' command
int builtin_status() {
    if (LAST_STATUS == 2) {
        printf("terminated by signal %d\n", LAST_STATUS);
        fflush(stdout);
        return 0;
    }
    printf("exit value %d\n", LAST_STATUS);
    fflush(stdout);
    return 0;
}
// Function which handles the built-in 'cd' command
int builtin_cd(struct command_line *curr_command) {
    // cd with no directory specified
    if (curr_command->argc == 1) {
        chdir(getenv("HOME"));
    }
    // cd into directory sent as arguement
    else {
        chdir(curr_command->argv[1]);
    }
    return 0;
}
// Function which executes normal, non-built-in commands in the foreground
int execute_command_foreground(struct command_line *curr_command) {
    char *newargv[curr_command->argc + 1];
    for (int i = 0; i < curr_command->argc + 1; i++) {
        if (i == curr_command->argc) {
            newargv[i] = NULL;
        }
        else {
            newargv[i] = strdup(curr_command->argv[i]);
        }
    }
    pid_t spawn_pid = fork();
    switch (spawn_pid) {
        case -1:  //error
            printf("fork failed\n");
            return EXIT_FAILURE;
        case 0:  // child process executes here
            set_custom_SIGINT(); // set custom SIGINT behavior for child
            set_ignore_SIGTSTP(); // set to ignore SIGTSTP
            // check for input direction
            if (curr_command->input_file) {
                int fd_in = open(curr_command->input_file, O_RDONLY);
                if (fd_in == -1) {
                    printf("cannot open %s for input\n", curr_command->input_file);
                    LAST_STATUS = 1;
                    exit(1);
                }
                dup2(fd_in, STDIN_FILENO);
            }
            // check for output direction
            if (curr_command->output_file) {
                int fd_out = open(curr_command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_out == -1) {
                    printf("cannot open %s for input\n", curr_command->output_file);
                    LAST_STATUS = 1;
                    exit(1);
                }
                dup2(fd_out, STDOUT_FILENO);
            }
            int result = execvp(curr_command->argv[0], newargv);
            if (result == -1) {
                printf("%s: %s\n",curr_command->argv[0], strerror(errno));
                exit(1);
            }
            break;
        default:
            wait_and_reap(spawn_pid, 0);
            fflush(stdout);
            break;
    }
    return 0;
}
// Function which executes normal, non-built-in commands in the background
int execute_command_background(struct command_line *curr_command) {
    //printf("execute command background\n");
    char *newargv[curr_command->argc + 1];
    for (int i = 0; i < curr_command->argc + 1; i++) {
        if (i == curr_command->argc) {
            newargv[i] = NULL;
        }
        else {
            newargv[i] = strdup(curr_command->argv[i]);
        }
    }
    pid_t spawn_pid = fork();
    switch (spawn_pid) {
        case -1:  //error
            printf("fork failed\n");
            return EXIT_FAILURE;
        case 0:  // child process executes here
            set_ignore_SIGTSTP(); // set to ignore SIGTSTP
            int fd_in;
            int fd_out;
            // if no input direction specified, redirect to null
            if (!curr_command->input_file) {
                fd_in = open("/dev/null", O_RDONLY);
            }
            else {
                fd_in = open(curr_command->input_file, O_RDONLY);  // otherwise direct input
                if (fd_in == -1) {
                    printf("cannot open %s for input\n", curr_command->input_file);
                    LAST_STATUS = 1;
                    return 1;
                }
            }
            // if no output direction specified, redirect to null
            if (!curr_command->output_file) {
                fd_out = open("/dev/null", O_WRONLY);
            }
            else {
                fd_out = open(curr_command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);  // otherwise direct output
                if (fd_out == -1) {
                    printf("cannot open %s for output\n", curr_command->output_file);
                    LAST_STATUS = 1;
                    return 1;
                }
            }
            dup2(fd_in, STDIN_FILENO);
            dup2(fd_out, STDOUT_FILENO);
            if (execvp(curr_command->argv[0], newargv) == -1) {
                exit(1);
            };
            break;
        default:
            add_pid(spawn_pid);
            printf("background pid is %d\n", spawn_pid);
            break;
    }
    return 0;
}


