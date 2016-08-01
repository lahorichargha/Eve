#include <runtime.h>

#define multibag_foreach(__m, __u, __b)  if(__m) table_foreach(__m, __u, __b)

// debuggin
static estring bagname(evaluation e, uuid u)
{

    estring bagname = efalse;
    table_foreach(e->scopes, n, u2) if (u2 ==u) return(n);
    return(intern_cstring("missing bag?"));
}

static inline int multibag_count(table m)
{
    int count = 0;
    multibag_foreach(m, u, b)
        count += edb_size(b);
    return count;
}

static boolean compare_sets(table set, table retain, table destroy)
{
    bag d;
    if (!retain != !destroy) return false;
    if (!retain) return true;

    table_foreach(set, u, _) {
        bag s = table_find(retain, u);
        bag d = table_find(destroy, u);

        if (!s != !d) return false;
        if (s) {
            if (edb_size(d) != edb_size(s)){
              /* prf("Mismatched sizes, outtie!\n"); */
              /*   bag_foreach(s, e, a, v, c, _) { */
              /*       if (count_of(d, e, a, v) != c) { */
              /*         prf("EAV, %v %v %v %d --> %d\n", e, a, v, c, count_of(d, e, a, v)); */
              /*       } */
              /*   } */
                return false;
            }
            bag_foreach(s, e, a, v, c, _) {
                if (count_of(d, e, a, v) != c) {
                    return false;
                }
            }
        }
    }
    return true;
}

static CONTINUATION_1_5(insert_f, evaluation, uuid, value, value, value, multiplicity);
static void insert_f(evaluation ev, uuid u, value e, value a, value v, multiplicity m)
{
    bag b;
    if (!ev->block_solution)
        ev->block_solution = create_value_table(ev->working);

    if (!(b = table_find(ev->block_solution, u))) {
        table_set(ev->block_solution, u, b = create_bag(ev->working, u));
    }
    edb_insert(b, e, a, v, m, ev->bk->name);
}



static CONTINUATION_2_5(shadow_f_by_p_and_t, evaluation, listener, value, value, value, multiplicity, uuid);
static void shadow_f_by_p_and_t(evaluation ev, listener result, value e, value a, value v, multiplicity m, uuid bku)
{
    int total = 0;

    if (m > 0) {
        bag b;
        multibag_foreach(ev->t_solution, u, b) {
            total += count_of(b, e, a, v);
        }
        if (total <= 0) {
            apply(result, e, a, v, m, bku);
        }
    }
}

static CONTINUATION_2_5( shadow_t_by_f, evaluation, listener, value, value, value, multiplicity, uuid);
static void shadow_t_by_f(evaluation ev, listener result, value e, value a, value v, multiplicity m, uuid bku)
{
    int total = 0;

    if (m > 0) {
        bag b;
        table_foreach(ev->f_bags, u, _) {
            if (ev->last_f_solution && (b = table_find(ev->last_f_solution, u)))
                total += count_of(b, e, a, v);
        }
        if (total >= 0)
            apply(result, e, a, v, m, bku);
    }
}


static CONTINUATION_2_5(shadow_p_by_t_and_f, evaluation, listener,
                        value, value, value, multiplicity, uuid);
static void shadow_p_by_t_and_f(evaluation ev, listener result,
                                value e, value a, value v, multiplicity m, uuid bku)
{
    int total = 0;

    if (m > 0) {
        bag b;
        multibag_foreach(ev->t_solution, u, b) {
            total += count_of(b, e, a, v);
        }

        if (total >= 0) {
            total = 0;
            table_foreach(ev->f_bags, u, _) {
                if (ev->last_f_solution && (b = table_find(ev->last_f_solution, u)))
                    total += count_of(b, e, a, v);
            }
            if (total >= 0)
                apply(result, e, a, v, m, bku);
        }
    }
}

