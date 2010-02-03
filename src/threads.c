/*         ______   ___    ___
 *        /\  _  \ /\_ \  /\_ \
 *        \ \ \L\ \\//\ \ \//\ \      __     __   _ __   ___
 *         \ \  __ \ \ \ \  \ \ \   /'__`\ /'_ `\/\`'__\/ __`\
 *          \ \ \/\ \ \_\ \_ \_\ \_/\  __//\ \L\ \ \ \//\ \L\ \
 *           \ \_\ \_\/\____\/\____\ \____\ \____ \ \_\\ \____/
 *            \/_/\/_/\/____/\/____/\/____/\/___L\ \/_/ \/___/
 *                                           /\____/
 *                                           \_/__/
 *
 *      Public threads interface.
 *
 *      By Peter Wang.
 *
 *      See readme.txt for copyright information.
 */

/* Title: Threads
 *
 * The thread functions are documented in refman/threads.txt.
 * The NaturalDocs are deliberately bare.
 */


#include "allegro5/allegro5.h"
#include "allegro5/internal/aintern.h"
#include "allegro5/internal/aintern_memory.h"
#include "allegro5/internal/aintern_thread.h"



typedef enum THREAD_STATE {
   THREAD_STATE_CREATED,   /* -> started or -> joining */
   THREAD_STATE_STARTED,   /* -> joining */
   THREAD_STATE_JOINING,   /* -> joined */
   THREAD_STATE_JOINED,    /* -> destroyed */
   THREAD_STATE_DESTROYED,
   THREAD_STATE_DETACHED
} THREAD_STATE;


struct ALLEGRO_THREAD {
   _AL_THREAD thread;
   _AL_MUTEX mutex;
   _AL_COND cond;
   THREAD_STATE thread_state;
   void *proc;
   void *arg;
   void *retval;
};


struct ALLEGRO_MUTEX {
   _AL_MUTEX mutex;
};


struct ALLEGRO_COND {
   _AL_COND cond;
};


static void thread_func_trampoline(_AL_THREAD *inner, void *_outer)
{
   ALLEGRO_THREAD *outer = (ALLEGRO_THREAD *) _outer;
   (void)inner;

   /* Wait to start the actual user thread function.  The thread could also be
    * destroyed before ever running the user function.
    */
   _al_mutex_lock(&outer->mutex);
   while (outer->thread_state == THREAD_STATE_CREATED) {
      _al_cond_wait(&outer->cond, &outer->mutex);
   }
   _al_mutex_unlock(&outer->mutex);

   if (outer->thread_state == THREAD_STATE_STARTED) {
      outer->retval =
         ((void *(*)(ALLEGRO_THREAD *, void *))outer->proc)(outer, outer->arg);
   }
}

static void detached_thread_func_trampoline(_AL_THREAD *inner, void *_outer)
{
   ALLEGRO_THREAD *outer = (ALLEGRO_THREAD *) _outer;
   (void)inner;

   ((void *(*)(void *))outer->proc)(outer->arg);
   _AL_FREE(outer);
}


static ALLEGRO_THREAD *create_thread(void) {
   ALLEGRO_THREAD *outer;

   outer = _AL_MALLOC(sizeof(*outer));
   if (!outer) {
      return NULL;
   }
   _AL_MARK_MUTEX_UNINITED(outer->mutex); /* required */
   outer->retval = NULL;
   return outer;
}


/* Function: al_create_thread
 */
ALLEGRO_THREAD *al_create_thread(
   void *(*proc)(ALLEGRO_THREAD *thread, void *arg), void *arg)
{
   ALLEGRO_THREAD *outer = create_thread();
   outer->thread_state = THREAD_STATE_CREATED;
   _al_mutex_init(&outer->mutex);
   _al_cond_init(&outer->cond);
   outer->arg = arg;
   outer->proc = proc;
   _al_thread_create(&outer->thread, thread_func_trampoline, outer);
   /* XXX _al_thread_create should return an error code */
   return outer;
}



/* Function: al_run_detached_thread
 */
void al_run_detached_thread(void *(*proc)(void *arg), void *arg)
{
   ALLEGRO_THREAD *outer = create_thread();
   outer->thread_state = THREAD_STATE_DETACHED;
   outer->arg = arg;
   outer->proc = proc;
   _al_thread_create(&outer->thread, detached_thread_func_trampoline, outer);
   _al_thread_detach(&outer->thread);
}


/* Function: al_start_thread
 */
void al_start_thread(ALLEGRO_THREAD *outer)
{
   ASSERT(outer);

   switch (outer->thread_state) {
      case THREAD_STATE_CREATED:
         _al_mutex_lock(&outer->mutex);
         outer->thread_state = THREAD_STATE_STARTED;
         _al_cond_broadcast(&outer->cond);
         _al_mutex_unlock(&outer->mutex);
         break;
      case THREAD_STATE_STARTED:
         break;
      /* invalid cases */
      case THREAD_STATE_JOINING:
         ASSERT(outer->thread_state != THREAD_STATE_JOINING);
         break;
      case THREAD_STATE_JOINED:
         ASSERT(outer->thread_state != THREAD_STATE_JOINED);
         break;
      case THREAD_STATE_DESTROYED:
         ASSERT(outer->thread_state != THREAD_STATE_DESTROYED);
         break;
      case THREAD_STATE_DETACHED:
         ASSERT(outer->thread_state != THREAD_STATE_DETACHED);
         break;
   }
}


/* Function: al_join_thread
 */
