#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <ctype.h>
#include <fcntl.h> // For open, read, write

#define MAX_STRING 512
#define MAX_CLUE 1024
#define MAX_TREASURES 100
#define MAX_LOG_DETAILS 1024 // Increased buffer size for log details
#define COMMAND_FILE "monitor_command.txt"
#define RESPONSE_FILE "monitor_response.txt"
#define MAX_COMMAND 1024
#define PIPE_BUF_SIZE 4096

// Structure to hold treasure information
typedef struct
{
    int id;
    char username[MAX_STRING];
    double latitude;
    double longitude;
    char clue[MAX_CLUE];
    int value;
} Treasure;

// Structure to hold hunt information
typedef struct
{
    char hunt_id[MAX_STRING];
    Treasure treasures[MAX_TREASURES];
    int treasure_count;
} Hunt;

// Function declarations
void add_treasure(const char *hunt_id);
void list_treasures(const char *hunt_id);
void view_treasure(const char *hunt_id, int treasure_id);
void create_hunt_directory(const char *hunt_id);
char *get_treasure_file_path(const char *hunt_id);
void save_treasures(const char *hunt_id, Hunt *hunt);
Hunt *load_treasures(const char *hunt_id);
void log_operation(const char *hunt_id, const char *operation, const char *details);
void create_log_symlinks();
void merge_hunt_logs();
void remove_treasure(const char *hunt_id, int treasure_id);
void remove_hunt(const char *hunt_id);
void handle_sigusr1(int signum);
void monitor_mode();
void process_command(const char *command);
void display_commands();

// Add these global variables after the includes
static volatile sig_atomic_t running = 1;
volatile sig_atomic_t command_ready = 0;