static CONTINUATION_1_5(merge_scan, evaluation, int, listener, value, value, value);
static void merge_scan(evaluation ev, int sig, listener result, value e, value a, value v)
{
    table_foreach(ev->persisted, u, b) {
        bag proposed;
        edb_scan(b, sig,
                 cont(ev->working, shadow_p_by_t_and_f, ev, result),
                 e, a, v);
        if (ev->t_solution && (proposed = table_find(ev->t_solution, u))) {
            edb_scan(proposed, sig,
                     cont(ev->working, shadow_t_by_f, ev, result),
                     e, a, v);
        }
    }

    table_foreach(ev->f_bags, u, _) {
        bag last;
        if (ev->last_f_solution && (last = table_find(ev->last_f_solution, u))){
            edb_scan(last, sig,
                     cont(ev->working, shadow_f_by_p_and_t, ev, result),
                     e, a, v);
        }
    }
}

static CONTINUATION_1_0(evaluation_complete, evaluation);
static void evaluation_complete(evaluation s)
{
    s->non_empty = true;
}


static boolean merge_solution_into_t(evaluation ev, uuid u, bag s)
{
    static int runcount = 0;
    runcount++;
    bag bd;
    boolean result = false;

    if (!ev->t_solution) 
        ev->t_solution = create_value_table(ev->working);
    
    if (!(bd = table_find(ev->t_solution, u))) {
        table_set(ev->t_solution, u, s);
        return true;
    } else {
        bag_foreach(s, e, a, v, count, bk) {
            int old_count = count_of(bd, e, a, v);
            if ((count > 0) && (old_count == 0)) {
                result = true;
                edb_insert(bd, e, a, v, 1, bk);
            }
            if (count < 0) {
                result = true;
                edb_insert(bd, e, a, v, -1, bk);
            }
        }
    }
    return result;
}

static void merge_multibag_bag(evaluation ev, table *d, uuid u, bag s)
{
    bag bd;
    if (!*d) {
        *d = create_value_table(ev->working);
    }

    if (!(bd = table_find(*d, u))) {
        table_set(*d, u, s);
    } else {
        bag_foreach(s, e, a, v, m, bku) {
            edb_insert(bd, e, a, v, m, bku);
        }
    }
}

static void run_block(evaluation ev, block bk)
{
    heap bh = allocate_rolling(pages, sstring("block run"));
    bk->ev->block_solution = 0;
    bk->ev->non_empty = false;
    ev->bk = bk;
    ticks start = rdtsc();
    value *r = allocate(ev->working, (bk->regs + 1)* sizeof(value));

    apply(bk->head, bh, 0, op_insert, r);
    // flush shouldn't need r
    apply(bk->head, bh, 0, op_flush, r);

    ev->cycle_time += rdtsc() - start;

    if (bk->ev->non_empty)
        multibag_foreach(ev->block_solution, u, b)
            merge_multibag_bag(ev, &ev->solution, u, b);

    destroy(bh);
}

