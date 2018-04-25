// Template is created with a given template path.
// Template is used with a given context to construct a page.
// The template is rebuilt when its template path changes (depends on inotify)
// The template represents the compiled template with an AST.
// Each template AST node knows how to build itself.
// The whole template is constructed with a visitor.

// Template(name, path)
// Template("user", "/usr/parsegraph/templates/user.html")

// Template AST node types:
// 1. Inline content (implied)
// 2. Content from other templates (by name if no dot, by path otherwise)
// 3. Content from template hashmap. (Hashmap provided from request that populates it)
//
// APR_HASH_SET(ctx, ..)
// APR_HASH_SET(ctx, "username", username)
// APR_HASH_SET(ctx, ..)
// rainback_invokeTemplate("user", ctx)

// You are <% username %>.

// Templates are used during generation to allow creation of HTML pages without needing
// to escape the text in C.

// Templates are used to generate HTTP bodies and not headers.
#include "mod_rainback.h"
#include <apr_strings.h>

static rainback_TemplateStep* rainback_makeTemplateStep(rainback_Template* template)
{
    rainback_TemplateStep* step = apr_palloc(template->pool, sizeof(rainback_TemplateStep));
    step->render = 0;
    step->renderData = 0;
    step->nextStep = 0;
    return step;
}

static rainback_TemplateStep* rainback_Template_addStep(rainback_Template* template)
{
    rainback_TemplateStep* step = rainback_makeTemplateStep(template);
    if(template->processor) {
        template->processor->addStep(template, step, template->processor->procData);
    }
    else {
        if(template->lastStep) {
            template->lastStep->nextStep = step;
        }
        else {
            template->firstStep = step;
        }
        template->lastStep = step;
    }
    return step;
}

static void rainback_Template_addTemplateProcessor(rainback_Template* template, rainback_TemplateProcessor* proc)
{
    rainback_TemplateProcessor* lastProc = template->processor;
    if(lastProc) {
        proc->prevProcessor = lastProc;
    }
    template->processor = proc;
}

static void rainback_Template_removeTemplateProcessor(rainback_Template* template)
{
    if(!template->processor) {
        return;
    }
    template->processor = template->processor->prevProcessor;
}

static void renderTemplateContent(mod_rainback* rb, rainback_Page* page, rainback_Context* context, void* stepData)
{
    rainback_renderTemplate(rb, stepData, context, page);
}

static void rainback_Template_includeTemplate(rainback_Template* template, const char* content)
{
    rainback_TemplateStep* step = rainback_Template_addStep(template);
    step->render = renderTemplateContent;
    step->renderData = (void*)content;
}

struct foreachData {
rainback_Template* tp;
char varname[128];
char list[128];
rainback_Page* upperPage;
rainback_TemplateStep* firstStep;
rainback_TemplateStep* lastStep;
};

struct ifData {
rainback_Template* tp;
char varname[128];
rainback_TemplateStep* firstStep;
rainback_TemplateStep* lastStep;
};

static void addForeachStep(rainback_Template* te, rainback_TemplateStep* step, void* procData)
{
    struct foreachData* d = procData;
    if(d->lastStep) {
        d->lastStep->nextStep = step;
    }
    else {
        d->firstStep = step;
    }
    d->lastStep = step;
}

static void addIfStep(rainback_Template* te, rainback_TemplateStep* step, void* procData)
{
    struct ifData* d = procData;
    if(d->lastStep) {
        d->lastStep->nextStep = step;
    }
    else {
        d->firstStep = step;
    }
    d->lastStep = step;
}

static void renderForeachContent(mod_rainback* rb, rainback_Page* page, rainback_Context* context, void* stepData)
{
    struct foreachData* d = stepData;

    void* givenState;
    rainback_Enumerator enumerator = rainback_Context_getEnumerator(context, d->list, &givenState);
    rainback_Context* ctx = rainback_Context_new(context->pool);
    rainback_Context_setParent(ctx, context);
    void* savePtr = 0;
    void* next = 0;
    void* val = 0;
    for(;;) {
        if(!next && !val) {
            val = enumerator(d->tp, ctx, &savePtr, givenState);
            if(!val) {
                // Nothing at all.
                break;
            }
            rainback_Context_setString(ctx, "first", "");
            val = apr_pstrdup(d->tp->pool, val);
            next = enumerator(d->tp, ctx, &savePtr, givenState);
        }
        else {
            rainback_Context_blank(ctx, "first");
            val = apr_pstrdup(d->tp->pool, next);
            next = enumerator(d->tp, ctx, &savePtr, givenState);
        }
        if(!next) {
            rainback_Context_setString(ctx, "last", "");
        }
        else {
            rainback_Context_blank(ctx, "last");
        }

        rainback_Context_setString(ctx, d->varname, val);

        // Process each step.
        for(rainback_TemplateStep* step = d->firstStep; step; step = step->nextStep) {
            step->render(rb, page, ctx, step->renderData);
        }
        if(val && !next) {
            break;
        }
    }
}