// Function to merge hunt logs into a single file
void merge_hunt_logs()
{
    int output_file = open("hunt_log.txt", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (output_file == -1)
    {
        perror("Error opening hunt_log.txt");
        return;
    }

    DIR *hunt_dir = opendir("hunt");
    if (hunt_dir == NULL)
    {
        perror("Error opening hunt directory");
        close(output_file);
        return;
    }

    struct dirent *entry;
    struct stat st;
    char log_path[MAX_STRING];
    char buffer[1024];
    char output_buffer[MAX_STRING];

    while ((entry = readdir(hunt_dir)) != NULL)
    {
        if (snprintf(log_path, sizeof(log_path), "hunt/%s", entry->d_name) >= sizeof(log_path))
        {
            fprintf(stderr, "Path truncated: %s\n", entry->d_name);
            continue;
        }

        if (stat(log_path, &st) == 0 && S_ISDIR(st.st_mode)) // Ensure it's a directory
        {
            if (snprintf(log_path, sizeof(log_path), "hunt/%s/logged_hunt.txt", entry->d_name) >= sizeof(log_path))
            {
                fprintf(stderr, "Path truncated: %s\n", entry->d_name);
                continue;
            }

            int log_file = open(log_path, O_RDONLY);
            if (log_file != -1)
            {
                // printf("Appending log from: %s\n", log_path); // Debugging line
                snprintf(output_buffer, sizeof(output_buffer), "=== Log for Hunt: %s ===\n", entry->d_name);
                write(output_file, output_buffer, strlen(output_buffer));

                ssize_t bytes_read;
                while ((bytes_read = read(log_file, buffer, sizeof(buffer))) > 0)
                {
                    write(output_file, buffer, bytes_read);
                }

                write(output_file, "\n", 1);
                close(log_file);
            }
        }
    }

    closedir(hunt_dir);
    close(output_file);
    printf("\nHunt logs merged successfully into hunt_log.txt\n");
}

// Function to create symbolic links for logged_hunt.txt files
void create_log_symlinks()
{
    DIR *hunt_dir = opendir("hunt");
    if (!hunt_dir)
    {
        perror("Error opening hunt directory");
        return;
    }

    // Create links_log_hunt directory if it doesn't exist
    if (mkdir("links_log_hunt", 0755) != 0 && errno != EEXIST)
    {
        perror("Error creating links_log_hunt directory");
        closedir(hunt_dir);
        return;
    }

    struct dirent *entry;
    struct stat st;
    char logged_hunt_path[MAX_STRING];
    char symlink_path[MAX_STRING];

    while ((entry = readdir(hunt_dir)) != NULL)
    {
        if (entry->d_type == DT_DIR &&
            strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0)
        {

            // Check if the subdirectory starts with "hunt"
            if (strncmp(entry->d_name, "hunt", 4) == 0)
            {
                const char *hunt_id = entry->d_name + 4; // Extract ID from "hunt<ID>"

                snprintf(logged_hunt_path, sizeof(logged_hunt_path),
                         "hunt/%s/logged_hunt.txt", entry->d_name);

                // Check if logged_hunt.txt exists
                if (stat(logged_hunt_path, &st) == 0)
                {
                    snprintf(symlink_path, sizeof(symlink_path),
                             "links_log_hunt/logged_hunt-%s", hunt_id);

                    // Remove existing symlink if any
                    unlink(symlink_path);

                    if (symlink(logged_hunt_path, symlink_path) != 0)
                    {
                        perror("Failed to create symlink");
                    }
                    else
                    {
                        printf("\nCreated symlink: %s -> %s\n", symlink_path, logged_hunt_path);
                    }
                }
            }
        }
    }

    closedir(hunt_dir);
    // printf("Symbolic links created in links_log_hunt directory.\n");
}

// Function to log operations
void log_operation(const char *hunt_id, const char *operation, const char *details)
{
    char log_path[MAX_STRING];
    if (snprintf(log_path, sizeof(log_path), "hunt/hunt%s/logged_hunt.txt", hunt_id) >= sizeof(log_path))
    {
        fprintf(stderr, "Log path truncated for hunt_id: %s\n", hunt_id);
        return;
    }

    int log_file = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (log_file == -1)
    {
        perror("Error opening log file");
        return;
    }

    time_t now;
    time(&now);
    char timestamp[26];
    if (ctime_r(&now, timestamp) == NULL)
    {
        perror("Error generating timestamp");
        close(log_file);
        return;
    }
    timestamp[24] = '\0'; // Remove newline

    char log_entry[MAX_LOG_DETAILS];
    snprintf(log_entry, sizeof(log_entry), "[%s] %s: %s\n", timestamp, operation, details);
    write(log_file, log_entry, strlen(log_entry));

    close(log_file);
    merge_hunt_logs();
    create_log_symlinks();
}

// Function to create hunt subdirectory if it doesn't exist
void create_hunt_directory(const char *hunt_id)
{
    char dir_path[MAX_STRING];
    if (snprintf(dir_path, sizeof(dir_path), "hunt/hunt%s", hunt_id) >= sizeof(dir_path))
    {
        fprintf(stderr, "Directory path truncated for hunt_id: %s\n", hunt_id);
        exit(EXIT_FAILURE);
    }

    if (mkdir(dir_path, 0755) != 0 && errno != EEXIST)
    {
        perror("Error creating hunt directory");
        exit(EXIT_FAILURE);
    }
}

// Function to get the full path to the treasure file
char *get_treasure_file_path(const char *hunt_id)
{
    static char path[MAX_STRING];
    if (snprintf(path, sizeof(path), "hunt/hunt%s/treasures.dat", hunt_id) >= sizeof(path))
    {
        fprintf(stderr, "Treasure file path truncated for hunt_id: %s\n", hunt_id);
        exit(EXIT_FAILURE);
    }
    return path;
}

// Function to save treasures to file
void save_treasures(const char *hunt_id, Hunt *hunt)
{
    char *file_path = get_treasure_file_path(hunt_id);
    int file = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (file == -1)
    {
        perror("Error opening treasure file for writing");
        exit(EXIT_FAILURE);
    }

    write(file, &hunt->treasure_count, sizeof(int));
    write(file, hunt->treasures, sizeof(Treasure) * hunt->treasure_count);

    close(file);
}

// Function to load treasures from file
Hunt *load_treasures(const char *hunt_id)
{
    static Hunt hunt;
    strncpy(hunt.hunt_id, hunt_id, MAX_STRING - 1);
    hunt.treasure_count = 0;

    char *file_path = get_treasure_file_path(hunt_id);
    int file = open(file_path, O_RDONLY);

    if (file == -1)
    {
        return &hunt; // Return empty hunt if file doesn't exist
    }

    read(file, &hunt.treasure_count, sizeof(int));
    read(file, hunt.treasures, sizeof(Treasure) * hunt.treasure_count);

    close(file);
    return &hunt;
}

// Function to add a new treasure
void add_treasure(const char *hunt_id)
{
    create_hunt_directory(hunt_id);
    Hunt *hunt = load_treasures(hunt_id);

    if (hunt->treasure_count >= MAX_TREASURES)
    {
        printf("Error: Maximum number of treasures reached\n");
        log_operation(hunt_id, "ADD", "Failed: Maximum number of treasures reached");
        return;
    }

    Treasure *new_treasure = &hunt->treasures[hunt->treasure_count];
    new_treasure->id = hunt->treasure_count + 1;

    char input_buffer[MAX_STRING];

    printf("Enter username: ");
    if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL)
    {
        printf("Error reading username\n");
        return;
    }
    input_buffer[strcspn(input_buffer, "\n")] = 0;
    strncpy(new_treasure->username, input_buffer, MAX_STRING - 1);
    new_treasure->username[MAX_STRING - 1] = '\0';

    printf("Enter latitude: ");
    if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL)
    {
        printf("Error reading latitude\n");
        return;
    }
    if (sscanf(input_buffer, "%lf", &new_treasure->latitude) != 1)
    {
        printf("Invalid latitude format\n");
        return;
    }

    printf("Enter longitude: ");
    if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL)
    {
        printf("Error reading longitude\n");
        return;
    }
    if (sscanf(input_buffer, "%lf", &new_treasure->longitude) != 1)
    {
        printf("Invalid longitude format\n");
        return;
    }

    printf("Enter clue: ");
    if (fgets(new_treasure->clue, MAX_CLUE, stdin) == NULL)
    {
        printf("Error reading clue\n");
        return;
    }
    new_treasure->clue[strcspn(new_treasure->clue, "\n")] = 0;

    printf("Enter value: ");
    if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL)
    {
        printf("Error reading value\n");
        return;
    }
    if (sscanf(input_buffer, "%d", &new_treasure->value) != 1)
    {
        printf("Invalid value format\n");
        return;
    }

    hunt->treasure_count++;
    save_treasures(hunt_id, hunt);

    char log_details[MAX_LOG_DETAILS];
    int written = snprintf(log_details, sizeof(log_details),
                           "Added treasure ID: %d, Username: %s, Value: %d",
                           new_treasure->id, new_treasure->username, new_treasure->value);

    if (written >= sizeof(log_details))
    {
        // Truncate the username if needed
        char truncated_username[MAX_STRING];
        strncpy(truncated_username, new_treasure->username, sizeof(truncated_username) - 1);
        truncated_username[sizeof(truncated_username) - 1] = '\0';

        snprintf(log_details, sizeof(log_details),
                 "Added treasure ID: %d, Username: %s, Value: %d",
                 new_treasure->id, truncated_username, new_treasure->value);
    }

    log_operation(hunt_id, "ADD", log_details);
    printf("\nTreasure added successfully with ID: %d\n", new_treasure->id);
}

