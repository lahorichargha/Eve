#include <runtime.h>
#include <http/http.h>

void print_value_json(buffer out, value v)
{
    switch(type_of(v)) {
    case uuid_space:
        bprintf(out , "{\"type\" : \"uuid\", \"value\" : \"%X\"}", alloca_wrap_buffer(v, UUID_LENGTH));
        break;
    case float_space:
        bprintf(out, "%v", v);
        break;
    case estring_space:
        {
            estring si = v;
            buffer current = alloca_wrap_buffer(si->body, si->length);
            buffer_write_byte(out , '"');
            string_foreach(current, ch) {
                if(ch == '\\' || ch == '"') {
                    bprintf(out , "\\");
                } else if(ch == '\n') {
                    bprintf(out , "\\n");
                    continue;
                }
                buffer_write_byte(out , ch);
            }
            buffer_write_byte(out , '"');
        }
        break;
    default:
        if(v == etrue)
            bprintf(out, "true");
        else if( v == efalse)
            bprintf(out, "false");
        else
          prf ("wth!@ %v\n", v);
    }

}

static char separator[] = {'{', '"', '"', ':', '"', '"', ','};

typedef enum {
    top = 0,
    tag_start,
    tag,
    tvsep,
    val_start,
    val,
    sep
} states;


typedef struct json_parser {
    heap h;
    buffer tag, value;
    json_handler out;
    bag b;
    uuid n,pu;
    states s;
    boolean backslash;
    reader self;
} *json_parser;


static CONTINUATION_1_2(json_input, json_parser, buffer, register_read);
static void json_input(json_parser p, buffer b, register_read reg)
{
    if (!b) {
        apply(p->out, 0, 0);
        return;
    }

    prf("json input: %b\n", b);
    // xxx - use foreach rune
    string_foreach(b, c) {

        // create a bag for this message if one doesn't exist
        if (!p->b) {
            p->pu = generate_uuid();
            p->b = create_bag(p->h, p->pu);
            p->n = generate_uuid();
        }

        if ((p->s == sep) && (buffer_length(p->tag) > 0)){
            estring tes= intern_buffer(p->tag);
            estring ves= intern_buffer(p->value);

            // xxx - should have some interesting source id here
            edb_insert(p->b, p->n, tes, ves, 1, 0);
            buffer_clear(p->tag);
            buffer_clear(p->value);
        }

        if ((c == '}')  && (p->s == sep)) {
            apply(p->out, p->b, p->pu);
            p->b = 0;
            p->s = 0;
            p->backslash = false;
        }

        if ((c == separator[p->s]) && !p->backslash) {
            if (p->s == sep) p->s = tag_start;
            else p->s++;
        } else {
            if (p->backslash && (c == 'n')) c = '\n';
            if (p->backslash && (c == 't')) c = '\t';
            if (c == '\\') {
                p->backslash = true;
            }  else {
                p->backslash = false;
                if (p->s == tag) buffer_write_byte(p->tag, c);
                if (p->s == val) buffer_write_byte(p->value, c);
            }
        }
    }
    apply(reg, p->self);
}

reader parse_json(heap h, json_handler j)
{
    json_parser p= allocate(h, sizeof(struct json_parser));
    p->h = h;
    p->tag = allocate_buffer(h, 10);
    p->value = allocate_buffer(h, 10);
    p->b = 0;
    p->out = j;
    p->self = cont(h, json_input, p);
    return(p->self);
}
