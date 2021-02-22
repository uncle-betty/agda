#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"

#define THREADED_RTS
#include <Rts.h>

#pragma GCC diagnostic pop

#include "trav.h"

typedef void (*evac_fn)(void *user, StgClosure **root);

// --- change me ---------------------------------------------------------------

// hard-coded addresses specific to my Agda build - use "nm" on your Agda
// executable to figure out yours

static Capability *(*const rts_lock_)(void) = (void *)0x30969b0;
static void (*const rts_unlock_)(Capability *) = (void *)0x3096a00;

static RTS_FLAGS *const RtsFlags_ = (void *)0x3718d00;
static generation **const generations_ = (void *)0x371a820;

static void (*const threadStableNameTable_)(evac_fn, void *) = (void *)0x30a2650;
static void (*const threadStablePtrTable_)(evac_fn, void *) = (void *)0x30a2bc0;

static unsigned int *const n_capabilities_ = (void *)0x371908c;
static StgTSO *const stg_END_TSO_QUEUE_closure_ = (void *)0x37161c8;

// -----------------------------------------------------------------------------

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

#define KNOWN_SZ 0x1000000
#define KNOWN_MASK (KNOWN_SZ - 1)

static void *known[KNOWN_SZ];

static bool is_known(void *p)
{
    size_t base = ((size_t)p * 0xcfd41b91) & KNOWN_MASK;

    for (size_t i = 0; i < KNOWN_SZ; ++i) {
        size_t pos = (base + i) & KNOWN_MASK;

        if (known[pos] == p) {
            return true;
        }

        if (known[pos] == NULL) {
            known[pos] = p;
            return false;
        }
    }

    fprintf(stderr, "### hash table overflow\n");
    abort();
}

static void handler(int sig, siginfo_t *info, void *ctx)
{
    (void)sig;
    (void)info;
    (void)ctx;

    int res = pthread_mutex_lock(&mutex);
    assert(res == 0);

    res = pthread_cond_signal(&cond);
    assert(res == 0);

    res = pthread_mutex_unlock(&mutex);
    assert(res == 0);
}

static void root_cb(void *ctx, StgClosure **tagged)
{
    traverseState *state = (traverseState *)ctx;
    StgClosure *clos = UNTAG_CLOSURE(*tagged);

    fprintf(stderr, "### root %p\n", clos);

    traversePushClosure(state, clos, clos);
}

static bool visit_cb(StgClosure *tagged, const StgClosure *tagged_p)
{
    StgClosure *clos = UNTAG_CLOSURE(tagged);
    StgClosure *clos_p = UNTAG_CLOSURE((StgClosure *)tagged_p);

    size_t sz = closure_sizeW(clos) * sizeof(StgWord);
    size_t sz_p = closure_sizeW(clos_p) * sizeof(StgWord);

    // StgInfoTable const *info = get_itbl((StgClosure *)clos);
    // StgInfoTable const *info_p = get_itbl((StgClosure *)clos_p);

    // DWARF symbol lookup needs the unadjusted pointer

    void *info = *(void **)clos;
    void *info_p = *(void **)clos_p;

    fprintf(stderr, "### visit %p:%zu:%p <- %p:%zu:%p\n", clos, sz, info,
            clos_p, sz_p, info_p);

    return !is_known(clos);
}

static void *run(void *ctx)
{
    (void)ctx;

    while (true) {
        fprintf(stderr, "### waiting\n");

        int res = pthread_mutex_lock(&mutex);
        assert(res == 0);

        res = pthread_cond_wait(&cond, &mutex);
        assert(res == 0);

        res = pthread_mutex_unlock(&mutex);
        assert(res == 0);

        fprintf(stderr, "### initializing\n");

        traverseState state;
        memset(&state, 0, sizeof(state));

        initializeTraverseStack(&state);

        fprintf(stderr, "### locking RTS\n");
        Capability *cap = rts_lock_();

        fprintf(stderr, "### %u OS thread(s)\n", *n_capabilities_);

        if (*n_capabilities_ > 1) {
            fprintf(stderr, "### please limit the GHC RTS to one OS thread\n");
        }
        else {
            fprintf(stderr, "### stable names\n");
            threadStableNameTable_(root_cb, &state);

            fprintf(stderr, "### stable pointers\n");
            threadStablePtrTable_(root_cb, &state);

            uint32_t n_gens = RtsFlags_->GcFlags.generations;
            fprintf(stderr, "### %u generation(s)\n", n_gens);

            for (uint32_t i = 0; i < n_gens; ++i) {
                fprintf(stderr, "### generation %u\n", i);
                StgTSO *tso = (*generations_)[i].threads;

                while (tso != stg_END_TSO_QUEUE_closure_) {
                    root_cb(&state, (StgClosure **)&tso);
                    tso = tso->global_link;
                }
            }
        }

        fprintf(stderr, "### traversing\n");
        traverseWorkStack(&state, visit_cb);

        fprintf(stderr, "### closing\n");
        closeTraverseStack(&state);

        fprintf(stderr, "### unlocking RTS\n");
        rts_unlock_(cap);
    }
}

__attribute__((constructor)) static void init(void)
{
    static const struct sigaction act = {
        .sa_sigaction = handler,
        .sa_flags = SA_SIGINFO
    };

    fprintf(stderr, "### initializing\n");

    int res = sigaction(SIGUSR1, &act, NULL);
    assert(res == 0);

    pthread_t thread;
    res = pthread_create(&thread, NULL, run, NULL);
    assert(res == 0);
}
