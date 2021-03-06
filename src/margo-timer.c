/*
 * (C) 2016 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <abt.h>
#include "margo.h"
#include "margo-instance.h"
#include "margo-timer-private.h"
#include "utlist.h"

/* structure for mapping margo instance ids to corresponding timer instances */
struct margo_timer_list {
    ABT_mutex    mutex;
    margo_timer* queue_head;
};

static void __margo_timer_queue(struct margo_timer_list* timer_lst,
                                margo_timer*             timer);

struct margo_timer_list* __margo_timer_list_create()
{
    struct margo_timer_list* timer_lst;

    timer_lst = malloc(sizeof(*timer_lst));
    if (!timer_lst) return NULL;

    ABT_mutex_create(&(timer_lst->mutex));
    timer_lst->queue_head = NULL;

    return timer_lst;
}

void __margo_timer_list_free(margo_instance_id        mid,
                             struct margo_timer_list* timer_lst)
{
    margo_timer* cur;
    ABT_pool     handler_pool;
    int          ret;

    ABT_mutex_lock(timer_lst->mutex);
    /* delete any remaining timers from the queue */
    while (timer_lst->queue_head) {
        cur = timer_lst->queue_head;
        DL_DELETE(timer_lst->queue_head, cur);
        cur->prev = cur->next = NULL;

        /* we must issue the callback now for any pending timers or else the
         * callers will hang indefinitely
         */
        margo_get_handler_pool(mid, &handler_pool);
        if (handler_pool != ABT_POOL_NULL) {
            /* if handler pool is present, run callback there */
            ret = ABT_thread_create(handler_pool, cur->cb_fn, cur->cb_dat,
                                    ABT_THREAD_ATTR_NULL, NULL);
            assert(ret == ABT_SUCCESS);
        } else {
            /* else run callback in place */
            cur->cb_fn(cur->cb_dat);
        }
    }
    ABT_mutex_unlock(timer_lst->mutex);
    ABT_mutex_free(&(timer_lst->mutex));

    free(timer_lst);

    return;
}

void __margo_timer_init(margo_instance_id       mid,
                        margo_timer*            timer,
                        margo_timer_callback_fn cb_fn,
                        void*                   cb_dat,
                        double                  timeout_ms)
{
    struct margo_timer_list* timer_lst;

    timer_lst = __margo_get_timer_list(mid);
    assert(timer_lst);
    assert(timer);

    memset(timer, 0, sizeof(*timer));
    timer->mid        = mid;
    timer->cb_fn      = cb_fn;
    timer->cb_dat     = cb_dat;
    timer->expiration = ABT_get_wtime() + (timeout_ms / 1000);
    timer->prev = timer->next = NULL;

    __margo_timer_queue(timer_lst, timer);

    return;
}

void __margo_timer_destroy(margo_instance_id mid, margo_timer* timer)
{
    struct margo_timer_list* timer_lst;

    timer_lst = __margo_get_timer_list(mid);
    assert(timer_lst);
    assert(timer);

    ABT_mutex_lock(timer_lst->mutex);
    if (timer->prev || timer->next) DL_DELETE(timer_lst->queue_head, timer);
    ABT_mutex_unlock(timer_lst->mutex);

    return;
}

void __margo_check_timers(margo_instance_id mid)
{
    int                      ret;
    margo_timer*             cur;
    struct margo_timer_list* timer_lst;
    ABT_pool                 handler_pool;
    double                   now;

    timer_lst = __margo_get_timer_list(mid);
    assert(timer_lst);
    margo_get_handler_pool(mid, &handler_pool);

    ABT_mutex_lock(timer_lst->mutex);

    if (timer_lst->queue_head) now = ABT_get_wtime();

    /* iterate through timer list, performing timeout action
     * for all elements which have passed expiration time
     */
    while (timer_lst->queue_head && (timer_lst->queue_head->expiration < now)) {
        cur = timer_lst->queue_head;
        DL_DELETE(timer_lst->queue_head, cur);
        cur->prev = cur->next = NULL;

        /* schedule callback on the handler pool */
        ret = ABT_thread_create(handler_pool, cur->cb_fn, cur->cb_dat,
                                ABT_THREAD_ATTR_NULL, NULL);
        assert(ret == ABT_SUCCESS);
    }
    ABT_mutex_unlock(timer_lst->mutex);

    return;
}