// Function to list all treasures from a hunt
void list_treasures(const char *hunt_id)
{
    // Clean hunt_id by removing spaces
    char clean_hunt_id[MAX_STRING];
    int j = 0;
    for (int i = 0; hunt_id[i] != '\0'; i++)
    {
        if (!isspace(hunt_id[i]))
        {
            clean_hunt_id[j++] = hunt_id[i];
        }
    }
    clean_hunt_id[j] = '\0';

    // printf("Debug: Attempting to list treasures for hunt: %s\n", clean_hunt_id);

    char *file_path = get_treasure_file_path(clean_hunt_id);
    // printf("Debug: Treasure file path: %s\n", file_path);

    FILE *file = fopen(file_path, "rb");
    if (file == NULL)
    {
        // printf("Debug: Failed to open treasure file. Error: %s\n", strerror(errno));
        printf("No treasures found in hunt: %s\n", clean_hunt_id);
        return;
    }

    Hunt hunt;
    if (fread(&hunt.treasure_count, sizeof(int), 1, file) != 1)
    {
        printf("Debug: Failed to read treasure count\n");
        fclose(file);
        return;
    }

    // printf("Debug: Found %d treasures\n", hunt.treasure_count);

    if (hunt.treasure_count == 0)
    {
        printf("No treasures found in hunt: %s\n", clean_hunt_id);
        log_operation(clean_hunt_id, "LIST", "No treasures found");
        return;
    }

    // Read all treasures
    if (fread(hunt.treasures, sizeof(Treasure), hunt.treasure_count, file) != hunt.treasure_count)
    {
        printf("Debug: Failed to read treasures\n");
        fclose(file);
        return;
    }

    struct stat st;
    if (stat(file_path, &st) == 0)
    {
        printf("Hunt: %s\n", clean_hunt_id);
        printf("File size: %ld bytes\n", st.st_size);
        printf("Last modified: %s", ctime(&st.st_mtime));
        printf("\nTreasures:\n");
    }

    for (int i = 0; i < hunt.treasure_count; i++)
    {
        Treasure *t = &hunt.treasures[i];
        printf("\nID: %d\n", t->id);
        printf("Username: %s\n", t->username);
        printf("Location: %.4f, %.4f\n", t->latitude, t->longitude);
        printf("Clue: %s\n", t->clue);
        printf("Value: %d\n", t->value);
    }

    fclose(file);

    char log_details[MAX_LOG_DETAILS];
    snprintf(log_details, sizeof(log_details), "Listed %d treasures", hunt.treasure_count);
    log_operation(clean_hunt_id, "LIST", log_details);
}

