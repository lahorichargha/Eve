#include <runtime.h>
#include <http/http.h>

static CONTINUATION_2_4(response, bag, uuid,
                        bag, uuid, station, buffer_handler);
static void response(bag b, uuid req, bag resp, uuid r, station s, buffer_handler bh)
{
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

