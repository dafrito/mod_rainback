#include "mod_rainback.h"

static int handleSearch(marla_Request* req, mod_rainback* rb)
{
    if(strncmp(req->uri, "/search", strlen("/search"))) {
        return 1;
    }

    char searchquery[1024];
    memset(searchquery, 0, sizeof searchquery);
    if(req->uri[strlen("/search")] != '?') {
        if(req->uri[strlen("/search")] != 0) {
            return 1;
        }
        // Accessing the search page directly.
    }
    else {
        if(req->uri[strlen("/search") + 1] != 'q') {
            return 1;
        }
        if(req->uri[strlen("/search") + 2] != '=') {
            return 1;
        }
        for(int i = 0;; ++i) {
            searchquery[i] = req->uri[strlen("/search") + 3 + i];
            if(searchquery[i] == 0) {
                break;
            }
        }
    }

    req->handler = rainback_searchHandler;
    req->handlerData = rainback_SearchResponse_new(req, rb);
    return 0;
}

void mod_rainback_route(struct marla_Request* req, void* hookData)
{
    mod_rainback* rb = hookData;
    struct marla_ChunkedPageRequest* cpr;
    if(handleSearch(req, rb) == 0) {
        return;
    }
    if(!strcmp(req->uri, "/")) {
        req->handler = rainback_homepageHandler;
        req->handlerData = rainback_HomepageResponse_new(req, rb);
        return;
    }
    if(!strcmp(req->uri, "/login") || !strcmp(req->uri, "/login/")) {
        req->handler = rainback_loginHandler;
        req->handlerData = rainback_LoginResponse_new(req, rb);
        return;
    }
    if(!strcmp(req->uri, "/logout") || !strcmp(req->uri, "/logout/")) {
        req->handler = rainback_logoutHandler;
        req->handlerData = rainback_LogoutResponse_new(req, rb);
        return;
    }
    if(!strcmp(req->uri, "/signup") || !strcmp(req->uri, "/signup/")) {
        req->handler = rainback_signupHandler;
        req->handlerData = rainback_SignupResponse_new(req, rb);
        return;
    }
    if(!strcmp(req->uri, "/profile") || !strcmp(req->uri, "/profile/")) {
        req->handler = rainback_profileHandler;
        req->handlerData = rainback_ProfileResponse_new(rb);
        return;
    }
    if(!strcmp(req->uri, "/authenticate") || !strcmp(req->uri, "/authenticate/")) {
        req->handler = rainback_authenticateHandler;
        req->handlerData = rainback_AuthenticateResponse_new(req, rb);
        return;
    }
    if(!strcmp(req->uri, "/account") || !strcmp(req->uri, "/account/")) {
        req->handler = rainback_accountHandler;
        req->handlerData = rainback_AccountResponse_new(req, rb);
        return;
    }
    if(!strcmp(req->uri, "/subscribe") || !strcmp(req->uri, "/subscribe/")) {
        req->handler = rainback_subscribeHandler;
        req->handlerData = rainback_SubscribeResponse_new(req, rb);
        return;
    }
    if(!strcmp(req->uri, "/contact") || !strcmp(req->uri, "/contact/")) {
        req->handler = rainback_contactHandler;
        req->handlerData = rainback_ContactResponse_new(req, rb);
        return;
    }
    if(!strcmp(req->uri, "/import") || !strcmp(req->uri, "/import/")) {
        req->handler = rainback_importHandler;
        req->handlerData = rainback_ImportResponse_new(req, rb);
        return;
    }
    if(!strcmp(req->uri, "/environment/live") || !strcmp(req->uri, "/environment/live")) {
        rainback_live_environment_install(rb, req);
        return;
    }
    int len = strlen("/environment");
    if(!strncmp(req->uri, "/environment", len)) {
        // Check for suitable termination
        if(req->uri[len] != 0 && req->uri[len] != '/' && req->uri[len] != '?' && req->uri[len] != '.' ) {
            // Not really handled.
            return;
        }
        if(strlen(req->uri) - len > 2) {
            req->handler = rainback_environmentHandler;
            req->handlerData = rainback_EnvironmentResponse_new(req, rb);
            return;
        }
    }
    /*if(!strncmp(req->uri, "/user", 5)) {
        // Check for suitable termination
        if(req->uri[5] != 0 && req->uri[5] != '/' && req->uri[5] != '?') {
            // Not really handled.
            return;
        }
        req->handler = rainback_userHandler;
        req->handlerData = rainback_UserHandlerData_new(req, rb);
        return;
    }
    if(!strcmp(req->uri, "/payment")) {
        req->handler = rainback_paymentHandler;
        req->handlerData = rainback_PaymentHandlerData_new(req, rb);
        return;
    }
    if(!strcmp(req->uri, "/profile")) {
        req->handler = rainback_profileHandler;
        req->handlerData = rainback_ProfileResponse_new(req, rb);
        return;
    }
*/

    req->handler = marla_fileHandler;
}