// Function to view a specific treasure
void view_treasure(const char *hunt_id, int treasure_id)
{
    Hunt *hunt = load_treasures(hunt_id);

    for (int i = 0; i < hunt->treasure_count; i++)
    {
        if (hunt->treasures[i].id == treasure_id)
        {
            Treasure *t = &hunt->treasures[i];
            printf("\nTreasure Details:\n");
            printf("ID: %d\n", t->id);
            printf("Username: %s\n", t->username);
            printf("Location: %.6f, %.6f\n", t->latitude, t->longitude);
            printf("Clue: %s\n", t->clue);
            printf("Value: %d\n", t->value);

            char log_details[MAX_LOG_DETAILS];
            int written = snprintf(log_details, sizeof(log_details),
                                   "Viewed treasure ID: %d, Username: %s",
                                   t->id, t->username);

            if (written >= sizeof(log_details))
            {
                // Truncate the username if needed
                char truncated_username[MAX_STRING];
                strncpy(truncated_username, t->username, sizeof(truncated_username) - 1);
                truncated_username[sizeof(truncated_username) - 1] = '\0';

                snprintf(log_details, sizeof(log_details),
                         "Viewed treasure ID: %d, Username: %s",
                         t->id, truncated_username);
            }

            log_operation(hunt_id, "VIEW", log_details);
            return;
        }
    }

    printf("Treasure with ID %d not found in hunt %s\n", treasure_id, hunt_id);
    char log_details[MAX_LOG_DETAILS];
    snprintf(log_details, sizeof(log_details), "Failed to view treasure ID: %d (not found)", treasure_id);
    log_operation(hunt_id, "VIEW", log_details);
}

void remove_treasure(const char *hunt_id, int treasure_id)
{
    Hunt *hunt = load_treasures(hunt_id);

    if (hunt->treasure_count == 0)
    {
        printf("\nNo treasures to remove in hunt %s\n", hunt_id);
        log_operation(hunt_id, "REMOVE", "Failed: No treasures found");
        return;
    }

    int found = 0;

    for (int i = 0; i < hunt->treasure_count; i++)
    {
        if (hunt->treasures[i].id == treasure_id)
        {
            found = 1;

            // Shift remaining treasures up
            for (int j = i; j < hunt->treasure_count - 1; j++)
            {
                hunt->treasures[j] = hunt->treasures[j + 1];
            }

            hunt->treasure_count--;

            // Reassign IDs to be sequential again
            for (int k = 0; k < hunt->treasure_count; k++)
            {
                hunt->treasures[k].id = k + 1;
            }

            save_treasures(hunt_id, hunt);

            char log_details[MAX_LOG_DETAILS];
            snprintf(log_details, sizeof(log_details), "Removed treasure ID: %d. Remaining count: %d", treasure_id, hunt->treasure_count);
            log_operation(hunt_id, "REMOVE", log_details);

            printf("\nTreasure ID %d removed successfully.\n", treasure_id);
            return;
        }
    }

    if (!found)
    {
        printf("\nTreasure ID %d not found in hunt %s\n", treasure_id, hunt_id);
        char log_details[MAX_LOG_DETAILS];
        snprintf(log_details, sizeof(log_details), "Failed to remove treasure ID: %d (not found)", treasure_id);
        log_operation(hunt_id, "REMOVE", log_details);
    }
}