static void renderConditional(mod_rainback* rb, rainback_Page* page, rainback_Context* context, void* stepData)
{
    struct ifData* d = stepData;
    rainback_Variable* var = rainback_Context_getVariable(context, d->varname);
    if(var && var->type != rainback_VariableType_BLANK) {
        // Process each step.
        for(rainback_TemplateStep* step = d->firstStep; step; step = step->nextStep) {
            step->render(rb, page, context, step->renderData);
        }
    }
}

static void rainback_Template_beginForeach(rainback_Template* template, const char* varname, const char* list)
{
    rainback_TemplateProcessor* proc = apr_palloc(template->pool, sizeof(rainback_TemplateProcessor));
    proc->templateCommand = 0;
    proc->addStep = addForeachStep;
    proc->prevProcessor = 0;

    struct foreachData* d = apr_palloc(template->pool, sizeof(struct foreachData));
    strncpy(d->varname, varname, sizeof(d->varname));
    strncpy(d->list, list, sizeof(d->list));
    proc->procData = d;

    rainback_TemplateStep* step = rainback_Template_addStep(template);
    step->render = renderForeachContent;
    step->renderData = d;

    d->tp = template;
    d->firstStep = 0;
    d->lastStep = 0;

    rainback_Template_addTemplateProcessor(template, proc);
}

static void rainback_Template_endForeach(rainback_Template* template)
{
    rainback_Template_removeTemplateProcessor(template);
}

static void rainback_Template_endIf(rainback_Template* template)
{
    rainback_Template_removeTemplateProcessor(template);
}

static void rainback_Template_beginIf(rainback_Template* template, const char* varname)
{
    rainback_TemplateProcessor* proc = apr_palloc(template->pool, sizeof(rainback_TemplateProcessor));
    proc->templateCommand = 0;
    proc->addStep = addIfStep;
    proc->prevProcessor = 0;

    struct ifData* d = apr_palloc(template->pool, sizeof(struct ifData));
    strncpy(d->varname, varname, sizeof(d->varname));
    proc->procData = d;

    rainback_TemplateStep* step = rainback_Template_addStep(template);
    step->render = renderConditional;
    step->renderData = d;

    d->tp = template;
    d->firstStep = 0;
    d->lastStep = 0;

    rainback_Template_addTemplateProcessor(template, proc);
}

static void rainback_processCommand(rainback_Template* template, char* str)
{
    char* cmdPtr;
    char* command = strtok_r(str, " ", &cmdPtr);
    if(!strcasecmp(command, "include")) {
        // include <template>
        char* name = strtok_r(0, " ", &cmdPtr);
        rainback_Template_includeTemplate(template, name);
    }
    else if(!strcasecmp(command, "if")) {
        // if <varname>
        char* varname = strtok_r(0, " ", &cmdPtr);
        rainback_Template_beginIf(template, varname);
    }
    else if(!strcasecmp(command, "endif")) {
        // endif
        rainback_Template_endIf(template);
    }
    else if(!strcasecmp(command, "foreach")) {
        // foreach <varname> in <list>
        char* varname = strtok_r(0, " ", &cmdPtr);
        char* sep = strtok_r(0, " ", &cmdPtr);
        if(strcmp(sep, "in")) {
            // Parse error.
            abort();
        }
        char* list = strtok_r(0, " ", &cmdPtr);
        rainback_Template_beginForeach(template, varname, list);
    }
    else if(!strcasecmp(command, "endfor")) {
        // endfor
        rainback_Template_endForeach(template);
    }
    else if(!strcasecmp(command, "get")) {
        // get <varname>
        char* varname = strtok_r(0, " ", &cmdPtr);
        rainback_Template_mappedContent(template, varname);
        strtok_r(0, " ", &cmdPtr);
    }
    else if(command[0] == '=') {
        if(command[1] == 0) {
            // = <varname>
            char* varname = strtok_r(0, " ", &cmdPtr);
            rainback_Template_mappedContent(template, varname);
        }
        else {
            // =<varname>
            rainback_Template_mappedContent(template, command + 1);
        }
        strtok_r(0, " ", &cmdPtr);
    }
}

static void rainback_Template_processTemplateWord(rainback_Template* template, unsigned char* content)
{
    rainback_TemplateProcessor* proc = template->processor;
    if(proc && proc->templateCommand) {
        proc->templateCommand(template, content, proc->procData);
    }
    else {
        rainback_Template_stringContent(template, content);
    }
}

