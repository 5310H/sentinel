#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"

// This function takes the RAM array and saves it to setup.json
bool storage_save_users(User_t* users, int count) {
    cJSON *root = cJSON_CreateObject();
    cJSON *u_arr = cJSON_CreateArray();

    for (int i = 0; i < count; i++) {
        cJSON *u = cJSON_CreateObject();
        cJSON_AddNumberToObject(u, "id", users[i].id);
        cJSON_AddStringToObject(u, "name", users[i].name);
        cJSON_AddStringToObject(u, "PIN", users[i].PIN);
        cJSON_AddBoolToObject(u, "IsAdmin", users[i].IsAdmin);
        // We include these because they are in config.h
        cJSON_AddStringToObject(u, "Phone", users[i].Phone);
        cJSON_AddStringToObject(u, "Email", users[i].Email);
        
        cJSON_AddItemToArray(u_arr, u);
    }

    cJSON_AddItemToObject(root, "users", u_arr);
    
    char *json_str = cJSON_Print(root);
    
    FILE *f = fopen("data/setup.json", "w");
    if (f == NULL) return false;
    
    fprintf(f, "%s", json_str);
    fclose(f);
    
    free(json_str);
    cJSON_Delete(root);
    return true;
}