/* -----------------------------------------------------------------------------
 *
 * Adapted from GHC's TraverseHeap.h.
 *
 * (c) The GHC Team, 2019
 * Author: Daniel Gröber
 *
 * ---------------------------------------------------------------------------*/

#pragma once

#include <rts/Types.h>

typedef struct stackElement_ stackElement;

typedef struct traverseState_ {
    bdescr *firstStack;
    bdescr *currentStack;
    stackElement *stackBottom, *stackTop, *stackLimit;
    int stackSize, maxStackSize;
} traverseState;

typedef bool (*visitClosure_cb) (StgClosure *c, const StgClosure *cp);

void initializeTraverseStack(traverseState *ts);
void closeTraverseStack(traverseState *ts);

void traversePushClosure(traverseState *ts, StgClosure *c, StgClosure *cp);
void traverseWorkStack(traverseState *ts, visitClosure_cb visit_cb);
