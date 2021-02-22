/* -----------------------------------------------------------------------------
 *
 * Adapted from GHC's TraverseHeap.c.
 *
 * (c) The GHC Team, 2001,2019
 * Author: Sungwoo Park, Daniel Gröber
 *
 * ---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>

#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"

#include "Rts.h"
#include "trav.h"

// --- change me ---------------------------------------------------------------

// hard-coded addresses specific to my Agda build - use "nm" on your Agda
// executable to figure out yours

bdescr *(*const allocGroup_)(W_) = (void *)0x30a6e70;
void (*const freeChain_)(bdescr *) = (void *)0x30a7390;

// -----------------------------------------------------------------------------

StgWord const *const stg_arg_bitmaps_ = (void *)0x31fa460;

typedef enum {
    posTypeStep,
    posTypePtrs,
    posTypeSRT,
    posTypeFresh
} nextPosType;

typedef union {
    StgWord step;

    struct {
        StgHalfWord pos;
        StgHalfWord ptrs;
        StgPtr payload;
    } ptrs;

    struct {
        StgClosure *srt;
    } srt;

    StgClosure *cp;
} nextPos;

typedef struct {
    nextPosType type;
    nextPos next;
} stackPos;

typedef struct stackElement_ {
    stackPos info;
    StgClosure *c;
} stackElement;


#define BLOCKS_IN_STACK 1

STATIC_INLINE void
newStackBlock( traverseState *ts, bdescr *bd )
{
    ts->currentStack = bd;
    ts->stackTop     = (stackElement *)(bd->start + BLOCK_SIZE_W * bd->blocks);
    ts->stackBottom  = (stackElement *)bd->start;
    ts->stackLimit   = (stackElement *)ts->stackTop;
    bd->free     = (StgPtr)ts->stackLimit;
}

STATIC_INLINE void
returnToOldStack( traverseState *ts, bdescr *bd )
{
    ts->currentStack = bd;
    ts->stackTop = (stackElement *)bd->free;
    ts->stackBottom = (stackElement *)bd->start;
    ts->stackLimit = (stackElement *)(bd->start + BLOCK_SIZE_W * bd->blocks);
    bd->free = (StgPtr)ts->stackLimit;
}

void
initializeTraverseStack( traverseState *ts )
{
    if (ts->firstStack != NULL) {
        freeChain_(ts->firstStack);
    }

    ts->firstStack = allocGroup_(BLOCKS_IN_STACK);
    ts->firstStack->link = NULL;
    ts->firstStack->u.back = NULL;

    ts->stackSize = 0;
    ts->maxStackSize = 0;

    newStackBlock(ts, ts->firstStack);
}

void
closeTraverseStack( traverseState *ts )
{
    freeChain_(ts->firstStack);
    ts->firstStack = NULL;
}

STATIC_INLINE bool
isEmptyWorkStack( traverseState *ts )
{
    return (ts->firstStack == ts->currentStack) && ts->stackTop == ts->stackLimit;
}

STATIC_INLINE void
init_ptrs( stackPos *info, uint32_t ptrs, StgPtr payload )
{
    info->type              = posTypePtrs;
    info->next.ptrs.pos     = 0;
    info->next.ptrs.ptrs    = ptrs;
    info->next.ptrs.payload = payload;
}

STATIC_INLINE StgClosure *
find_ptrs( stackPos *info )
{
    if (info->next.ptrs.pos < info->next.ptrs.ptrs) {
        return (StgClosure *)info->next.ptrs.payload[info->next.ptrs.pos++];
    } else {
        return NULL;
    }
}

STATIC_INLINE void
init_srt_fun( stackPos *info, const StgFunInfoTable *infoTable )
{
    info->type = posTypeSRT;
    if (infoTable->i.srt) {
        info->next.srt.srt = (StgClosure*)GET_FUN_SRT(infoTable);
    } else {
        info->next.srt.srt = NULL;
    }
}

STATIC_INLINE void
init_srt_thunk( stackPos *info, const StgThunkInfoTable *infoTable )
{
    info->type = posTypeSRT;
    if (infoTable->i.srt) {
        info->next.srt.srt = (StgClosure*)GET_SRT(infoTable);
    } else {
        info->next.srt.srt = NULL;
    }
}

STATIC_INLINE StgClosure *
find_srt( stackPos *info )
{
    StgClosure *c;
    if (info->type == posTypeSRT) {
        c = info->next.srt.srt;
        info->next.srt.srt = NULL;
        return c;
    }

    return NULL;
}

static void
pushStackElement(traverseState *ts, const stackElement se)
{
    bdescr *nbd;
    if (ts->stackTop - 1 < ts->stackBottom) {
        ts->currentStack->free = (StgPtr)ts->stackTop;

        if (ts->currentStack->link == NULL) {
            nbd = allocGroup_(BLOCKS_IN_STACK);
            nbd->link = NULL;
            nbd->u.back = ts->currentStack;
            ts->currentStack->link = nbd;
        } else
            nbd = ts->currentStack->link;

        newStackBlock(ts, nbd);
    }

    ts->stackTop--;
    *ts->stackTop = se;

    ts->stackSize++;
    if (ts->stackSize > ts->maxStackSize) ts->maxStackSize = ts->stackSize;
    ASSERT(ts->stackSize >= 0);
}

inline void
traversePushClosure(traverseState *ts, StgClosure *c, StgClosure *cp) {
    stackElement se;

    se.c = c;
    se.info.next.cp = cp;
    se.info.type = posTypeFresh;

    pushStackElement(ts, se);
};

STATIC_INLINE void
traversePushChildren(traverseState *ts, StgClosure *c, StgClosure **first_child)
{
    stackElement se;

    ASSERT(get_itbl(c)->type != TSO);
    ASSERT(get_itbl(c)->type != AP_STACK);

    se.c = c;

    switch (get_itbl(c)->type) {
    case CONSTR_0_1:
    case CONSTR_0_2:
    case ARR_WORDS:
    case COMPACT_NFDATA:
        *first_child = NULL;
        return;

    case MUT_VAR_CLEAN:
    case MUT_VAR_DIRTY:
        *first_child = ((StgMutVar *)c)->var;
        return;
    case THUNK_SELECTOR:
        *first_child = ((StgSelector *)c)->selectee;
        return;
    case BLACKHOLE:
        *first_child = ((StgInd *)c)->indirectee;
        return;
    case CONSTR_1_0:
    case CONSTR_1_1:
        *first_child = c->payload[0];
        return;

    case CONSTR_2_0:
        *first_child = c->payload[0];
        se.info.type = posTypeStep;
        se.info.next.step = 2;
        break;

    case MVAR_CLEAN:
    case MVAR_DIRTY:
        *first_child = (StgClosure *)((StgMVar *)c)->head;
        se.info.type = posTypeStep;
        se.info.next.step = 2;
        break;

    case WEAK:
        *first_child = ((StgWeak *)c)->key;
        se.info.type = posTypeStep;
        se.info.next.step = 2;
        break;

    case TVAR:
    case CONSTR:
    case CONSTR_NOCAF:
    case PRIM:
    case MUT_PRIM:
    case BCO:
        init_ptrs(&se.info, get_itbl(c)->layout.payload.ptrs,
                  (StgPtr)c->payload);
        *first_child = find_ptrs(&se.info);
        if (*first_child == NULL)
            return;
        break;

    case MUT_ARR_PTRS_CLEAN:
    case MUT_ARR_PTRS_DIRTY:
    case MUT_ARR_PTRS_FROZEN_CLEAN:
    case MUT_ARR_PTRS_FROZEN_DIRTY:
        init_ptrs(&se.info, ((StgMutArrPtrs *)c)->ptrs,
                  (StgPtr)(((StgMutArrPtrs *)c)->payload));
        *first_child = find_ptrs(&se.info);
        if (*first_child == NULL)
            return;
        break;

    case SMALL_MUT_ARR_PTRS_CLEAN:
    case SMALL_MUT_ARR_PTRS_DIRTY:
    case SMALL_MUT_ARR_PTRS_FROZEN_CLEAN:
    case SMALL_MUT_ARR_PTRS_FROZEN_DIRTY:
        init_ptrs(&se.info, ((StgSmallMutArrPtrs *)c)->ptrs,
                  (StgPtr)(((StgSmallMutArrPtrs *)c)->payload));
        *first_child = find_ptrs(&se.info);
        if (*first_child == NULL)
            return;
        break;

    case FUN_STATIC:
    case FUN:
    case FUN_2_0:
        init_ptrs(&se.info, get_itbl(c)->layout.payload.ptrs, (StgPtr)c->payload);
        *first_child = find_ptrs(&se.info);
        if (*first_child == NULL)
            goto fun_srt_only;
        break;

    case THUNK:
    case THUNK_2_0:
        init_ptrs(&se.info, get_itbl(c)->layout.payload.ptrs,
                  (StgPtr)((StgThunk *)c)->payload);
        *first_child = find_ptrs(&se.info);
        if (*first_child == NULL)
            goto thunk_srt_only;
        break;

    case FUN_1_0:
    case FUN_1_1:
        *first_child = c->payload[0];
        ASSERT(*first_child != NULL);
        init_srt_fun(&se.info, get_fun_itbl(c));
        break;

    case THUNK_1_0:
    case THUNK_1_1:
        *first_child = ((StgThunk *)c)->payload[0];
        ASSERT(*first_child != NULL);
        init_srt_thunk(&se.info, get_thunk_itbl(c));
        break;

    case FUN_0_1:
    case FUN_0_2:
    fun_srt_only:
        init_srt_fun(&se.info, get_fun_itbl(c));
        *first_child = find_srt(&se.info);
        if (*first_child == NULL)
            return;
        break;

    case THUNK_STATIC:
        ASSERT(get_itbl(c)->srt != 0);
    case THUNK_0_1:
    case THUNK_0_2:
    thunk_srt_only:
        init_srt_thunk(&se.info, get_thunk_itbl(c));
        *first_child = find_srt(&se.info);
        if (*first_child == NULL)
            return;
        break;

    case TREC_CHUNK:
        *first_child = (StgClosure *)((StgTRecChunk *)c)->prev_chunk;
        se.info.type = posTypeStep;
        se.info.next.step = 0;
        break;

    case PAP:
    case AP:
    case AP_STACK:
    case TSO:
    case STACK:
    case IND_STATIC:

    case UPDATE_FRAME:
    case CATCH_FRAME:
    case UNDERFLOW_FRAME:
    case STOP_FRAME:
    case RET_BCO:
    case RET_SMALL:
    case RET_BIG:

    case IND:
    case INVALID_OBJECT:

    default:
        fprintf(stderr, "invalid object in traversePushChildren: %d\n",
                get_itbl(c)->type);
        abort();
    }

    ASSERT(se.info.type != posTypeFresh);

    pushStackElement(ts, se);
}

static void
popStackElement(traverseState *ts) {
    ASSERT(ts->stackTop != ts->stackLimit);
    ASSERT(!isEmptyWorkStack(ts));

    if (ts->stackTop + 1 < ts->stackLimit) {
        ts->stackTop++;

        ts->stackSize--;
        if (ts->stackSize > ts->maxStackSize) ts->maxStackSize = ts->stackSize;
        ASSERT(ts->stackSize >= 0);

        return;
    }

    bdescr *pbd;

    ASSERT(ts->stackTop + 1 == ts->stackLimit);
    ASSERT(ts->stackBottom == (stackElement *)ts->currentStack->start);

    if (ts->firstStack == ts->currentStack) {
        ts->stackTop++;
        ASSERT(ts->stackTop == ts->stackLimit);

        ts->stackSize--;
        if (ts->stackSize > ts->maxStackSize) ts->maxStackSize = ts->stackSize;
        ASSERT(ts->stackSize >= 0);

        return;
    }

    ts->currentStack->free = (StgPtr)ts->stackLimit;

    pbd = ts->currentStack->u.back;
    ASSERT(pbd != NULL);

    returnToOldStack(ts, pbd);

    ts->stackSize--;
    if (ts->stackSize > ts->maxStackSize) ts->maxStackSize = ts->stackSize;
    ASSERT(ts->stackSize >= 0);
}

STATIC_INLINE void
traversePop(traverseState *ts, StgClosure **c, StgClosure **cp)
{
    stackElement *se;

    bool last = false;

    do {
        if (isEmptyWorkStack(ts)) {
            *c = NULL;
            return;
        }

        se = ts->stackTop;

        if (se->info.type == posTypeFresh) {
            *cp = se->info.next.cp;
            *c = se->c;
            popStackElement(ts);
            return;
        }

        switch (get_itbl(se->c)->type) {
        case CONSTR_2_0:
            *c = se->c->payload[1];
            last = true;
            goto out;

        case MVAR_CLEAN:
        case MVAR_DIRTY:
            if (se->info.next.step == 2) {
                *c = (StgClosure *)((StgMVar *)se->c)->tail;
                se->info.next.step++;
            } else {
                *c = ((StgMVar *)se->c)->value;
                last = true;
            }
            goto out;

        case WEAK:
            if (se->info.next.step == 2) {
                *c = ((StgWeak *)se->c)->value;
                se->info.next.step++;
            } else {
                *c = ((StgWeak *)se->c)->finalizer;
                last = true;
            }
            goto out;

        case TREC_CHUNK: {
            TRecEntry *entry;
            uint32_t entry_no = se->info.next.step >> 2;
            uint32_t field_no = se->info.next.step & 3;
            if (entry_no == ((StgTRecChunk *)se->c)->next_entry_idx) {
                *c = NULL;
                popStackElement(ts);
                break;
            }
            entry = &((StgTRecChunk *)se->c)->entries[entry_no];
            if (field_no == 0) {
                *c = (StgClosure *)entry->tvar;
            } else if (field_no == 1) {
                *c = entry->expected_value;
            } else {
                *c = entry->new_value;
            }
            se->info.next.step++;
            goto out;
        }

        case TVAR:
        case CONSTR:
        case PRIM:
        case MUT_PRIM:
        case BCO:
        case MUT_ARR_PTRS_CLEAN:
        case MUT_ARR_PTRS_DIRTY:
        case MUT_ARR_PTRS_FROZEN_CLEAN:
        case MUT_ARR_PTRS_FROZEN_DIRTY:
        case SMALL_MUT_ARR_PTRS_CLEAN:
        case SMALL_MUT_ARR_PTRS_DIRTY:
        case SMALL_MUT_ARR_PTRS_FROZEN_CLEAN:
        case SMALL_MUT_ARR_PTRS_FROZEN_DIRTY:
            *c = find_ptrs(&se->info);
            if (*c == NULL) {
                popStackElement(ts);
                break;
            }
            goto out;

        case FUN:
        case FUN_STATIC:
        case FUN_2_0:
            if (se->info.type == posTypePtrs) {
                *c = find_ptrs(&se->info);
                if (*c != NULL) {
                    goto out;
                }
                init_srt_fun(&se->info, get_fun_itbl(se->c));
            }
            goto do_srt;

        case THUNK:
        case THUNK_2_0:
            if (se->info.type == posTypePtrs) {
                *c = find_ptrs(&se->info);
                if (*c != NULL) {
                    goto out;
                }
                init_srt_thunk(&se->info, get_thunk_itbl(se->c));
            }
            goto do_srt;

        do_srt:
        case THUNK_STATIC:
        case FUN_0_1:
        case FUN_0_2:
        case THUNK_0_1:
        case THUNK_0_2:
        case FUN_1_0:
        case FUN_1_1:
        case THUNK_1_0:
        case THUNK_1_1:
            *c = find_srt(&se->info);
            if(*c == NULL) {
                popStackElement(ts);
                break;
            }
            goto out;

        case CONSTR_0_1:
        case CONSTR_0_2:
        case ARR_WORDS:

        case MUT_VAR_CLEAN:
        case MUT_VAR_DIRTY:
        case THUNK_SELECTOR:
        case CONSTR_1_1:

        case PAP:
        case AP:
        case AP_STACK:
        case TSO:
        case STACK:
        case IND_STATIC:
        case CONSTR_NOCAF:

        case UPDATE_FRAME:
        case CATCH_FRAME:
        case UNDERFLOW_FRAME:
        case STOP_FRAME:
        case RET_BCO:
        case RET_SMALL:
        case RET_BIG:

        case IND:
        case INVALID_OBJECT:

        default:
            fprintf(stderr, "invalid object in traversePop: %d\n",
                    get_itbl(se->c)->type);
            abort();
        }
    } while (*c == NULL);

out:

    ASSERT(*c != NULL);

    *cp = se->c;

    if(last)
        popStackElement(ts);

    return;

}

static void
traverseLargeBitmap (traverseState *ts, StgPtr p, StgLargeBitmap *large_bitmap,
                     uint32_t size, StgClosure *c)
{
    uint32_t i, b;
    StgWord bitmap;

    b = 0;
    bitmap = large_bitmap->bitmap[b];
    for (i = 0; i < size; ) {
        if ((bitmap & 1) == 0) {
            traversePushClosure(ts, (StgClosure *)*p, c);
        }
        i++;
        p++;
        if (i % BITS_IN(W_) == 0) {
            b++;
            bitmap = large_bitmap->bitmap[b];
        } else {
            bitmap = bitmap >> 1;
        }
    }
}

STATIC_INLINE StgPtr
traverseSmallBitmap (traverseState *ts, StgPtr p, uint32_t size, StgWord bitmap,
                     StgClosure *c)
{
    while (size > 0) {
        if ((bitmap & 1) == 0) {
            traversePushClosure(ts, (StgClosure *)*p, c);
        }
        p++;
        bitmap = bitmap >> 1;
        size--;
    }
    return p;
}

static void
traversePushStack(traverseState *ts, StgClosure *cp, StgPtr stackStart,
        StgPtr stackEnd)
{
    StgPtr p;
    const StgRetInfoTable *info;
    StgWord bitmap;
    uint32_t size;

    ASSERT(get_itbl(cp)->type == STACK);

    p = stackStart;
    while (p < stackEnd) {
        info = get_ret_itbl((StgClosure *)p);

        switch(info->i.type) {

        case UPDATE_FRAME:
            traversePushClosure(ts, ((StgUpdateFrame *)p)->updatee, cp);
            p += sizeofW(StgUpdateFrame);
            continue;

        case UNDERFLOW_FRAME:
        case STOP_FRAME:
        case CATCH_FRAME:
        case CATCH_STM_FRAME:
        case CATCH_RETRY_FRAME:
        case ATOMICALLY_FRAME:
        case RET_SMALL:
            bitmap = BITMAP_BITS(info->i.layout.bitmap);
            size   = BITMAP_SIZE(info->i.layout.bitmap);
            p++;
            p = traverseSmallBitmap(ts, p, size, bitmap, cp);

        follow_srt:
            if (info->i.srt) {
                traversePushClosure(ts, GET_SRT(info), cp);
            }
            continue;

        case RET_BCO: {
            StgBCO *bco;

            p++;
            traversePushClosure(ts, (StgClosure*)*p, cp);
            bco = (StgBCO *)*p;
            p++;
            size = BCO_BITMAP_SIZE(bco);
            traverseLargeBitmap(ts, p, BCO_BITMAP(bco), size, cp);
            p += size;
            continue;
        }

        case RET_BIG:
            size = GET_LARGE_BITMAP(&info->i)->size;
            p++;
            traverseLargeBitmap(ts, p, GET_LARGE_BITMAP(&info->i), size, cp);
            p += size;
            goto follow_srt;

        case RET_FUN: {
            StgRetFun *ret_fun = (StgRetFun *)p;
            const StgFunInfoTable *fun_info;

            traversePushClosure(ts, ret_fun->fun, cp);
            fun_info = get_fun_itbl(UNTAG_CONST_CLOSURE(ret_fun->fun));

            p = (P_)&ret_fun->payload;
            switch (fun_info->f.fun_type) {
            case ARG_GEN:
                bitmap = BITMAP_BITS(fun_info->f.b.bitmap);
                size = BITMAP_SIZE(fun_info->f.b.bitmap);
                p = traverseSmallBitmap(ts, p, size, bitmap, cp);
                break;
            case ARG_GEN_BIG:
                size = GET_FUN_LARGE_BITMAP(fun_info)->size;
                traverseLargeBitmap(ts, p, GET_FUN_LARGE_BITMAP(fun_info),
                                    size, cp);
                p += size;
                break;
            default:
                bitmap = BITMAP_BITS(stg_arg_bitmaps_[fun_info->f.fun_type]);
                size = BITMAP_SIZE(stg_arg_bitmaps_[fun_info->f.fun_type]);
                p = traverseSmallBitmap(ts, p, size, bitmap, cp);
                break;
            }
            goto follow_srt;
        }

        default:
            fprintf(stderr, "invalid object in traversePushStack: %d",
                 (int)(info->i.type));
            abort();
        }
    }
}

STATIC_INLINE StgPtr
traversePAP (traverseState *ts,
                    StgClosure *pap,
                    StgClosure *fun,
                    StgClosure** payload, StgWord n_args)
{
    StgPtr p;
    StgWord bitmap;
    const StgFunInfoTable *fun_info;

    traversePushClosure(ts, fun, pap);
    fun = UNTAG_CLOSURE(fun);
    fun_info = get_fun_itbl(fun);
    ASSERT(fun_info->i.type != PAP);

    p = (StgPtr)payload;

    switch (fun_info->f.fun_type) {
    case ARG_GEN:
        bitmap = BITMAP_BITS(fun_info->f.b.bitmap);
        p = traverseSmallBitmap(ts, p, n_args, bitmap, pap);
        break;
    case ARG_GEN_BIG:
        traverseLargeBitmap(ts, p, GET_FUN_LARGE_BITMAP(fun_info), n_args, pap);
        p += n_args;
        break;
    case ARG_BCO:
        traverseLargeBitmap(ts, (StgPtr)payload, BCO_BITMAP(fun), n_args, pap);
        p += n_args;
        break;
    default:
        bitmap = BITMAP_BITS(stg_arg_bitmaps_[fun_info->f.fun_type]);
        p = traverseSmallBitmap(ts, p, n_args, bitmap, pap);
        break;
    }
    return p;
}

void
traverseWorkStack(traverseState *ts, visitClosure_cb visit_cb)
{
    StgClosure *c, *cp, *first_child;
    StgWord typeOfc;

loop:
    traversePop(ts, &c, &cp);

    if (c == NULL) {
        return;
    }

inner_loop:
    c = UNTAG_CLOSURE(c);

    typeOfc = get_itbl(c)->type;

    switch (typeOfc) {
    case TSO:
        if (((StgTSO *)c)->what_next == ThreadComplete ||
            ((StgTSO *)c)->what_next == ThreadKilled) {
            goto loop;
        }
        break;

    case IND_STATIC:
        c = ((StgIndStatic *)c)->indirectee;
        goto inner_loop;

    case CONSTR_NOCAF:
        goto loop;

    case THUNK_STATIC:
        if (get_itbl(c)->srt == 0) {
            goto loop;
        }
        // fall through

    case FUN_STATIC: {
        const StgInfoTable *info = get_itbl(c);
        if (info->srt == 0 && info->layout.payload.ptrs == 0) {
            goto loop;
        } else {
            break;
        }
    }

    default:
        break;
    }

    bool traverse_children = visit_cb(c, cp);
    if(!traverse_children)
        goto loop;

    switch (typeOfc) {
    case STACK:
        traversePushStack(ts, c,
                    ((StgStack *)c)->sp,
                    ((StgStack *)c)->stack + ((StgStack *)c)->stack_size);
        goto loop;

    case TSO:
    {
        StgTSO *tso = (StgTSO *)c;

        traversePushClosure(ts, (StgClosure *) tso->stackobj, c);
        traversePushClosure(ts, (StgClosure *) tso->blocked_exceptions, c);
        traversePushClosure(ts, (StgClosure *) tso->bq, c);
        traversePushClosure(ts, (StgClosure *) tso->trec, c);
        if (   tso->why_blocked == BlockedOnMVar
               || tso->why_blocked == BlockedOnMVarRead
               || tso->why_blocked == BlockedOnBlackHole
               || tso->why_blocked == BlockedOnMsgThrowTo
            ) {
            traversePushClosure(ts, tso->block_info.closure, c);
        }
        goto loop;
    }

    case BLOCKING_QUEUE:
    {
        StgBlockingQueue *bq = (StgBlockingQueue *)c;
        traversePushClosure(ts, (StgClosure *) bq->link,  c);
        traversePushClosure(ts, (StgClosure *) bq->bh,    c);
        traversePushClosure(ts, (StgClosure *) bq->owner, c);
        goto loop;
    }

    case PAP:
    {
        StgPAP *pap = (StgPAP *)c;
        traversePAP(ts, c, pap->fun, pap->payload, pap->n_args);
        goto loop;
    }

    case AP:
    {
        StgAP *ap = (StgAP *)c;
        traversePAP(ts, c, ap->fun, ap->payload, ap->n_args);
        goto loop;
    }

    case AP_STACK:
        traversePushClosure(ts, ((StgAP_STACK *)c)->fun, c);
        traversePushStack(ts, c,
                    (StgPtr)((StgAP_STACK *)c)->payload,
                    (StgPtr)((StgAP_STACK *)c)->payload +
                             ((StgAP_STACK *)c)->size);
        goto loop;
    }

    traversePushChildren(ts, c, &first_child);

    if (first_child == NULL)
        goto loop;

    cp = c;
    c = first_child;
    goto inner_loop;
}