void remove_hunt(const char *hunt_id)
{
    char dir_path[MAX_STRING];
    if (snprintf(dir_path, sizeof(dir_path), "hunt/hunt%s", hunt_id) >= sizeof(dir_path))
    {
        fprintf(stderr, "Directory path truncated for hunt_id: %s\n", hunt_id);
        return;
    }

    DIR *dir = opendir(dir_path);
    if (dir == NULL)
    {
        perror("Failed to open hunt directory");
        return;
    }

    struct dirent *entry;
    char file_path[MAX_STRING];

    // Remove all files in the hunt directory
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        if (snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name) >= sizeof(file_path))
        {
            fprintf(stderr, "File path truncated: %s\n", entry->d_name);
            continue;
        }

        if (remove(file_path) != 0)
        {
            perror("Error deleting file");
        }
    }

    closedir(dir);

    // Remove the directory itself
    if (rmdir(dir_path) != 0)
    {
        perror("Failed to remove hunt directory");
        return;
    }

    // Remove the symlink to the hunt directory
    char symlink_path[MAX_STRING];
    if (snprintf(symlink_path, sizeof(symlink_path), "links_log_hunt/logged_hunt-%s", hunt_id) < sizeof(symlink_path))
    {
        unlink(symlink_path); // Ignore errors if it doesn't exist
    }

    printf("\nHunt %s removed successfully.\n", hunt_id);
}

// Signal handler for SIGUSR1
void handle_sigusr1(int signum)
{
    command_ready = 1;
}

void monitor_mode()
{
    // Set up signal handler
    struct sigaction sa;
    sa.sa_handler = handle_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sa, NULL) < 0)
    {
        perror("sigaction failed");
        exit(1);
    }

    // Ignore SIGTSTP (Ctrl+Z) to prevent stopping
    signal(SIGTSTP, SIG_IGN);

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    // Buffer for building response
    FILE *stdout_pipe = fdopen(STDOUT_FILENO, "w");
    if (!stdout_pipe)
    {
        perror("Failed to open stdout pipe");
        exit(1);
    }
    setbuf(stdout_pipe, NULL); // Make the stream unbuffered

    // Set stdin to non-blocking mode
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    // Send initial ready message through pipe
    fprintf(stdout_pipe, "Monitor mode started. Waiting for commands...\n");
    fflush(stdout_pipe);

    while (running)
    {
        // Wait for signal indicating command is ready
        while (!command_ready && running)
        {
            pause();
        }

        if (!running)
            break;
        command_ready = 0;

        // Read command from stdin (pipe)
        if ((read = getline(&line, &len, stdin)) == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            break;
        }

        // Remove newline
        if (read > 0 && line[read - 1] == '\n')
        {
            line[read - 1] = '\0';
        }

        if (strcmp(line, "stop") == 0)
        {
            running = 0;
            fprintf(stdout_pipe, "Monitor stopping...\n");
            fflush(stdout_pipe);
            kill(getppid(), SIGUSR1);
            break;
        }

        // Process commands and write responses to pipe
        if (strcmp(line, "list_hunts") == 0)
        {
            DIR *hunt_dir = opendir("hunt");
            if (hunt_dir)
            {
                struct dirent *entry;
                int found_hunts = 0;
                while ((entry = readdir(hunt_dir)) != NULL)
                {
                    if (entry->d_type == DT_DIR && strncmp(entry->d_name, "hunt", 4) == 0)
                    {
                        char *hunt_id = entry->d_name + 4;
                        Hunt *hunt = load_treasures(hunt_id);
                        if (hunt)
                        {
                            fprintf(stdout_pipe, "Hunt %s: %d treasures\n", hunt_id, hunt->treasure_count);
                            found_hunts = 1;
                        }
                    }
                }
                if (!found_hunts)
                {
                    fprintf(stdout_pipe, "No hunts found\n");
                }
                closedir(hunt_dir);
            }
            else
            {
                fprintf(stdout_pipe, "Error: Could not open hunt directory\n");
            }
            fflush(stdout_pipe);
            kill(getppid(), SIGUSR1);
        }
        else if (strncmp(line, "list_treasures ", 14) == 0)
        {
            const char *hunt_id = line + 14;
            // Redirect all printf output to the pipe
            int stdout_fd = dup(STDOUT_FILENO);
            dup2(fileno(stdout_pipe), STDOUT_FILENO);

            // Call list_treasures
            list_treasures(hunt_id);

            // Restore original stdout
            fflush(stdout);
            dup2(stdout_fd, STDOUT_FILENO);
            close(stdout_fd);

            fflush(stdout_pipe);
            kill(getppid(), SIGUSR1);
        }
        else if (strncmp(line, "view_treasure ", 13) == 0)
        {
            char hunt_id[512];
            int treasure_id;
            if (sscanf(line + 13, "%s %d", hunt_id, &treasure_id) == 2)
            {
                // Redirect all printf output to the pipe
                int stdout_fd = dup(STDOUT_FILENO);
                dup2(fileno(stdout_pipe), STDOUT_FILENO);

                // Call view_treasure
                view_treasure(hunt_id, treasure_id);

                // Restore original stdout
                fflush(stdout);
                dup2(stdout_fd, STDOUT_FILENO);
                close(stdout_fd);

                fflush(stdout_pipe);
                kill(getppid(), SIGUSR1);
            }
        }
    }

    if (line)
    {
        free(line);
    }
    fclose(stdout_pipe);
}

