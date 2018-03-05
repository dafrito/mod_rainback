#include "mod_rainback.h"

void mod_rainback_init(struct marla_Server* server, enum marla_ServerModuleEvent e)
{
    switch(e) {
    case marla_EVENT_SERVER_MODULE_START:
        marla_Server_addHook(server, marla_ServerHook_ROUTE, routeHook, 0);
        //printf("mod_rainback loaded.\n");
        break;
    case marla_EVENT_SERVER_MODULE_STOP:
        break;
    }
}
