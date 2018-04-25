#include "mod_rainback.h"
    #include <apr_strings.h>

rainback_Context* rainback_Context_new(apr_pool_t* pool)
{
    rainback_Context* ctx = apr_palloc(pool, sizeof(*ctx));
    ctx->pool = pool;
    ctx->vars = apr_hash_make(pool);
    ctx->parent = 0;
    return ctx;
}

void rainback_Context_setParent(rainback_Context* ctx, rainback_Context* par)
{
    ctx->parent = par;
}

rainback_Variable* rainback_Variable_new(rainback_Context* ctx, const char* name)
{
    rainback_Variable* var = apr_palloc(ctx->pool, sizeof(*var));
    rainback_Variable_clear(var);
    var->type = rainback_VariableType_STRING;
    var->name = apr_pstrdup(ctx->pool, name);
    var->data.s = "";
    return var;
}

void rainback_Variable_clear(rainback_Variable* var)
{
    var->type = rainback_VariableType_STRING;
    var->data.s = "";
}

void rainback_Context_blank(rainback_Context* ctx, const char* name)
{
    rainback_Variable* var = apr_hash_get(ctx->vars, name, APR_HASH_KEY_STRING);
    if(!var) {
        var = rainback_Variable_new(ctx, name);
        rainback_Context_setVariable(ctx, var);
    }
    var->type = rainback_VariableType_BLANK;
}

void rainback_Context_remove(rainback_Context* context, const char* name)
{
    apr_hash_set(context->vars, name, APR_HASH_KEY_STRING, 0);
}

rainback_Variable* rainback_Context_getVariable(rainback_Context* ctx, const char* name)
{
    void* var = apr_hash_get(ctx->vars, name, APR_HASH_KEY_STRING);
    if(!var && ctx->parent) {
        return rainback_Context_getVariable(ctx->parent, name);
    }
    return var;
}

void rainback_Context_setVariable(rainback_Context* ctx, rainback_Variable* v)
{
    apr_hash_set(ctx->vars, v->name, APR_HASH_KEY_STRING, v);
}

const char* rainback_Context_getString(rainback_Context* ctx, const char* name)
{
    rainback_Variable* v = rainback_Context_getVariable(ctx, name);
    if(!v) {
        return 0;
    }
    switch(v->type) {
    case rainback_VariableType_BLANK:
        return 0;
    case rainback_VariableType_STRING:
        return v->data.s;
    case rainback_VariableType_ENUMERATOR:
        return name;
    case rainback_VariableType_HASH:
        return name;
    }
}

void rainback_Context_setString(rainback_Context* ctx, const char* name, const char* value)
{
    rainback_Variable* v = rainback_Context_getVariable(ctx, name);
    if(!v) {
        v = rainback_Variable_new(ctx, name);
        rainback_Context_setVariable(ctx, v);
    }
    rainback_Variable_clear(v);
    v->type = rainback_VariableType_STRING;
    v->data.s = value;
}

void rainback_Context_setHash(rainback_Context* ctx, const char* name, rainback_Context* hash)
{
    rainback_Variable* v = rainback_Context_getVariable(ctx, name);
    if(!v) {
        v = rainback_Variable_new(ctx, name);
        rainback_Context_setVariable(ctx, v);
    }
    rainback_Variable_clear(v);
    v->type = rainback_VariableType_HASH;
    v->data.h = hash;
}

void rainback_Context_setEnumerator(rainback_Context* ctx, const char* name, void*(*enumerator)(rainback_Template*, rainback_Context*, void**, void*), void* extra)
{
    rainback_Variable* v = rainback_Context_getVariable(ctx, name);
    if(!v) {
        v = rainback_Variable_new(ctx, name);
        rainback_Context_setVariable(ctx, v);
    }
    rainback_Variable_clear(v);
    v->type = rainback_VariableType_ENUMERATOR;
    v->data.e.func = enumerator;
    v->data.e.data = extra;
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
        return v->data.h;
    case rainback_VariableType_BLANK:
        return 0;
    }
}

rainback_Enumerator rainback_Context_getEnumerator(rainback_Context* ctx, const char* name, void** savePtr)
{
    rainback_Variable* v = rainback_Context_getVariable(ctx, name);
    if(!v) {
        return 0;
    }
    if(v->type != rainback_VariableType_ENUMERATOR) {
        return 0;
    }
    *savePtr = v->data.e.data;
    return v->data.e.func;
}
