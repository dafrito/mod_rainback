#include "mod_rainback.h"
#include <apr_escape.h>

marla_WriteResult rainback_readForm(marla_Request* req, apr_hash_t* formData, char* buf, size_t len)
{
    // Get the first pair.
    char* pairPtr = 0;
    for(char* pair = strtok_r(buf, "&", &pairPtr); pair; pair = strtok_r(0, "&", &pairPtr)) {
        char keybuf[MAX_FIELD_NAME_LENGTH + 1];
        char valuebuf[MAX_FIELD_VALUE_LENGTH + 1];
        char* savePtr = 0;

        // Get the key.
        const char* key = strtok_r(pair, "=", &savePtr);
        if(strlen(key) > sizeof(keybuf)) {
            marla_killRequest(req, 400, "Key too long");
            return marla_WriteResult_KILLED;
        }
        key = apr_punescape_url(req->pool, key, 0, 0, 1);
        if(!key) {
            marla_killRequest(req, 400, "Failed to unescape key.");
            return marla_WriteResult_KILLED;
        }

        // Get the value.
        const char* value = strtok_r(0, "=", &savePtr);
        if(strlen(value) > sizeof(valuebuf)) {
            marla_killRequest(req, 400, "Value too long");
            return marla_WriteResult_KILLED;
        }
        value = apr_punescape_url(req->pool, value, 0, 0, 1);
        if(!value) {
            marla_killRequest(req, 400, "Failed to unescape value.");
            return marla_WriteResult_KILLED;
        }

        // Reset strtok.
        if(strtok_r(0, "=", &savePtr) != 0) {
            marla_killRequest(req, 400, "Value contains unexpected separator.");
            return marla_WriteResult_KILLED;
        }

        // Add the value to the form.
        apr_hash_set(formData, key, APR_HASH_KEY_STRING, value);
    }
    return marla_WriteResult_CONTINUE;
}
