#include "mod_rainback.h"
#include <string.h>

#define BUFSIZE 4096

struct rainback_HomepageResponse {
mod_rainback* rb;
marla_Ring* input;
apr_pool_t* pool;
parsegraph_user_login login;
};

rainback_HomepageResponse* rainback_HomepageResponse_new(marla_Request* req, mod_rainback* rb)
{
    rainback_HomepageResponse* resp = malloc(sizeof(*resp));
    memset(&resp->login, 0, sizeof(resp->login));
    resp->rb = rb;
    if(apr_pool_create(&resp->pool, resp->rb->session->pool) != APR_SUCCESS) {
        marla_killRequest(req, "Failed to create request handler memory pool.");
    }
    resp->input = marla_Ring_new(BUFSIZE);
    return resp;
}

void rainback_HomepageResponse_destroy(rainback_HomepageResponse* resp)
{
    apr_pool_destroy(resp->pool);
    marla_Ring_free(resp->input);
    free(resp);
}

static int acceptRequest(marla_Request* req)
{
    rainback_HomepageResponse* resp = req->handlerData;
    if(!strcmp(req->method, "GET") || !strcmp(req->method, "HEAD")) {
        req->handler = rainback_pageHandler;
        req->handlerData = rainback_getPage(resp->rb, "", req->uri, &resp->login);
        rainback_HomepageResponse_destroy(resp);
        return 1;
    }

    return 0;
}

static marla_WriteResult readRequestBody(marla_Request* req, marla_WriteEvent* we)
{
    if(we->length == 0) {
        req->writeStage = marla_CLIENT_REQUEST_DONE_READING;
        return marla_WriteResult_CONTINUE;
    }
    marla_killRequest(req, "Unexpected input given in %s request.", req->method);
    return marla_WriteResult_KILLED;
}

void rainback_homepageHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen)
{
    rainback_HomepageResponse* resp = req->handlerData;

    marla_WriteEvent* we;
    switch(ev) {
    case marla_EVENT_HEADER:
        if(strcmp("Cookie", data)) {
            break;
        }
        rainback_authenticateByCookie(req, resp->rb, &resp->login, data + dataLen);
        break;
    case marla_EVENT_ACCEPTING_REQUEST:
        *((int*)data) = acceptRequest(req);
        break;
    case marla_EVENT_REQUEST_BODY:
        we = data;
        we->status = readRequestBody(req, we);
        break;
    case marla_EVENT_MUST_WRITE:
        marla_killRequest(req, "HomepageHandler must not process write events.");
        break;
    case marla_EVENT_DESTROYING:
        req->handlerData = 0;
        rainback_HomepageResponse_destroy(resp);
        break;
    }
}