void display_commands()
{
    printf("\nAvailable commands:\n");
    printf("  add <hunt_id> - Add a new treasure\n");
    printf("  list <hunt_id> - List all treasures\n");
    printf("  view <hunt_id> <treasure_id> - View specific treasure\n");
    printf("  remove <hunt_id> <treasure_id> - Remove a specific treasure\n");
    printf("  remove_hunt <hunt_id> - Remove a specific hunt\n");
    printf("  exit - Exit the program\n");
    printf("\nEnter command: ");
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "monitor") == 0)
    {
        monitor_mode();
        return 0;
    }

    if (argc == 1)
    {
        printf("Welcome to Treasure Manager!\n");
        display_commands();

        char command[1024];
        char input_buffer[1024];
        while (1)
        {
            if (fgets(input_buffer, sizeof(input_buffer), stdin))
            {
                // Remove newline
                input_buffer[strcspn(input_buffer, "\n")] = 0;
                strncpy(command, input_buffer, sizeof(command) - 1);
                command[sizeof(command) - 1] = '\0';

                if (strcmp(command, "exit") == 0)
                {
                    printf("Exiting Treasure Manager...\n");
                    break;
                }

                // Parse the command
                char cmd[32], hunt_id[512];
                int treasure_id = 0;

                if (sscanf(command, "%31s %511s %d", cmd, hunt_id, &treasure_id) >= 2)
                {
                    if (strcmp(cmd, "add") == 0)
                    {
                        add_treasure(hunt_id);
                        display_commands(); // Only display commands after treasure is added
                    }
                    else if (strcmp(cmd, "list") == 0)
                    {
                        list_treasures(hunt_id);
                        display_commands();
                    }
                    else if (strcmp(cmd, "view") == 0)
                    {
                        view_treasure(hunt_id, treasure_id);
                        display_commands();
                    }
                    else if (strcmp(cmd, "remove") == 0)
                    {
                        if (treasure_id == 0)
                        {
                            printf("Please provide a valid treasure ID to remove.\n");
                        }
                        else
                        {
                            remove_treasure(hunt_id, treasure_id);
                        }
                        display_commands();
                    }
                    else if (strcmp(cmd, "remove_hunt") == 0)
                    {
                        remove_hunt(hunt_id);
                        display_commands();
                    }
                    else
                    {
                        printf("Unknown command: %s\n", cmd);
                        display_commands();
                    }
                }
                else
                {
                    printf("Invalid command format. Please use: <command> <hunt_id> [treasure_id]\n");
                    display_commands();
                }
            }
        }
        return 0;
    }

    if (argc < 3)
    {
        printf("Usage: %s <command> <hunt_id> [treasure_id]\n", argv[0]);
        display_commands();
        return 1;
    }

    char *command = argv[1];
    char *hunt_id = argv[2];
    int treasure_id = (argc > 3) ? atoi(argv[3]) : 0;

    if (strcmp(command, "add") == 0)
    {
        add_treasure(hunt_id);
    }
    else if (strcmp(command, "list") == 0)
    {
        list_treasures(hunt_id);
    }
    else if (strcmp(command, "view") == 0)
    {
        view_treasure(hunt_id, treasure_id);
    }
    else if (strcmp(command, "remove") == 0)
    {
        if (treasure_id == 0)
        {
            printf("Please provide a valid treasure ID to remove.\n");
            return 1;
        }
        remove_treasure(hunt_id, treasure_id);
    }
    else if (strcmp(command, "remove_hunt") == 0)
    {
        remove_hunt(hunt_id);
    }
    else
    {
        printf("Unknown command: %s\n", command);
        return 1;
    }

    return 0;
}