/* returns 0 and sets 'next_timer_exp' if the timer instance
 * has timers queued up, -1 otherwise
 */
int __margo_timer_get_next_expiration(margo_instance_id mid,
                                      double*           next_timer_exp)
{
    struct margo_timer_list* timer_lst;
    double                   now;
    int                      ret;

    timer_lst = __margo_get_timer_list(mid);
    assert(timer_lst);

    ABT_mutex_lock(timer_lst->mutex);
    if (timer_lst->queue_head) {
        now             = ABT_get_wtime();
        *next_timer_exp = timer_lst->queue_head->expiration - now;
        ret             = 0;
    } else {
        ret = -1;
    }
    ABT_mutex_unlock(timer_lst->mutex);

    return (ret);
}

static void __margo_timer_queue(struct margo_timer_list* timer_lst,
                                margo_timer*             timer)
{
    margo_timer* cur;

    ABT_mutex_lock(timer_lst->mutex);

    /* if list of timers is empty, put ourselves on it */
    if (!(timer_lst->queue_head)) {
        DL_APPEND(timer_lst->queue_head, timer);
    } else {
        /* something else already in queue, keep it sorted in ascending order
         * of expiration time
         */
        cur = timer_lst->queue_head;
        do {
            /* walk backwards through queue */
            cur = cur->prev;
            /* as soon as we find an element that expires before this one,
             * then we add ours after it
             */
            if (cur->expiration < timer->expiration) {
                DL_APPEND_ELEM(timer_lst->queue_head, cur, timer);
                break;
            }
        } while (cur != timer_lst->queue_head);

        /* if we never found one with an expiration before this one, then
         * this one is the new head
         */
        if (timer->prev == NULL && timer->next == NULL)
            DL_PREPEND(timer_lst->queue_head, timer);
    }
    ABT_mutex_unlock(timer_lst->mutex);

    return;
}

struct margo_timer_list* __margo_get_timer_list(margo_instance_id mid)
{
    return mid->timer_list;
}

int margo_timer_create(margo_instance_id       mid,
                       margo_timer_callback_fn cb_fn,
                       void*                   cb_dat,
                       margo_timer_t*          timer)
{
    margo_timer_t tmp = (margo_timer_t)calloc(1, sizeof(*tmp));
    if (!tmp) return -1;
    tmp->mid    = mid;
    tmp->cb_fn  = cb_fn;
    tmp->cb_dat = cb_dat;
    *timer      = tmp;
    return 0;
}

int margo_timer_start(margo_timer_t timer, double timeout_ms)
{
    if (timer->prev != NULL || timer->next != NULL) return -1;

    struct margo_timer_list* timer_lst = __margo_get_timer_list(timer->mid);
    timer->expiration                  = ABT_get_wtime() + (timeout_ms / 1000);
    __margo_timer_queue(timer_lst, timer);

    return 0;
}

int margo_timer_cancel(margo_timer_t timer)
{
    struct margo_timer_list* timer_lst = __margo_get_timer_list(timer->mid);

    ABT_mutex_lock(timer_lst->mutex);
    if (timer->prev || timer->next) DL_DELETE(timer_lst->queue_head, timer);
    ABT_mutex_unlock(timer_lst->mutex);

    timer->prev = timer->next = NULL;

    return 0;
}

int margo_timer_destroy(margo_timer_t timer)
{
    margo_timer_cancel(timer);
    free(timer);
    return 0;
}