static void rainback_generateUserpage(rainback_Page* page, mod_rainback* rb, const char* pageState, parsegraph_user_login* login)
{
    char fullResponse[8*8192];
    memset(fullResponse, 0, sizeof fullResponse);
    char part[8192];
    int len = snprintf(part, sizeof part,
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>%s</title>"
    "<link rel=\"stylesheet\" type=\"text/css\" href=\"rainback.css\">"
    "<link rel=\"icon\" type=\"image/png\" href=\"favicon.png\" sizes=\"16x16\">"
    "<style>"
    "</style>"
"</head>"
"<body>"
"<nav>"
    "<p style=\"text-align: center\">"
    "<a href=/><img id=logo src=\"nav-side-logo.png\"></img></a>"
    "</p>"
"</nav>"
"<main>"
    "<div class=links>"
        "<form id=search action=\"/search\">"
        "<input name=q></input> <input type=submit value=Search></input>"
        "</form>"
        "<form id=logout method=post action=\"/logout\"><input type=submit value=\"Log out\"></form> "
        "<a href=\"/profile\"><span class=\"bud\">Profile</span></a> "
        "<a href=\"/import\"><span class=\"bud import-button\">Import</span></a>"
    "</div>"
    "<div class=block style=\"clear:both; overflow: hidden\">"
    "<h1>%s</h1>"
    "<p>"
"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed urna purus, tempus in fermentum nec, blandit blandit nunc. Mauris aliquam augue ac faucibus faucibus. Cras faucibus molestie augue a interdum. Maecenas laoreet magna lectus, eget pharetra magna pharetra et. Ut eu mi placerat, interdum diam ac, tristique odio. Nulla facilisi. Vestibulum eu nisi accumsan, vehicula felis at, rutrum felis. Cras id nunc est. Aenean nec gravida dolor. Aenean velit erat, tempus non aliquam vehicula, sagittis vel mi. Aenean facilisis efficitur risus, fermentum lobortis massa porttitor at. Nulla facilisi. Curabitur metus nulla, mollis vel purus in, eleifend accumsan eros. Sed accumsan sed leo eu porttitor. Vestibulum erat dui, lobortis in ornare id, sodales porttitor nunc. Nam vestibulum, ipsum lacinia vulputate malesuada, sapien dolor iaculis mi, quis scelerisque enim libero ut ipsum."
"<p>"
"Fusce ante augue, volutpat et nunc vitae, accumsan laoreet massa. Aliquam erat volutpat. Fusce pharetra, sem ut aliquet hendrerit, lacus massa pulvinar diam, sit amet imperdiet mi magna eget neque. Duis vel nisl velit. Donec condimentum lacus eget euismod faucibus. Sed imperdiet eros non ex dapibus tincidunt id eu orci. Vivamus eu ante nibh."
"<p>"
"Vivamus maximus libero ut placerat mollis. Fusce in blandit eros. Suspendisse dui mauris, cursus in massa vitae, vestibulum tincidunt sapien. Sed euismod elit in dolor semper ullamcorper. Praesent vehicula velit in fermentum hendrerit. Praesent molestie ligula eget leo vulputate, id mollis sapien finibus. Pellentesque quam mi, varius sed dapibus in, iaculis non ligula. Nulla enim quam, iaculis et massa vitae, suscipit pretium tellus."
"<p>"
"In suscipit efficitur orci, vel lacinia tellus elementum ac. Cras justo diam, blandit sit amet tincidunt quis, tristique id turpis. Fusce ac libero eleifend, efficitur diam et, faucibus tortor. Donec eu ligula neque. Phasellus mollis at turpis vitae sagittis. Nunc eros mi, lobortis sit amet nisl sed, mattis vestibulum quam. Duis a pellentesque massa. Nunc vitae ipsum consectetur, rhoncus est imperdiet, ultrices erat. Integer in bibendum libero. Donec semper odio sem, in semper neque pellentesque sed. Suspendisse elementum ipsum quis magna fringilla, ut condimentum metus vehicula. Vivamus maximus pharetra mattis. Mauris a tincidunt ante, quis molestie est. Nam convallis pharetra sagittis. Pellentesque habitant morbi tristique senectus et netus et malesuada fames ac turpis egestas. Nulla eget pretium felis."
"<p>"
"Sed pharetra elit libero, vel interdum massa posuere vel. Ut malesuada rutrum blandit. Sed ut nunc eu ligula semper molestie. Curabitur pulvinar felis eros, eget sollicitudin ligula dignissim bibendum. Aliquam vehicula lectus vitae congue fringilla. Nunc at magna non tellus tincidunt dictum. In pharetra, felis mattis sollicitudin pharetra, augue elit dictum tortor, eu imperdiet lectus risus hendrerit ex. Praesent consequat bibendum mauris at facilisis. Quisque tincidunt sodales porta. Vivamus accumsan mi ut vehicula auctor. Mauris consequat lectus vel fringilla tempus. Quisque interdum, quam vitae ultrices fringilla, enim justo varius lorem, in pulvinar mi tortor a arcu. Praesent condimentum augue in dolor venenatis, at imperdiet quam dignissim. Phasellus feugiat justo sed lectus ultrices, sit amet lacinia erat mollis. Aliquam nec diam nisl. Vestibulum sit amet eros at mi imperdiet sagittis nec interdum ligula.",
    (login && login->username) ? login->username : "Rainback",
    (login && login->username) ? login->username : "no one",
    (login && login->username) ? login->username : "Anonymous");
    size_t bodylen = strlen(part);
    strcat(fullResponse, part);

    /*for( each user environment) {
        len = snprintf(part, sizeof part,
        "<div>"
        "Environment X"
        "</div>"
        );
        bodylen += strlen(part);
        strcat(fullResponse, part);
    }*/

    len = snprintf(part, sizeof part,
    "</div>"
"</main>"
"<div style=\"clear: both\"></div>"
"<div style=\"display: block; text-align: center; margin: 1em 0\">"
    "<div class=slot style=\"display: inline-block;\">"
        "&copy; 2018 <a href='https://rainback.com'>Rainback, Inc.</a> All rights reserved. <a href=/contact><span class=\"bud\">Contact Us</span></a>"
    "</div>"
"</div>"
"</body>"
"</html>"
    );
    bodylen += strlen(part);
    strcat(fullResponse, part);

    char buf[8192];
    len = snprintf(buf, sizeof buf,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        bodylen
    );
    rainback_Page_write(page, buf, len);
    rainback_Page_endHead(page);
    rainback_Page_write(page, fullResponse, bodylen);
}

