#include <runtime.h>
#include <http/http.h>
#include <bswap.h>

extern thunk ignore;

typedef struct websocket {
    heap h;
    heap buffer_heap;
    buffer reassembly;
    reader client;
    buffer_handler write;
    timer keepalive;
    reader self;
    u32 output_mask;
} *websocket;

typedef enum {
    ws_continuation = 0,
    ws_text = 1,
    ws_binary = 2,
    ws_close = 8,
    ws_ping = 9,
    ws_pong = 10,
} opcodes;


// implement close
void websocket_send(websocket w, int opcode, buffer b, thunk t)
{
    int length = buffer_length(b);
    buffer out = allocate_buffer(w->h, 10);
    buffer_write_byte(out, opcode | 0x80);
    if (length > 65536) {
        buffer_write_byte(out, 127);
        buffer_write_be64(out, length);
    } else {
        if (length > 125) {
            buffer_write_byte(out, 126);
            buffer_write_be16(out, length);
        } else {
            buffer_write_byte(out, length);
        }
    }
    apply(w->write, out, ignore); // reclaim
    apply(w->write, b, t);
}


static CONTINUATION_2_0(send_keepalive, websocket, buffer);
static void send_keepalive(websocket w, buffer b)
{
    websocket_send(w, 0x9, b, ignore);
}

CONTINUATION_1_2(websocket_output_frame, websocket, buffer, thunk);
void websocket_output_frame(websocket w, buffer b, thunk t)
{
    websocket_send(w, 1, b, t);
}

static CONTINUATION_1_2(websocket_input_frame, websocket, buffer, register_read);
static void websocket_input_frame(websocket w, buffer b, register_read reg)
{
    int rlen;

    if (!b) {
        prf ("websocket close\n");
        apply(w->client, 0, ignore);
        return;
    }

    // there is a better approach here, chained buffers, incremental delivery, etc
    buffer_append(w->reassembly, bref(b, 0), buffer_length(b));
    while ((rlen = buffer_length(w->reassembly)) > 0) {
        int offset = 2;
        if (rlen < offset) goto end;
        long length = *(u8 *)bref(w->reassembly, 1) & 0x7f;

        if (length == 126) {
            if (rlen < 4) goto end;
            length = htons(*(u16 *)bref(w->reassembly, 2));
            offset += 2;
        } else {
            if (length == 127) {
                // ok, we are throwing away the top byte, who the hell thought
                // that 1TB wasn't enough per object
                if (rlen< 10) goto end;
                length = htonll(*(u64 *)bref(w->reassembly, 2));
                offset += 8;
            }
        }

        int opcode = *(u8 *)bref(w->reassembly, 0) & 0xf;
        if (opcode == ws_close) {
            apply(w->client, 0, ignore);
            return;
        }

        u32 mask = 0;
        // which should always be the case for client streams
        if (*(u8 *)bref(w->reassembly, 1) & 0x80) {
            mask = *(u32 *)bref(w->reassembly, offset);
            offset += 4;
        }

        if ((rlen - offset) < length) goto end;

        w->reassembly->start += offset;

        if (mask) {
            for (int i=0; i<length; i++) {
                // xxx -figure out how to apply this a word at a time
                *(u8 *)bref(w->reassembly, i) ^= (mask>>((i&3)*8)) & 0xff;
            }
        }

        switch(opcode) {
        case ws_continuation:
            prf("wth continuation\n");
            break;
        case ws_text:
        case ws_binary:
            {
                buffer out = w->reassembly;
                if (buffer_length(w->reassembly) > length) {
                    // leak?
                    out = wrap_buffer(w->h, bref(w->reassembly, 0), length);
                }
                apply(w->client, out, ignore);
                break;
            }
        case ws_ping:
            websocket_send(w, ws_pong, w->reassembly, ignore);
            break;
        case ws_close:
        case ws_pong:
            break;
        default:
            prf("invalid ws frame %d\n", opcode);
        }
        w->reassembly->start += length;
        if (w->reassembly->start == w->reassembly->end) {
            buffer_clear(w->reassembly);
        }
    }
 end:
    // i think we're responsible for freeing this buffer
    apply(reg, w->self);
}

void sha1(buffer d, buffer s);

websocket new_websocket(heap h, reader up)
{
    websocket w = allocate(h, sizeof(struct websocket));
    w->reassembly = allocate_buffer(h, 1000);
    w->h = h;
    w->output_mask = 0;
    w->client = up;
    w->self = cont(h, websocket_input_frame, w);
    return w;
}



static CONTINUATION_1_3(header_response, websocket, bag, uuid, register_read);
static void header_response(websocket w, bag b, uuid n, register_read r)
{
    // xxx - should -  allow plumbing for simple bodies to just dump
    // into the response body
    prf("response body %b\n", bag_dump(init, b));
    apply(r, w->self);
}


static CONTINUATION_3_2(client_connected, websocket, bag, uuid,
                        buffer_handler, register_read);
static void client_connected(websocket w, bag request, uuid rid,
                             buffer_handler wr, register_read r)
{
    bag shadow = create_bag(w->h, request->u);
    vector_insert(shadow->includes, request);

    prf("connecton!\n");
    value header = lookupv(request, rid, sym(headers));
    // i guess we could vary this, but since it doesn't actually provide any security...
    // umm, apparently its to stop a cache(?) from replaying an old session? i just
    // dont get it. it would be kind of idiotic to do in the first place, and
    // if they really wanted to...they..could handle this part as well
    edb_insert(shadow, header, sym(Sec-WebSocket-Key), sym(dGhlIHNhbXBsZSBub25jZQ), 1, 0); /*bku*/
    edb_insert(shadow, header, sym(Upgrade), sym(websocket), 1, 0);
    http_send_request(wr, shadow, rid);
    w->write = wr;
    apply(r, response_header_parser(w->h, cont(w->h, header_response, w)));
}

// xxx - we're returning the write function before we're able to accept input on it
buffer_handler websocket_client(heap h,
                                bag request,
                                uuid rid,
                                reader up)
{
    websocket w = new_websocket(h, up);
    estring host = lookupv(request, rid, sym(host));
    tcp_create_client (h,
                       station_from_string(h, alloca_wrap_buffer(host->body, host->length)),
                       cont(h, client_connected, w, request, rid));
    return(cont(h, websocket_output_frame, w));
}


buffer_handler websocket_send_upgrade(heap h,
                                      bag b,
                                      uuid n,
                                      buffer_handler down,
                                      buffer_handler up,
                                      register_read reg)
{
    websocket w = new_websocket(h, up);
    estring ekey;
    string key;

    if (!(ekey=lookupv(b, n, sym(Sec-WebSocket-Key)))) {
        // something tasier
        return 0;
    }

    key = allocate_buffer(h, ekey->length);
    buffer_append(key, ekey->body, ekey->length);
    string_concat(key, sstring("258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
    buffer sh = allocate_buffer(h, 20);
    sha1(sh, key);
    string r = base64_encode(h, sh);
    buffer upgrade = allocate_buffer(h, 200);

    outline(upgrade, "HTTP/1.1 101 Switching Protocols");
    outline(upgrade, "Upgrade: websocket");
    outline(upgrade, "Connection: Upgrade");
    outline(upgrade, "Sec-WebSocket-Accept: %b", r);
    outline(upgrade, "");

    register_periodic_timer(seconds(5), cont(w->h, send_keepalive, w, allocate_buffer(w->h, 0)));
    w->write = down;
    apply(w->write, upgrade, ignore);
    apply(reg, w->self);
    return(cont(h, websocket_output_frame, w));
}