void al_join_thread(ALLEGRO_THREAD *outer, void **ret_value)
{
   ASSERT(outer);

   switch (outer->thread_state) {
      case THREAD_STATE_CREATED: /* fall through */
      case THREAD_STATE_STARTED:
         _al_mutex_lock(&outer->mutex);
         outer->thread_state = THREAD_STATE_JOINING;
         _al_cond_broadcast(&outer->cond);
         _al_mutex_unlock(&outer->mutex);
         _al_mutex_destroy(&outer->mutex);
         _al_thread_join(&outer->thread);
         outer->thread_state = THREAD_STATE_JOINED;
         break;
      case THREAD_STATE_JOINING:
         ASSERT(outer->thread_state != THREAD_STATE_JOINING);
         break;
      case THREAD_STATE_JOINED:
         ASSERT(outer->thread_state != THREAD_STATE_JOINED);
         break;
      case THREAD_STATE_DESTROYED:
         ASSERT(outer->thread_state != THREAD_STATE_DESTROYED);
         break;
      case THREAD_STATE_DETACHED:
         ASSERT(outer->thread_state != THREAD_STATE_DETACHED);
         break;
   }

   if (ret_value) {
      *ret_value = outer->retval;
   }
}


/* Function: al_set_thread_should_stop
 */
void al_set_thread_should_stop(ALLEGRO_THREAD *outer)
{
   ASSERT(outer);
   _al_thread_set_should_stop(&outer->thread);
}


/* Function: al_get_thread_should_stop
 */
bool al_get_thread_should_stop(ALLEGRO_THREAD *outer)
{
   ASSERT(outer);
   return _al_get_thread_should_stop(&outer->thread);
}


/* Function: al_destroy_thread
 */
void al_destroy_thread(ALLEGRO_THREAD *outer)
{
   if (!outer) {
      return;
   }

   /* Join if required. */
   switch (outer->thread_state) {
      case THREAD_STATE_CREATED: /* fall through */
      case THREAD_STATE_STARTED:
         al_join_thread(outer, NULL);
         break;
      case THREAD_STATE_JOINING:
         ASSERT(outer->thread_state != THREAD_STATE_JOINING);
         break;
      case THREAD_STATE_JOINED:
         break;
      case THREAD_STATE_DESTROYED:
         ASSERT(outer->thread_state != THREAD_STATE_DESTROYED);
         break;
      case THREAD_STATE_DETACHED:
         ASSERT(outer->thread_state != THREAD_STATE_DETACHED);
         break;
   }

   /* May help debugging. */
   outer->thread_state = THREAD_STATE_DESTROYED;
   _AL_FREE(outer);
}


/* Function: al_create_mutex
 */
ALLEGRO_MUTEX *al_create_mutex(void)
{
   ALLEGRO_MUTEX *mutex = _AL_MALLOC(sizeof(*mutex));
   if (mutex) {
      _AL_MARK_MUTEX_UNINITED(mutex->mutex);
      _al_mutex_init(&mutex->mutex);
   }
   return mutex;
}


/* Function: al_create_mutex_recursive
 */
ALLEGRO_MUTEX *al_create_mutex_recursive(void)
{
   ALLEGRO_MUTEX *mutex = _AL_MALLOC(sizeof(*mutex));
   if (mutex) {
      _al_mutex_init_recursive(&mutex->mutex);
   }
   return mutex;
}


/* Function: al_lock_mutex
 */
void al_lock_mutex(ALLEGRO_MUTEX *mutex)
{
   ASSERT(mutex);
   _al_mutex_lock(&mutex->mutex);
}



/* Function: al_unlock_mutex
 */
void al_unlock_mutex(ALLEGRO_MUTEX *mutex)
{
   ASSERT(mutex);
   _al_mutex_unlock(&mutex->mutex);
}


/* Function: al_destroy_mutex
 */
void al_destroy_mutex(ALLEGRO_MUTEX *mutex)
{
   if (mutex) {
      _al_mutex_destroy(&mutex->mutex);
      _AL_FREE(mutex);
   }
}


/* Function: al_create_cond
 */
ALLEGRO_COND *al_create_cond(void)
{
   ALLEGRO_COND *cond = _AL_MALLOC(sizeof(*cond));
   if (cond) {
      _al_cond_init(&cond->cond);
   }
   return cond;
}


/* Function: al_destroy_cond
 */
void al_destroy_cond(ALLEGRO_COND *cond)
{
   if (cond) {
      _al_cond_destroy(&cond->cond);
      _AL_FREE(cond);
   }
}


/* Function: al_wait_cond
 */
void al_wait_cond(ALLEGRO_COND *cond, ALLEGRO_MUTEX *mutex)
{
   ASSERT(cond);
   ASSERT(mutex);

   _al_cond_wait(&cond->cond, &mutex->mutex);
}


/* Function: al_wait_cond_timed
 */
int al_wait_cond_timed(ALLEGRO_COND *cond, ALLEGRO_MUTEX *mutex,
   const ALLEGRO_TIMEOUT *timeout)
{
   ASSERT(cond);
   ASSERT(mutex);
   ASSERT(timeout);

   return _al_cond_timedwait(&cond->cond, &mutex->mutex, timeout);
}


/* Function: al_broadcast_cond
 */
void al_broadcast_cond(ALLEGRO_COND *cond)
{
   ASSERT(cond);

   _al_cond_broadcast(&cond->cond);
}


/* Function: al_signal_cond
 */
void al_signal_cond(ALLEGRO_COND *cond)
{
   ASSERT(cond);

   _al_cond_signal(&cond->cond);
}


/* vim: set sts=3 sw=3 et: */