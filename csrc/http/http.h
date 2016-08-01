typedef struct http_server *http_server;
http_server create_http_server(station p);

void http_send_request(buffer_handler w, bag b, uuid n);

void send_http_response(heap h,
                        buffer_handler write,
                        string type,
                        buffer b);

void register_http_service(http_server s,
                           string url,
                           thunk apply);

void register_http_file(http_server s,
                        string url,
                        string pathname,
                        string mimetype);


string base64_encode(heap h, buffer x);

void register_websocket_service(heap h,
                                http_server s,
                                string url,
                                thunk connect);

void register_static_content(http_server h, char *url, char *content_type, buffer b, char *);

#define outline(__b, __format, ...)\
    bbprintf(__b, sstring(__format), ## __VA_ARGS__);\
    buffer_append(__b, "\r\n", 2);

// maybe deconstruct the headers across the interface instead of the raw business
typedef closure(http_handler, bag, uuid, station, buffer_handler);

buffer_handler websocket_send_upgrade(heap h,
                                      bag b,
                                      uuid n,
                                      buffer_handler down,
                                      buffer_handler up,
                                      register_read reg);

// should be asynch...but you know
typedef closure(http_service, buffer_handler, bag, uuid, register_read);
void http_register_service(http_server, http_service, string);

typedef closure(json_handler, bag, uuid, thunk);
buffer_handler parse_json(heap h, uuid pu, json_handler j);
void print_value_json(buffer out, value v);

typedef closure(header_handler, bag, uuid, buffer, register_read);

reader response_header_parser(heap h, header_handler result_handler);
reader request_header_parser(heap h, header_handler result_handler);

typedef struct client *client;
client open_http_client(heap h, bag s, uuid request, header_handler response);

void http_send_header(buffer_handler w, bag b, uuid n, value first, value second, value third);