void rainback_generateHomepage(rainback_Page* page, mod_rainback* rb, const char* pageState, parsegraph_user_login* login)
{
    if(login && login->username) {
        return rainback_generateUserpage(page, rb, pageState, login);
    }

    char body[8192];
    int len = snprintf(body, sizeof body,
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Rainback</title>"
    "<link rel=\"stylesheet\" type=\"text/css\" href=\"rainback.css\">"
    "<link rel=\"icon\" type=\"image/png\" href=\"favicon.png\" sizes=\"16x16\">"
"</head>"
"<body>"
"<nav>"
    "<p style=\"text-align: center\">"
    "<a href=/><img id=logo src=\"nav-side-logo.png\"></img></a>"
    "</p>"
    "<div style=\"text-align: center\">"
    "</div>"
    "<p style=\"text-align: center\">"
    "</p>"
"</nav>"
"<main>"
    "<div class=links>"
        "<form id=search action=\"/search\">"
        "<input name=q></input> <input type=submit value=Search></input>"
        "</form>"
        "<a href=/login><span class=\"bud\" style=\"background-color: greenyellow\">Log in</span></a> "
        "<a href=/signup><span class=\"bud\" style=\"background-color: gold\">Sign up</span></a> "
        "<a href=\"/import\"><span class=\"bud\">Import</span></a>"
    "</div>"
    "<div class=block style=\"clear:both\">"
        "<h1>Rainback</h1>"
"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed urna purus, tempus in fermentum nec, blandit blandit nunc. Mauris aliquam augue ac faucibus faucibus. Cras faucibus molestie augue a interdum. Maecenas laoreet magna lectus, eget pharetra magna pharetra et. Ut eu mi placerat, interdum diam ac, tristique odio. Nulla facilisi. Vestibulum eu nisi accumsan, vehicula felis at, rutrum felis. Cras id nunc est. Aenean nec gravida dolor. Aenean velit erat, tempus non aliquam vehicula, sagittis vel mi. Aenean facilisis efficitur risus, fermentum lobortis massa porttitor at. Nulla facilisi. Curabitur metus nulla, mollis vel purus in, eleifend accumsan eros. Sed accumsan sed leo eu porttitor. Vestibulum erat dui, lobortis in ornare id, sodales porttitor nunc. Nam vestibulum, ipsum lacinia vulputate malesuada, sapien dolor iaculis mi, quis scelerisque enim libero ut ipsum."
"<p>"
"Fusce ante augue, volutpat et nunc vitae, accumsan laoreet massa. Aliquam erat volutpat. Fusce pharetra, sem ut aliquet hendrerit, lacus massa pulvinar diam, sit amet imperdiet mi magna eget neque. Duis vel nisl velit. Donec condimentum lacus eget euismod faucibus. Sed imperdiet eros non ex dapibus tincidunt id eu orci. Vivamus eu ante nibh."
"<p>"
"Vivamus maximus libero ut placerat mollis. Fusce in blandit eros. Suspendisse dui mauris, cursus in massa vitae, vestibulum tincidunt sapien. Sed euismod elit in dolor semper ullamcorper. Praesent vehicula velit in fermentum hendrerit. Praesent molestie ligula eget leo vulputate, id mollis sapien finibus. Pellentesque quam mi, varius sed dapibus in, iaculis non ligula. Nulla enim quam, iaculis et massa vitae, suscipit pretium tellus."
"<p>"
"In suscipit efficitur orci, vel lacinia tellus elementum ac. Cras justo diam, blandit sit amet tincidunt quis, tristique id turpis. Fusce ac libero eleifend, efficitur diam et, faucibus tortor. Donec eu ligula neque. Phasellus mollis at turpis vitae sagittis. Nunc eros mi, lobortis sit amet nisl sed, mattis vestibulum quam. Duis a pellentesque massa. Nunc vitae ipsum consectetur, rhoncus est imperdiet, ultrices erat. Integer in bibendum libero. Donec semper odio sem, in semper neque pellentesque sed. Suspendisse elementum ipsum quis magna fringilla, ut condimentum metus vehicula. Vivamus maximus pharetra mattis. Mauris a tincidunt ante, quis molestie est. Nam convallis pharetra sagittis. Pellentesque habitant morbi tristique senectus et netus et malesuada fames ac turpis egestas. Nulla eget pretium felis."
"<p>"
"Sed pharetra elit libero, vel interdum massa posuere vel. Ut malesuada rutrum blandit. Sed ut nunc eu ligula semper molestie. Curabitur pulvinar felis eros, eget sollicitudin ligula dignissim bibendum. Aliquam vehicula lectus vitae congue fringilla. Nunc at magna non tellus tincidunt dictum. In pharetra, felis mattis sollicitudin pharetra, augue elit dictum tortor, eu imperdiet lectus risus hendrerit ex. Praesent consequat bibendum mauris at facilisis. Quisque tincidunt sodales porta. Vivamus accumsan mi ut vehicula auctor. Mauris consequat lectus vel fringilla tempus. Quisque interdum, quam vitae ultrices fringilla, enim justo varius lorem, in pulvinar mi tortor a arcu. Praesent condimentum augue in dolor venenatis, at imperdiet quam dignissim. Phasellus feugiat justo sed lectus ultrices, sit amet lacinia erat mollis. Aliquam nec diam nisl. Vestibulum sit amet eros at mi imperdiet sagittis nec interdum ligula."
        "</div>"
"</main>"
"<div style=\"clear: both\"></div>"
"<div style=\"display: block; text-align: center; margin: 1em 0\">"
    "<div class=slot style=\"display: inline-block;\">"
        "&copy; 2018 <a href='https://rainback.com'>Rainback, Inc.</a> All rights reserved. <a href=/contact><span class=\"bud\">Contact Us</span></a>"
    "</div>"
"</div>"
"</body>"
"</html>"
    );
    size_t bodylen = strlen(body);

    char buf[8192];
    len = snprintf(buf, sizeof buf,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        bodylen
    );
    rainback_Page_write(page, buf, len);
    rainback_Page_endHead(page);
    rainback_Page_write(page, body, bodylen);
}
