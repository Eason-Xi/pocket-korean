// main/ko_nav.c —— 页面栈。
#include "ko_nav.h"

#include <string.h>

void ko_nav_init(ko_nav_t *nav) {
    memset(nav, 0, sizeof(*nav));
    nav->stack[0].id = KO_SCR_HOME;
    nav->depth = 1;
}

const ko_route_t *ko_nav_top(const ko_nav_t *nav) {
    return &nav->stack[nav->depth - 1];
}

ko_route_t *ko_nav_top_mut(ko_nav_t *nav) {
    return &nav->stack[nav->depth - 1];
}

bool ko_nav_push(ko_nav_t *nav, ko_screen_id_t id, uint16_t arg0, uint16_t arg1) {
    if (nav->depth >= KO_NAV_DEPTH) return false;
    ko_route_t *route = &nav->stack[nav->depth++];
    route->id = id;
    route->arg0 = arg0;
    route->arg1 = arg1;
    route->cursor = 0;
    return true;
}

bool ko_nav_pop(ko_nav_t *nav) {
    if (nav->depth <= 1) return false;
    nav->depth--;
    return true;
}

void ko_nav_replace(ko_nav_t *nav, ko_screen_id_t id, uint16_t arg0, uint16_t arg1) {
    ko_route_t *route = ko_nav_top_mut(nav);
    route->id = id;
    route->arg0 = arg0;
    route->arg1 = arg1;
    route->cursor = 0;
}
