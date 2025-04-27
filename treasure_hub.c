#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_COMMAND 256
#define MAX_HUNT_ID 512
#define COMMAND_FILE "monitor_command.txt"
#define RESPONSE_FILE "monitor_response.txt"

// Global variables
pid_t monitor_pid = 0;
int monitor_running = 0;

// Signal handler for SIGCHLD
void handle_sigchld(int signum)
{
    int status;
    pid_t pid = waitpid(monitor_pid, &status, WNOHANG);
    if (pid > 0)
    {
        monitor_running = 0;
        printf("Monitor process terminated with status: %d\n", WEXITSTATUS(status));
    }
}

// Signal handler for SIGUSR1 (response from monitor)
void handle_sigusr1(int signum)
{
    // Read and display response from monitor
    FILE *response_file = fopen(RESPONSE_FILE, "r");
    if (response_file)
    {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), response_file))
        {
            printf("%s", buffer);
        }
        fclose(response_file);
    }
}

// Function to start the monitor process
void start_monitor()
{
    if (monitor_running)
    {
        printf("Monitor is already running\n");
        return;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork failed");
        return;
    }

    if (pid == 0)
    {
        // Child process - start the monitor
        execl("./treasure_manager", "treasure_manager", "monitor", NULL);
        perror("execl failed");
        exit(1);
    }
    else
    {
        monitor_pid = pid;
        monitor_running = 1;
        printf("Monitor started with PID: %d\n", pid);
    }
}

// Function to send a command to the monitor
void send_command(const char *command)
{
    if (!monitor_running)
    {
        printf("Monitor is not running\n");
        return;
    }

    // Write command to file
    FILE *cmd_file = fopen(COMMAND_FILE, "w");
    if (!cmd_file)
    {
        perror("Failed to open command file");
        return;
    }
    fprintf(cmd_file, "%s\n", command);
    fclose(cmd_file);

    // Send signal to monitor
    if (kill(monitor_pid, SIGUSR1) < 0)
    {
        perror("Failed to send signal to monitor");
        return;
    }
}

// Function to stop the monitor
void stop_monitor()
{
    if (!monitor_running)
    {
        printf("Monitor is not running\n");
        return;
    }

    send_command("stop");
    printf("Waiting for monitor to terminate...\n");
    // Wait for SIGCHLD to be handled
    while (monitor_running)
    {
        sleep(1);
    }
}

int main()
{
    // Set up signal handlers
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) < 0)
    {
        perror("sigaction failed");
        return 1;
    }

    sa.sa_handler = handle_sigusr1;
    if (sigaction(SIGUSR1, &sa, NULL) < 0)
    {
        perror("sigaction failed");
        return 1;
    }

    char command[MAX_COMMAND];
    char hunt_id[MAX_HUNT_ID];
    int treasure_id;

    while (1)
    {
        printf("> ");
        if (fgets(command, sizeof(command), stdin) == NULL)
        {
            break;
        }

        // Remove newline
        command[strcspn(command, "\n")] = 0;

        if (strcmp(command, "start_monitor") == 0)
        {
            start_monitor();
        }
        else if (strcmp(command, "list_hunts") == 0)
        {
            send_command("list_hunts");
        }
        else if (strcmp(command, "list_treasures") == 0)
        {
            printf("Enter hunt ID: ");
            if (fgets(hunt_id, sizeof(hunt_id), stdin))
            {
                hunt_id[strcspn(hunt_id, "\n")] = 0;
                char full_command[MAX_COMMAND + MAX_HUNT_ID];
                snprintf(full_command, sizeof(full_command), "list_treasures %s", hunt_id);
                send_command(full_command);
            }
        }
        else if (strcmp(command, "view_treasure") == 0)
        {
            printf("Enter hunt ID: ");
            if (fgets(hunt_id, sizeof(hunt_id), stdin))
            {
                hunt_id[strcspn(hunt_id, "\n")] = 0;
                printf("Enter treasure ID: ");
                if (scanf("%d", &treasure_id) == 1)
                {
                    char full_command[MAX_COMMAND + MAX_HUNT_ID + 20];
                    snprintf(full_command, sizeof(full_command), "view_treasure %s %d", hunt_id, treasure_id);
                    send_command(full_command);
                }
                // Clear input buffer
                while (getchar() != '\n')
                    ;
            }
        }
        else if (strcmp(command, "stop_monitor") == 0)
        {
            stop_monitor();
        }
        else if (strcmp(command, "exit") == 0)
        {
            if (monitor_running)
            {
                printf("Error: Monitor is still running. Please stop it first.\n");
            }
            else
            {
                break;
            }
        }
        else
        {
            printf("Unknown command\n");
        }
    }

    return 0;
}