void rainback_Template_parseString(rainback_Template* template, unsigned char* content)
{
    enum TemplateParseStage tps = TemplateParseStage_STATIC;
    int length = 0;
    unsigned char* word = content;
    for(int i = 0;;) {
        unsigned char c = content[i];
        switch(tps) {
        case TemplateParseStage_STATIC:
            if(!c) {
                rainback_Template_processTemplateWord(template, word);
                return;
            }
            if(c == '<' && content[i + 1] == '%') {
                if(length > 0) {
                    // Add the static content.
                    content[i] = 0;
                    rainback_Template_processTemplateWord(template, word);
                    content[i] = '<';
                }
                tps = TemplateParseStage_COMMAND;
                length = 0;
                i += 2;
                word = content + i;
                continue;
            }
            ++i;
            ++length;
            break;
        case TemplateParseStage_COMMAND:
            if(c == '%' && content[i + 1] == '>') {
                if(length > 0) {
                    // Process the command.
                    content[i] = 0;
                    rainback_processCommand(template, word);
                    content[i] = '%';
                }
                tps = TemplateParseStage_STATIC;
                i += 2;
                length = 0;
                word = content + i;
                continue;
            }
            ++i;
            ++length;
            break;
        }
    }
}

rainback_Template* rainback_Template_new(mod_rainback* rb)
{
    rainback_Template* template = malloc(sizeof(*template));
    if(APR_SUCCESS != apr_pool_create(&template->pool, rb->session->pool)) {
        marla_die(rb->session->server, "Failed to create template memory pool.");
    }
    template->fe = 0;
    template->rb = rb;
    template->path = 0;
    template->firstStep = 0;
    template->lastStep = 0;
    template->processor = 0;

    return template;
}

static void reloadTemplate(marla_FileEntry* fe)
{
    rainback_Template* te = fe->callbackData;
    te->processor = 0;
    te->firstStep = 0;
    te->lastStep = 0;
    rainback_Template_parseString(te, te->fe->data);
    if(te->renderedPage) {
        rainback_removePageFromCache(te->rb, te->renderedPage->cacheKey);
        te->renderedPage = 0;
    }
}

void rainback_Template_parseFile(rainback_Template* template, const char* filepath, const char* watchpath)
{
    marla_FileEntry* fe = marla_Server_getFile(template->rb->session->server, filepath, watchpath);
    fe->callback = reloadTemplate;
    fe->callbackData = template;
    template->fe = fe;
    template->path = filepath;
    rainback_Template_parseString(template, fe->data);
}

void rainback_Template_destroy(rainback_Template* template)
{
    marla_FileEntry_free(template->fe);
    apr_pool_destroy(template->pool);
    free(template);
}

// step->render(rb, page, step->renderData);
static void renderStringContent(mod_rainback* rb, rainback_Page* page, rainback_Context* context, void* stepData)
{
    rainback_Page_append(page, stepData, strlen(stepData));
}

void rainback_Template_stringContent(rainback_Template* template, const char* content)
{
    rainback_TemplateStep* step = rainback_Template_addStep(template);
    step->render = renderStringContent;
    step->renderData = (void*)apr_pstrdup(template->pool, content);
}

// step->render(rb, page, step->renderData);
static void renderMappedContent(mod_rainback* rb, rainback_Page* page, rainback_Context* context, void* stepData)
{
    const char* val = rainback_Context_getString(context, stepData);
    if(val) {
        rainback_Page_append(page, val, strlen(val));
    }
}

void rainback_Template_mappedContent(rainback_Template* template, const char* key)
{
    rainback_TemplateStep* step = rainback_Template_addStep(template);
    step->render = renderMappedContent;
    step->renderData = (void*)apr_pstrdup(template->pool, key);
}

void rainback_Template_render(rainback_Template* tp, rainback_Context* context, rainback_Page* page)
{
    // Process each step.
    for(rainback_TemplateStep* step = tp->firstStep; step; step = step->nextStep) {
        step->render(tp->rb, page, context, step->renderData);
    }
    tp->renderedPage = page;
}

void rainback_renderTemplate(mod_rainback* rb, const char* name, rainback_Context* context, rainback_Page* page)
{
    // Get the template path.
    char* filepath;
    if(name[0] != '/') {
        // Relative path
        if(APR_SUCCESS != apr_filepath_merge(&filepath, rb->session->server->dataRoot, name, 0, rb->session->pool)) {
            marla_die(rb->session->server, "Paths failed to merge.");
        }
    }
    else {
        filepath = apr_pstrdup(rb->session->pool, name);
    }

    // Get the template.
    rainback_Template* tp = apr_hash_get(rb->templates, filepath, APR_HASH_KEY_STRING);
    if(!tp) {
        tp = rainback_Template_new(rb);
        rainback_Template_parseFile(tp, filepath, rb->session->server->dataRoot);
        apr_hash_set(rb->templates, tp->path, APR_HASH_KEY_STRING, tp);
    }

    rainback_Template_render(tp, context, page);
}
