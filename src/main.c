#ifdef _WIN32
#include "win_native.h"
#elif __linux__
#include "linux.h"
#endif
#include "main.h"

// https://gist.github.com/BigBrainAFK/0ba454a1efb43f7cb6301cda8838f432
const char ReloadProfile0 = 7;
const char GetCurrentKeyboardProfileIndex = 11;
const char ActivateProfile = 23;
const char RefreshRgbColors = 29;
const char WootDevResetAll = 32;

typedef struct proc_struct
{
    char* match;
    int profile;
} Process;

Process *process_list;
int process_list_length;

int last_profile = -1;
int initial_profile = -1;
int main()
{
    if (!wooting_rgb_kbd_connected())
    {
        puts("Keyboard not connected.");
        return EXIT_FAILURE;
    }

    uint8_t *buff = (uint8_t *) calloc(256, sizeof(uint8_t));

    int read_result = wooting_usb_send_feature_with_response(buff, 256, GetCurrentKeyboardProfileIndex, 0, 0, 0, 0);

#ifdef _DEBUG
    printf("Bytes read: %d\n", read_result);

    printf("Buffer \n");
    for(int i = 0; i < 256; i++ )
    {
        printf("%d%s", buff[i], i < 255 ? ", " : "");
    }
    printf("\n");
#endif

    if (buff[4] == 1)
    {
        memcpy(&last_profile, &buff[5], sizeof(last_profile));
        printf("Current Profile is %s%c\n", last_profile == 0 ? "Digital" : "Analog", last_profile > 0 ? (char)last_profile+'0' : ' ');
    }

    free(buff);

    memcpy(&initial_profile, &last_profile, sizeof(last_profile));

    load_config();

    // Exit handler so the platforms can clean up their hooks if necessary
    register_cleanup();

    start_listening();
}

void reset_profile()
{
    set_profile(initial_profile);
}

const char *last_match = "";
int update_profile(const char *match)
{
    int match_found = 0;

    if (strcmp(match, last_match) == 0)
        return match_found;
    else
        last_match = match;

    puts(match);

    int new_profile = 0; // Default to Digital Profile
    for (size_t i = 0; i < process_list_length; i++)
    {
        Process process = process_list[i];
        #ifdef _DEBUG
        printf("Proc_match_name: %s\n", process.match);
        #endif
        if (strcmp(match, process.match) == 0)
        {
            new_profile = process.profile;
            match_found = 1;
        }
    }

    if (last_profile != new_profile)
    {
        last_profile = new_profile;
        set_profile(new_profile);
    }

    return match_found;
}

void set_profile(int profileIndex)
{
    wooting_usb_send_feature(ActivateProfile, 0, 0, 0, profileIndex);  // Change profile
    usleep(10000);
    wooting_usb_send_feature(ReloadProfile0, 0, 0, 0, profileIndex);   // Change RGB
    usleep(10000);
    wooting_usb_send_feature(WootDevResetAll, 0, 0, 0, 0);            // Reset (Load RGB)
    usleep(10000);
    wooting_usb_send_feature(RefreshRgbColors, 0, 0, 0, profileIndex); // Refresh RGB (Load Effect)
}

void register_cleanup()
{
    atexit(cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGINT, cleanup);
}

char* read_file(char* filename) {
    FILE *f = fopen(filename, "rt");
    if (f == NULL)
    {
        fprintf(stderr, "Error while reading config file: %s\n", filename);
        fprintf(stderr, "Press any key to exit");
        getchar();
        exit(EXIT_FAILURE);
    }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buffer = (char*) malloc(length + 1);
    buffer[length] = '\0';
    fread(buffer, 1, length, f);
    fclose(f);
    return buffer;
}

void load_config()
{
    // TODO: Get config path per OS if necessary
    char *path = get_config_path();

    char *content = read_file(path);
    cJSON *json = cJSON_Parse(content);

    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            fprintf(stderr, "Error while parsing the JSON file right before: %s\n", error_ptr);
        }
        else
        {
            fprintf(stderr, "General error while trying to parse JSON file: $s\n", path);
        }
        fprintf(stderr, "Press any key to exit");
        getchar();
        exit(EXIT_FAILURE);
    }

    cJSON *proc_name = NULL;
    cJSON *profile_index = NULL;

    int i;

    cJSON *processes = cJSON_GetObjectItem(json, "process_list");
    process_list_length = cJSON_GetArraySize(processes);

    process_list = calloc(process_list_length, sizeof(Process));

    for (i = 0 ; i < process_list_length; i++)
    {
        cJSON *temp = cJSON_GetArrayItem(processes, i);
        proc_name = cJSON_GetObjectItem(temp, "process_name");
        profile_index = cJSON_GetObjectItem(temp, "profile_index");

        if (cJSON_IsInvalid(proc_name))
        {
            printf("Found entry without \"process_name\" field specified.\nSkipping...\n");
            continue;
        }

        if (!cJSON_IsString(proc_name))
        {
            printf("Invalid entry found: \"process_name\" field has to be a string!\nSkipping...\n");
            continue;
        }

        if (cJSON_IsInvalid(profile_index))
        {
            printf("Found entry without \"profile_index\" field specified (%s).\nSkipping...\n",
                proc_name->valuestring
            );
            continue;
        }

        if (!cJSON_IsNumber(profile_index))
        {
            printf("Invalid entry found: \"profile_index\" field has to be a number (%s)!\nSkipping...\n",
                proc_name->valuestring
            );
            continue;
        }

        if (profile_index->valueint != (int)profile_index->valueint)
        {
            printf("Entry for \"%s\" has a non integer index (%d)\nSkipping..\n",
                proc_name->valuestring,
                profile_index->valueint
            );
            continue;
        }

        if (profile_index->valueint > 3 || profile_index->valueint < 0)
        {
            printf("Entry for \"%s\" has an index out of the range 0 to 3 (%d)\nSkipping..\n",
                proc_name->valuestring,
                profile_index->valueint
            );
            continue;
        }

        process_list[i].match = proc_name->valuestring;
        process_list[i].profile = profile_index->valueint;
    }
}