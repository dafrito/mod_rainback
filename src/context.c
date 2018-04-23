#include "mod_rainback.h"

struct rainback_Iteration {
void* savePtr;
void* iterationData;
};

rainback_Context* rainback_Context_new(apr_pool_t* pool)
{
    rainback_Context* ctx = apr_palloc(pool, sizeof(*ctx));
    ctx->vars = apr_hash_make(pool);
    return ctx;
}

rainback_Variable* rainback_Context_getVariable(rainback_Context* ctx, const char* name)
{
    return apr_hash_get(ctx->vars, name, APR_HASH_KEY_STRING);
}

const char* rainback_Context_getString(rainback_Context* ctx, const char* name)
{
    rainback_Variable* v = rainback_Context_getVariable(ctx, name);
    if(!v) {
        return 0;
    }
    switch(v->type) {
    case rainback_VariableType_STRING:
        return v->data;
    case rainback_VariableType_ENUMERATOR:
        return name;
    case rainback_VariableType_HASH:
        return name;
    }
}

rainback_Context* rainback_Context_getHash(rainback_Context* ctx, const char* name)
{
    rainback_Variable* v = rainback_Context_getVariable(ctx, name);
    if(!v) {
        return 0;
    }
    switch(v->type) {
    case rainback_VariableType_STRING:
        return 0;
    case rainback_VariableType_ENUMERATOR:
        return 0;
    case rainback_VariableType_HASH:
        return v->data;
    }
}

void*(*)(rainback_Template*, rainback_Context*, rainback_Iteration*) rainback_Context_getEnumerator(rainback_Context* ctx, const char* name)
{
    rainback_Variable* v = rainback_Context_getVariable(ctx, name);
    if(!v) {
        return 0;
    }
    if(v->type != rainback_VariableType_ENUMERATOR) {
        return 0;
    }
    return v->data;
}
