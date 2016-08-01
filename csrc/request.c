#include <runtime.h>
#include <http/http.h>

typedef struct request {
    heap h;
    bag b;
    uuid req;
} *request;

static CONTINUATION_1_1(json_input, r, b);
static void json_input(request r, buffer b)
{
}

static CONTINUATION_1_4(response, request,
                        bag, uuid, buffer, register_read);
static void response(request r, bag resp, uuid r, buffer s, register_read reg)
{
    websocket_client(r->h, json, closure(r->h, json_input, r));
    prf("response: %b\n", bag_dump(init, resp));
}

static CONTINUATION_1_1(bag_update, bag, bag);
static void bag_update(bag root, bag deltas)
{
    heap h = init;

    prf("delon: %b\n", bag_dump(init, deltas));
    
    bag_foreach_e(deltas, e, sym(tag), sym(http-request), c) {
        open_http_client(h, root, e,
                         cont(h, response, root, e));
    }
}


void init_request_service(bag b)
{
    prf("register\n");
    register_delta_listener(b, cont(init, bag_update, b));
}

