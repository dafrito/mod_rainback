#ifndef rainback_servermod_INCLUDED
#define rainback_servermod_INCLUDED

#include "marla.h"
#include <parsegraph_user.h>
#include <apr_pools.h>

void routeHook(struct marla_Request* req, void* hookData);
void module_servermod_init(struct marla_Server* server, enum marla_ServerModuleEvent e);
marla_WriteResult makeAboutPage(struct marla_ChunkedPageRequest* cpr);
marla_WriteResult makeCounterPage(struct marla_ChunkedPageRequest* cpr);

// Website sections
marla_WriteResult makeContactPage(struct marla_ChunkedPageRequest* cpr);
marla_WriteResult makeImportPage(struct marla_ChunkedPageRequest* cpr);
marla_WriteResult makeDocumentationPage(struct marla_ChunkedPageRequest* cpr);

marla_WriteResult makeHomepage(struct marla_ChunkedPageRequest* cpr);
marla_WriteResult makeUserpage(struct marla_ChunkedPageRequest* cpr);
marla_WriteResult makeUserEnvironmentsPage(struct marla_ChunkedPageRequest* cpr);
marla_WriteResult makePublicEnvironmentsPage(struct marla_ChunkedPageRequest* cpr);

marla_WriteResult makeLoginPage(struct marla_ChunkedPageRequest* cpr);
marla_WriteResult makeProfilePage(struct marla_ChunkedPageRequest* cpr);

marla_WriteResult makeAccountPage(struct marla_ChunkedPageRequest* cpr);
marla_WriteResult makeSubscriptionPage(struct marla_ChunkedPageRequest* cpr);
marla_WriteResult makePaymentPage(struct marla_ChunkedPageRequest* cpr);
marla_WriteResult makeSearchPage(struct marla_ChunkedPageRequest* cpr);

void rainback_importHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int datalen);
void rainback_profileHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int datalen);
int rainback_processCookie(marla_Request* req, apr_pool_t* pool, ap_dbd_t* dbd, parsegraph_user_login* login, char* cookie);

#endif // rainback_servermod_INCLUDED
