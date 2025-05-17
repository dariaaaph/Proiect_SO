#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STRING 512
#define MAX_CLUE 1024
#define MAX_TREASURES 100
#define MAX_USERS 50

typedef struct
{
    int id;
    char username[MAX_STRING];
    double latitude;
    double longitude;
    char clue[MAX_CLUE];
    int value;
} Treasure;

typedef struct
{
    char username[MAX_STRING];
    int total_score;
    int treasure_count;
} UserScore;

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "ERROR:Invalid arguments\n");
        return 1;
    }

    char hunt_id[MAX_STRING];
    strncpy(hunt_id, argv[1], MAX_STRING - 1);
    hunt_id[MAX_STRING - 1] = '\0';

    char file_path[MAX_STRING * 2];
    snprintf(file_path, sizeof(file_path), "hunt/hunt%s/treasures.dat", hunt_id);

    FILE *file = fopen(file_path, "rb");
    if (!file)
    {
        fprintf(stderr, "ERROR:Could not open treasure file\n");
        return 1;
    }

    int treasure_count;
    if (fread(&treasure_count, sizeof(int), 1, file) != 1)
    {
        fprintf(stderr, "ERROR:Could not read treasure count\n");
        fclose(file);
        return 1;
    }

    Treasure treasures[MAX_TREASURES];
    if (treasure_count > 0 && fread(treasures, sizeof(Treasure), treasure_count, file) != treasure_count)
    {
        fprintf(stderr, "ERROR:Could not read treasures\n");
        fclose(file);
        return 1;
    }
    fclose(file);

    UserScore users[MAX_USERS] = {0};
    int user_count = 0;

    // Calculate scores for each user
    for (int i = 0; i < treasure_count; i++)
    {
        int found = 0;
        for (int j = 0; j < user_count; j++)
        {
            if (strcmp(users[j].username, treasures[i].username) == 0)
            {
                users[j].total_score += treasures[i].value;
                users[j].treasure_count++;
                found = 1;
                break;
            }
        }
        if (!found && user_count < MAX_USERS)
        {
            strcpy(users[user_count].username, treasures[i].username);
            users[user_count].total_score = treasures[i].value;
            users[user_count].treasure_count = 1;
            user_count++;
        }
    }

    // Output data in a simple format for pipe communication
    fprintf(stdout, "%s\n%d\n", hunt_id, user_count);
    for (int i = 0; i < user_count; i++)
    {
        fprintf(stdout, "%s %d %d\n",
                users[i].username,
                users[i].total_score,
                users[i].treasure_count);
    }

    return 0;
}