static void fixedpoint(evaluation ev)
{
    long iterations = 0;
    vector counts = allocate_vector(ev->working, 10);
    boolean again;

    ticks start_time = now();
    ev->t = start_time;
    ev->solution = 0;

    do {
        again = false;
        ev->solution =  0;
        do {
            iterations++;
            ev->last_f_solution = ev->solution;
            ev->solution = 0;

            if (ev->event_blocks)
                vector_foreach(ev->event_blocks, b)
                    run_block(ev, b);
            vector_foreach(ev->blocks, b)
                run_block(ev, b);
        } while(!compare_sets(ev->f_bags, ev->solution, ev->last_f_solution));
        
        multibag_foreach(ev->solution, u, b)
            if (table_find(ev->persisted, u))
                again |= merge_solution_into_t(ev, u, b);
        
        vector_insert(counts, box_float((double)iterations));
        iterations = 0;
        ev->event_blocks = 0;
    } while(again);


    boolean changed_persistent = false;
    multibag_foreach(ev->t_solution, u, b) {
        bag bd;
        if ((bd = table_find(ev->persisted, u))) {
            bag_foreach((bag)b, e, a, v, m, bku) {
                //                prf("persist from (delta p) %v %v %v %d\n", e, a, v, m);
                changed_persistent = true;
                edb_insert(bd, e, a, v, m, bku);
            }
        }
    }

    if (changed_persistent) {
        table_foreach(ev->persisted, _, b)  {
            table_foreach(((bag)b)->listeners, t, _)
                if (t != ev->run)
                    apply((thunk)t);
        }
    }

    // allow the deltas to also see the updated base by applying
    // them after
    multibag_foreach(ev->t_solution, u, b) {
        bag bd;
        prf("delta listeners: %v\n", bagname(ev, u));
        if ((bd = table_find(ev->persisted, u))) {
            prf("delta listeners: %v\n", bagname(ev, u));
            table_foreach(bd->delta_listeners, t, _)
                apply((bag_handler)t, b);
        }
    }

    apply(ev->complete, ev->solution, ev->counters);

    ticks end_time = now();
    table_set(ev->counters, intern_cstring("time"), (void *)(end_time - start_time));
    table_set(ev->counters, intern_cstring("iterations"), (void *)iterations);

    prf ("fixedpoint in %t seconds, %d blocks, %V iterations, %d input bags, %d output bags\n",
         end_time-start_time, vector_length(ev->blocks),
         counts, table_elements(ev->scopes),
         ev->solution?table_elements(ev->solution):0);
    destroy(ev->working);
    table_set(ev->counters, intern_cstring("cycle-time"), (void *)ev->cycle_time);
}

static void clear_evaluation(evaluation ev)
{
    ev->working = allocate_rolling(pages, sstring("working"));
    ev->t_solution = 0;
    ev->t = now();
}

void inject_event(evaluation ev, buffer b, boolean tracing)
{
    buffer desc;
    clear_evaluation(ev);
    ev->event_blocks = 0;
    vector c = compile_eve(ev->working, b, tracing, &desc);
    vector_foreach(c, i) {
        if (!ev->event_blocks)
            ev->event_blocks = allocate_vector(ev->working, vector_length(c));
        vector_insert(ev->event_blocks, build(ev, i));
    }
    fixedpoint(ev);
}

CONTINUATION_1_0(run_solver, evaluation);
void run_solver(evaluation ev)
{
    clear_evaluation(ev);
    fixedpoint(ev);
}

void close_evaluation(evaluation ev)
{
    table_foreach(ev->persisted, uuid, b)
        deregister_listener(b, ev->run);

    vector_foreach(ev->blocks, b)
        block_close(b);

    destroy(ev->h);
}

evaluation build_evaluation(table scopes, table persisted, evaluation_result r)
{
    heap h = allocate_rolling(pages, sstring("eval"));
    evaluation ev = allocate(h, sizeof(struct evaluation));
    ev->h = h;
    ev->scopes = scopes;
    ev->f_bags = create_value_table(h);
    table_foreach(scopes, n, u){
        if (!table_find(persisted, u)) {
            table_set(ev->f_bags, u, (void *)1);
        }
    }

    // ok, now counts just accrete forever
    ev->counters =  allocate_table(h, key_from_pointer, compare_pointer);
    ev->insert = cont(h, insert_f, ev);
    ev->blocks = allocate_vector(h, 10);
    ev->persisted = persisted;
    ev->cycle_time = 0;
    ev->reader = cont(ev->h, merge_scan, ev);
    ev->complete = r;
    ev->terminal = cont(ev->h, evaluation_complete, ev);

    ev->run = cont(h, run_solver, ev);
    table_foreach(ev->persisted, uuid, b) {
        register_listener(b, ev->run);
        table_foreach(edb_implications(b), n, v){
            vector_insert(ev->blocks, build(ev, n));
        }
    }

    return ev;
}
