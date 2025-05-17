#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#define MAX_COMMAND 256
#define MAX_HUNT_ID 512
#define MAX_STRING 512
#define PIPE_BUF_SIZE 4096

// Global variables
pid_t monitor_pid = 0;
int monitor_running = 0;
int pipe_to_monitor[2];   // Parent writes to monitor
int pipe_from_monitor[2]; // Parent reads from monitor
volatile sig_atomic_t response_received = 0;
volatile sig_atomic_t command_in_progress = 0;

// Signal handler for SIGCHLD
void handle_sigchld(int signum)
{
    int status;
    pid_t pid = waitpid(monitor_pid, &status, WNOHANG);
    if (pid > 0)
    {
        monitor_running = 0;
        printf("\nMonitor process terminated with status: %d\n", WEXITSTATUS(status));
        close(pipe_to_monitor[1]);
        close(pipe_from_monitor[0]);
    }
}

// Signal handler for SIGUSR1 (response ready from monitor)
void handle_sigusr1(int signum)
{
    response_received = 1;
}

// Function to read response from monitor
void read_monitor_response()
{
    char buffer[PIPE_BUF_SIZE];
    ssize_t bytes_read;
    int retries = 10; // Increased number of retries
    int total_bytes = 0;
    int got_initial_response = 0;

    while (retries > 0)
    {
        bytes_read = read(pipe_from_monitor[0], buffer + total_bytes, PIPE_BUF_SIZE - total_bytes - 1);
        if (bytes_read > 0)
        {
            total_bytes += bytes_read;
            buffer[total_bytes] = '\0';

            // Print the response if we haven't printed anything yet
            if (!got_initial_response)
            {
                printf("%s", buffer);
                fflush(stdout);
                got_initial_response = 1;
                total_bytes = 0; // Reset for next chunk
            }
        }
        else if (bytes_read == 0 || (bytes_read == -1 && errno != EAGAIN))
        {
            break;
        }
        retries--;
        usleep(100000); // 100ms delay between retries
    }

    // If we have any remaining data, print it
    if (total_bytes > 0)
    {
        buffer[total_bytes] = '\0';
        printf("%s", buffer);
        fflush(stdout);
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

    if (command_in_progress)
    {
        printf("Previous command still in progress\n");
        return;
    }

    printf("Debug: Sending command: %s\n", command);
    command_in_progress = 1;
    response_received = 0;

    // Write command to pipe
    ssize_t bytes_written = write(pipe_to_monitor[1], command, strlen(command));
    if (bytes_written < 0)
    {
        perror("Failed to write command to pipe");
        command_in_progress = 0;
        return;
    }
    bytes_written = write(pipe_to_monitor[1], "\n", 1);
    if (bytes_written < 0)
    {
        perror("Failed to write newline to pipe");
        command_in_progress = 0;
        return;
    }

    // printf("Debug: Command written to pipe, sending SIGUSR1 to monitor (PID: %d)\n", monitor_pid);

    // Send signal to monitor to process command
    if (kill(monitor_pid, SIGUSR1) < 0)
    {
        perror("Failed to send signal to monitor");
        command_in_progress = 0;
        return;
    }

    // printf("Debug: SIGUSR1 sent, waiting for response...\n");

    // Wait for response with timeout
    int timeout = 50; // 5 seconds timeout (50 * 100ms)
    while (!response_received && timeout > 0 && monitor_running)
    {
        usleep(100000); // 100ms delay
        timeout--;
        if (timeout % 10 == 0)
        { // Print every second
            printf("Debug: Waiting for response... %d seconds left\n", timeout / 10);
        }
    }

    if (response_received)
    {
        // printf("Debug: Response received, reading...\n");
        read_monitor_response();
    }
    else if (monitor_running)
    {
        printf("No response received from monitor (timeout)\n");
    }
    else
    {
        printf("Monitor process terminated while waiting for response\n");
    }

    command_in_progress = 0;
}

// Function to start the monitor process
void start_monitor()
{
    if (monitor_running)
    {
        printf("Monitor is already running\n");
        return;
    }

    // Create pipes before forking
    if (pipe(pipe_to_monitor) == -1 || pipe(pipe_from_monitor) == -1)
    {
        perror("pipe creation failed");
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
        close(pipe_to_monitor[1]);   // Close write end of input pipe
        close(pipe_from_monitor[0]); // Close read end of output pipe

        // Redirect stdin to read from pipe
        dup2(pipe_to_monitor[0], STDIN_FILENO);
        // Redirect stdout to write to pipe
        dup2(pipe_from_monitor[1], STDOUT_FILENO);

        close(pipe_to_monitor[0]);
        close(pipe_from_monitor[1]);

        execl("./treasure_manager", "treasure_manager", "monitor", NULL);
        perror("execl failed");
        exit(1);
    }
    else
    {
        // Parent process
        close(pipe_to_monitor[0]);   // Close read end of input pipe
        close(pipe_from_monitor[1]); // Close write end of output pipe

        // Set up signal handlers
        struct sigaction sa;
        sa.sa_handler = handle_sigusr1;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGUSR1, &sa, NULL);

        sa.sa_handler = handle_sigchld;
        sigaction(SIGCHLD, &sa, NULL);

        // Set pipes to non-blocking mode
        int flags = fcntl(pipe_from_monitor[0], F_GETFL, 0);
        fcntl(pipe_from_monitor[0], F_SETFL, flags | O_NONBLOCK);

        monitor_pid = pid;
        monitor_running = 1;
        printf("Monitor started with PID: %d\n", pid);
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

    // Wait for monitor to terminate with timeout
    int timeout = 30; // 3 seconds timeout
    while (monitor_running && timeout > 0)
    {
        usleep(100000);
        timeout--;
    }

    if (monitor_running)
    {
        printf("Monitor did not terminate gracefully, forcing termination...\n");
        kill(monitor_pid, SIGTERM);
        usleep(100000);
        if (monitor_running)
        {
            kill(monitor_pid, SIGKILL);
        }
    }
}

// Function to calculate scores for a hunt
void calculate_hunt_scores(const char *hunt_id)
{
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        perror("pipe failed");
        return;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

    if (pid == 0)
    {
        // Child process
        close(pipefd[0]); // Close read end

        // Redirect stdout to pipe
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        // Execute the score calculator
        execl("./score_calculator", "score_calculator", hunt_id, NULL);
        perror("execl failed");
        exit(1);
    }
    else
    {
        // Parent process
        close(pipefd[1]); // Close write end

        // Read from pipe
        FILE *pipe_read = fdopen(pipefd[0], "r");
        if (!pipe_read)
        {
            perror("fdopen failed");
            close(pipefd[0]);
            return;
        }

        char line[PIPE_BUF_SIZE];
        char hunt_id_read[MAX_HUNT_ID];

        // Read hunt ID and user count
        if (fgets(hunt_id_read, sizeof(hunt_id_read), pipe_read))
        {
            hunt_id_read[strcspn(hunt_id_read, "\n")] = 0;

            if (fgets(line, sizeof(line), pipe_read))
            {
                // int user_count = atoi(line);

                // Print header
                printf("\nScores for Hunt %s\n", hunt_id_read);
                printf("----------------------------------------\n");
                printf("Username            | Score | Treasures\n");
                printf("----------------------------------------\n");

                // Read and format each user's data
                char username[MAX_STRING];
                int score, treasures;
                while (fgets(line, sizeof(line), pipe_read))
                {
                    if (sscanf(line, "%s %d %d", username, &score, &treasures) == 3)
                    {
                        printf("%-18s | %5d | %9d\n", username, score, treasures);
                    }
                }
                printf("----------------------------------------\n");
            }
        }

        fclose(pipe_read);
        waitpid(pid, NULL, 0);
    }
}

void display_commands()
{
    printf("\nAvailable commands:\n");
    printf("  start_monitor - Start the monitor process\n");
    printf("  stop_monitor - Stop the monitor process\n");
    printf("  list_hunts - List all available hunts\n");
    printf("  list_treasures - List all treasures in a hunt\n");
    printf("  view_treasure - View a specific treasure\n");
    printf("  calculate_score - Calculate scores for a hunt\n");
    printf("  exit - Exit the program\n");
    printf("\nEnter command: ");
}

int main()
{
    // Set up signal handlers
    struct sigaction sa;
    sa.sa_handler = handle_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sa, NULL) < 0)
    {
        perror("sigaction failed");
        return 1;
    }

    sa.sa_handler = handle_sigchld;
    if (sigaction(SIGCHLD, &sa, NULL) < 0)
    {
        perror("sigaction failed");
        return 1;
    }

    char command[MAX_COMMAND];
    char hunt_id[MAX_HUNT_ID];
    int treasure_id;

    printf("Welcome to Treasure Hub!\n");
    display_commands();

    while (1)
    {
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
                // Remove any leading/trailing spaces
                char *start = hunt_id;
                while (*start && isspace(*start))
                    start++;
                char *end = start + strlen(start) - 1;
                while (end > start && isspace(*end))
                    *end-- = '\0';

                char full_command[MAX_COMMAND + MAX_HUNT_ID];
                snprintf(full_command, sizeof(full_command), "list_treasures %s", start);
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
        else if (strcmp(command, "calculate_score") == 0)
        {
            printf("Enter hunt ID: ");
            if (fgets(hunt_id, sizeof(hunt_id), stdin))
            {
                hunt_id[strcspn(hunt_id, "\n")] = 0;
                // Remove any leading/trailing spaces
                char *start = hunt_id;
                while (*start && isspace(*start))
                    start++;
                char *end = start + strlen(start) - 1;
                while (end > start && isspace(*end))
                    *end-- = '\0';

                calculate_hunt_scores(start);
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
            printf("Unknown command. Type 'help' for available commands.\n");
        }
        display_commands();
    }

    return 0;
}
