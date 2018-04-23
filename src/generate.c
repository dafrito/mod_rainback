#include "mod_rainback.h"
#include <string.h>
#include <apr_escape.h>

int handleSearch(rainback_Page* page, mod_rainback* rb, const char* uri)
{
    if(strlen(uri) < strlen("/search")) {
        return 1;
    }
    if(strncmp(uri, "/search", strlen("/search"))) {
        return 1;
    }
    char searchquery[1024];
    memset(searchquery, 0, sizeof searchquery);
    if(uri[strlen("/search")] != '?') {
        if(uri[strlen("/search")] != 0) {
            return 1;
        }
        // Accessing the search page directly.
    }
    else {
        if(uri[strlen("/search") + 1] != 'q') {
            return 1;
        }
        if(uri[strlen("/search") + 2] != '=') {
            return 1;
        }
        for(int i = 0;; ++i) {
            searchquery[i] = uri[strlen("/search") + 3 + i];
            if(searchquery[i] == 0) {
                break;
            }
        }
    }
    rainback_generateSearchPage(page, rb, searchquery);
    return 0;
}

void rainback_generatePage(rainback_Page* page, mod_rainback* rb, const char* pageState, const char* url, parsegraph_user_login* login)
{
    if(handleSearch(page, rb, url) == 0) {
        return;
    }
    if(!strcmp(url, "/")) {
        rainback_generateHomepage(page, rb, pageState, login);
    }
    else if(!strcmp(url, "/login") || !strcmp(url, "/login")) {
        rainback_generateLoginPage(page, rb, pageState, login);
    }
    else if(!strcmp(url, "/logout") || !strcmp(url, "/logout")) {
        rainback_generateLogoutPage(page, rb, pageState, login);
    }
    else if(!strcmp(url, "/signup") || !strcmp(url, "/signup/")) {
        rainback_generateSignupPage(page, rb);
    }
    else if(!strcmp(url, "/profile") || !strcmp(url, "/profile/")) {
        rainback_generateProfilePage(page, rb, pageState, login);
    }
    else if(!strcmp(url, "/account") || !strcmp(url, "/account/")) {
        rainback_generateAccountPage(page, rb, login);
    }
    else if(!strcmp(url, "/subscribe") || !strcmp(url, "/subscribe/")) {
        rainback_generateSubscribePage(page, rb, login);
    }
    else if(!strcmp(url, "/contact") || !strcmp(url, "/contact/")) {
        rainback_generateContactPage(page, rb, login);
    }
    else if(!strcmp(url, "/import") || !strcmp(url, "/import/")) {
        rainback_generateImportPage(page, rb, login);
    }
    else {
        int len = strlen("/environment/");
        if(!strncmp(url, "/environment/", len)) {
            rainback_generateEnvironmentPage(page, rb);
        }
    }
}

rainback_Page* rainback_getPageByKey(mod_rainback* rb, const char* cacheKey)
{
    rainback_Page* page = apr_hash_get(rb->cache, cacheKey, APR_HASH_KEY_STRING);
    if(!page) {
        return 0;
    }

    struct timespec now;
    if(0 != clock_gettime(CLOCK_MONOTONIC, &now)) {
        fprintf(stderr, "Failed to retrieve current server time.\n");
        abort();
    }

    if(page->expiry.tv_sec < now.tv_sec || (page->expiry.tv_sec == now.tv_sec && page->expiry.tv_nsec < now.tv_nsec)) {
        // Page has expired.
        apr_hash_set(rb->cache, cacheKey, APR_HASH_KEY_STRING, 0);
        rainback_Page_unref(page);
        return 0;
    }
    // page->expiry.tv_sec >= now.tv_sec && (page->expiry.tv_sec != now.tv_sec || page->expiry.tv_nsec >= now.tv_nsec))
    // Page is still valid.
    rainback_Page_ref(page);
    return page;
}

void rainback_removePageFromCache(mod_rainback* rb, const char* cacheKey)
{
    rainback_Page* page = rainback_getPageByKey(rb, cacheKey);
    apr_hash_set(rb->cache, page->cacheKey, APR_HASH_KEY_STRING, 0);
    rainback_Page_unref(page);
}

rainback_Page* rainback_getPage(mod_rainback* rb, const char* urlState, const char* url, parsegraph_user_login* login)
{
    char buf[1024];
    memset(buf, 0, sizeof buf);

    char* cacheKey = buf;
    cacheKey = strcat(cacheKey, "normal$");

    if(login && login->username) {
        char userIdBuf[32];
        memset(userIdBuf, 0, sizeof userIdBuf);
        snprintf(userIdBuf, sizeof userIdBuf, "%d", login->userId);
        cacheKey = strcat(cacheKey, userIdBuf);
    }
    if(urlState) {
        cacheKey = strcat(cacheKey, urlState);
    }
    cacheKey = strcat(cacheKey, url);

    rainback_Page* page = rainback_getPageByKey(rb, cacheKey);
    if(!page) {
        page = rainback_Page_new(cacheKey);
        rainback_generatePage(page, rb, urlState, url, login);
        struct timespec now;
        if(0 != clock_gettime(CLOCK_MONOTONIC, &now)) {
            fprintf(stderr, "Failed to retrieve current server time.\n");
            abort();
        }
        page->expiry.tv_sec = now.tv_sec + 60;
        page->expiry.tv_nsec = now.tv_nsec;
        apr_hash_set(rb->cache, page->cacheKey, APR_HASH_KEY_STRING, page);
    }
    return page;
}
