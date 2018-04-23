#include <stdio.h>
#include <unistd.h>
#include <mod_rainback.h>

struct counter {
char numbuf[16];
int c;
};

static void* countSome(rainback_Template* tp, apr_hash_t* context, void** savePtr)
{
    struct counter* p = 0;
    if(*savePtr) {
        p = *savePtr;
        if(p->c >= 10) {
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

int main()
{
    apr_initialize();
    marla_Server server;
    marla_Server_init(&server);

    mod_rainback* rb = mod_rainback_new(&server);

    rainback_Template* te = rainback_Template_new(rb);
    unsigned char* f = strdup(
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "    <title><%=title%></title>\n"
    "</head>\n"
    "<body>\n"
    "<p><%=content%></p>\n"
    "<ul>\n"
    "<% foreach v in list %>"
    "    <li>\n"
    "    <p><%=v%></p>\n"
    "    <ul>\n"
        "<% foreach p in list %>"
    "        <li><%=p%></li>\n"
        "<% endfor %>"
    "    </ul>\n"
    "    </li>\n"
    "<% endfor %>"
    "</ul>\n"
    "</body>\n"
    "</html>\n");
    //rainback_Template_parseString(te, f);
    rainback_Template_parseFile(te, "test.html", server.dataRoot);

    apr_hash_t* h = apr_hash_make(rb->session->pool);

    apr_hash_set(h, "title", APR_HASH_KEY_STRING, "Rainback");
    apr_hash_set(h, "content", APR_HASH_KEY_STRING, "No time.");
    apr_hash_set(h, "list", APR_HASH_KEY_STRING, countSome);

    rainback_Page* page = rainback_Page_new(0);
    rainback_Template_render(te, h, page);
    write(2, page->data, page->length);

    rainback_Template_destroy(te);
    free(f);
    rainback_Page_unref(page);
    mod_rainback_destroy(rb);
    marla_Server_free(&server);
    apr_terminate();
    return 0;
}
