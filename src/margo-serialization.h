/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_SERIALIZATION_H
#define __MARGO_SERIALIZATION_H

#include "margo.h"

// This file provides the serialization mechanism that Margo injects into
// Mercury in order to add a header to every RPC and response.
//
// Instead of calling HG_Register with user-provided proc callbacks, we call
// it with margo_forward_proc and margo_respond_proc, and attach the actual
// user- provided callback using HG_Register_data (see margo_rpc_data struct in
// margo-instance.h).
//
// Once it's time to call HG_Forward or HG_Respond, we extract the user-provided
// callbacks from the RPC data, create a margo_forward_proc_args or a
// margo_respond_proc_args structure, initialize it with the user-provided data
// pointer, and call HG_Forward/Respond with that argument instead.
//
// The margo_respond_proc_args structure carries an error code that allows
// margo to propagate an hg_return_t value to the client. It is used e.g.
// in the __MARGO_INTERNAL_RPC_HANDLER_BODY macro when something happened
// that prevented the RPC from running. It allows to not care about the
// semantics of the user-provided data, since any value other than HG_SUCCESS
// will make serialization stop at the error code.

typedef struct margo_forward_proc_args {
    void*        user_args;
    hg_proc_cb_t user_cb;
    struct {
        // nothing here yet
    } header;
} * margo_forward_proc_args_t;

typedef struct margo_respond_proc_args {
    void*        user_args;
    hg_proc_cb_t user_cb;
    struct {
        hg_return_t hg_ret;
    } header;
} * margo_respond_proc_args_t;

static inline hg_return_t margo_forward_proc(hg_proc_t proc, void* args)
{
    margo_forward_proc_args_t sargs = (margo_forward_proc_args_t)args;
    hg_return_t               ret   = HG_SUCCESS;
    ret = hg_proc_memcpy(proc, (void*)(&sargs->header), sizeof(sargs->header));
    if (ret != HG_SUCCESS) return ret;
    if (sargs && sargs->user_cb) {
        ret = sargs->user_cb(proc, sargs->user_args);
        return ret;
    } else
        return HG_SUCCESS;
}

static inline hg_return_t margo_respond_proc(hg_proc_t proc, void* args)
{
    margo_respond_proc_args_t sargs = (margo_respond_proc_args_t)args;
    hg_return_t               ret
        = hg_proc_memcpy(proc, (void*)(&sargs->header), sizeof(sargs->header));
    if (ret != HG_SUCCESS) return ret;
    if (sargs->header.hg_ret != HG_SUCCESS) return HG_SUCCESS;
    if (sargs && sargs->user_cb) {
        return sargs->user_cb(proc, sargs->user_args);
    } else
        return HG_SUCCESS;
}

#endif
