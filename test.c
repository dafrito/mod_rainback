#include <stdio.h>
#include <unistd.h>
#include <mod_rainback.h>

struct counter {
char numbuf[16];
int c;
};

static void* countSome(rainback_Template* tp, rainback_Context* context, void** savePtr, void* iterPtr)
{
    long max = (long)iterPtr;
    struct counter* p = 0;
    if(*savePtr) {
        p = *savePtr;
        if(p->c >= max) {
            // Terminate.
            return 0;
        }
        // Increment.
        p->c = p->c + 1;
    }
    else {
        // Initialize.
        p = apr_palloc(tp->pool, sizeof(struct counter));
        memset(p->numbuf, 0, sizeof(p->numbuf));
        p->c = 1;
        *savePtr = p;
    }
    snprintf(p->numbuf, sizeof(p->numbuf), "%d", p->c);
    return p->numbuf;
}

void test_basic_template(mod_rainback* rb)
{
    rainback_Template* te = rainback_Template_new(rb);
    rainback_Template_parseFile(te, "test.html", rb->session->server->dataRoot);

    rainback_Context* ctx = rainback_Context_new(rb->session->pool);
    rainback_Context_setString(ctx, "title", "Rainback");
    rainback_Context_setString(ctx, "content", "No time");
    rainback_Context_setEnumerator(ctx, "list", countSome, (void*)10);

    rainback_Page* page = rainback_Page_new(0);
    rainback_Template_render(te, ctx, page);
    write(2, page->data, page->length);
    rainback_Page_unref(page);

    rainback_Template_destroy(te);
}

void test_template(mod_rainback* rb)
{
    rainback_Template* te = rainback_Template_new(rb);
    rainback_Template_parseFile(te, "test.html", rb->session->server->dataRoot);

    rainback_Context* ctx = rainback_Context_new(rb->session->pool);
    rainback_Context_setString(ctx, "title", "Rainback");
    rainback_Context_setString(ctx, "content", "No time");
    rainback_Context_setEnumerator(ctx, "list", countSome, (void*)10);

    rainback_Page* page = rainback_Page_new(0);
    rainback_Template_render(te, ctx, page);
    write(2, page->data, page->length);

    rainback_Template_destroy(te);
    rainback_Page_unref(page);
}

int main()
{
    apr_initialize();
    marla_Server server;
    marla_Server_init(&server);

    mod_rainback* rb = mod_rainback_new(&server);
    test_template(rb);
    test_basic_template(rb);
    mod_rainback_destroy(rb);
    marla_Server_free(&server);
    apr_terminate();
    return 0;
}
