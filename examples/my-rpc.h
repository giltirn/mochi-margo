/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __MY_RPC
#define __MY_RPC

#include "hgargo.h"

/* visible API for example RPC operation */

MERCURY_GEN_PROC(my_rpc_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(my_rpc_in_t,
    ((int32_t)(input_val))\
    ((hg_bulk_t)(bulk_handle)))

hg_id_t my_rpc_register(void);

#endif /* __MY_RPC */
