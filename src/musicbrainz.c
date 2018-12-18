/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8 -*- */

/*-
 * Copyright (c) 2014, Johan Hattne
 *
 * Permission to use, copy, modify, and/or distribute this software
 * for any purpose with or without fee is hereby granted, provided
 * that the above copyright notice and this permission notice appear
 * in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id:$
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <unistd.h>

#include <musicbrainz5/mb5_c.h>
#include <neon/ne_string.h>

#include "musicbrainz.h"
#include "ratelimit.h"
#include "simpleq.h"

/* XXX This should be the MusicBrainz default.
 */
#define MB_LIMIT 25
#define MB_RETRIES 5
#define MB_SLEEP 5


/* A parameter, a name-value pair
 */
struct _param
{
    char *name;
    char *value;
};


/* A cache entry.  XXX Does this have to be a completed query?  No,
 * it's a bit more complicated than that.
 */
struct _query
{
    /* The strings cannot be const because it is managed by this
     * module (in particular, it is strdup():d at the beginning, and
     * free(3):d at the end).
     */
    char *entity;

    char *id;

    char *resource;

    /* Parameters
     */
    struct _param **params;

    /* Number of parameters
     */
    size_t nmemb_params;

    /* Even a successfully executed query may yield zero results.
     */
    Mb5Release *releases;

    /* The number of releases (results).
     */
    size_t nmemb_releases;

    /* XXX This is something to distinguish the status of the query
     * (e.g. if it has been executed or not).
     *
     * See _pool_request structure in pool.c
     */
    int status;

    /* Simple queue
     */
    SIMPLEQ_ENTRY(_query) queries;
};


/* Opaque structure.
 */
struct musicbrainz_ctx
{
    /* This is not really a query, but represents the connection to
     * the remote MusicBrainz server?
     */
    Mb5Query Query;
};


/* Define the structure for the head of the queries yet to be executed.
 */
SIMPLEQ_HEAD(_head, _query);

/* FIFO queue of queries to execute.
 */
struct _head _queries = SIMPLEQ_HEAD_INITIALIZER(_queries);

/* Mutex to ensure exclusive access to the result cache
 */
static pthread_mutex_t _mutex_cache = PTHREAD_MUTEX_INITIALIZER;

/* Mutex to ensure exclusive access to the queue of unexecuted
 * queries.
 */
static pthread_mutex_t _mutex_queries = PTHREAD_MUTEX_INITIALIZER;

/* The single worker thread.  This is an opaque structure which must
 * be initialized with pthread_create(3) before use.
 */
static pthread_t _thread;

/* Name of the named query semaphore
 */
static char *_name_queued = NULL;

/* Name of the named query semaphore
 */
static char *_name_processed = NULL;

/* The named query semaphore
 */
static sem_t *_sem_queued = SEM_FAILED;

/* The named results semaphore.  XXX Should use a condition variable
 * instead
 * (https://en.wikipedia.org/wiki/Monitor_(synchronization)#Condition_variables)?
 */
static sem_t *_sem_processed = SEM_FAILED;

/* Cache of completed queries
 *
 * This is not implemented as a queue because it is only appended to
 * and items are never removed.
 */
struct _query **_cache = NULL;

/* Number of completed queries in the cache.
 */
size_t _nmemb = 0;


static struct _param *
_param_new(const char *name, const char *value)
{
    struct _param *param;

    param = malloc(sizeof(struct _param));
    if (param == NULL)
        return (NULL);

    param->name = strdup(name);
    if (param->name == NULL) {
        free(param);
        return (NULL);
    }

    param->value = strdup(value);
    if (param->value == NULL) {
        free(param->name);
        free(param);
        return (NULL);
    }

    return (param);
}


static void
_param_free(struct _param *param)
{
    if (param->name != NULL)
        free(param->name);
    if (param->value != NULL)
        free(param->value);
    free(param);
}


static void
_query_free(struct _query *query)
{
    size_t i;

    if (query->entity != NULL)
        free(query->entity);

    if (query->id != NULL)
        free(query->id);

    if (query->resource != NULL)
        free(query->resource);

    if (query->params != NULL) {
        for (i = 0; i < query->nmemb_params; i++)
            _param_free(query->params[i]);
        free(query->params);
    }

    // XXX Free the releases
    
    free(query);
}


/* Adds unconditionally; should maybe check if it exists first?
 */
static struct _param *
_query_add_param(struct _query *query,
                 const char *name,
                 const char *value)
{
    struct _param *param;
    void *p;

    param = _param_new(name, value);
    if (param == NULL)
        return (NULL);

    p = realloc(
        query->params, (query->nmemb_params + 1) * sizeof(struct _param *));
    if (p == NULL) {
        _param_free(param);
        return (NULL);
    }

    query->params = p;
    query->params[query->nmemb_params++] = param;

    return (param);
}


static struct _query *
_query_new(const char *entity,
           const char *id,
           const char *resource,
           size_t nmemb_params,
           char **param_names,
           char **param_values)
{
    struct _query *query;
    size_t i;

    query = malloc(sizeof(struct _query));
    if (query == NULL)
        return (NULL);

    query->entity = NULL;
    query->id = NULL;
    query->resource = NULL;
    query->params = NULL;
    query->nmemb_params = 0;

    query->releases = NULL;
    query->nmemb_releases = 0;

    query->entity = strdup(entity);
    if (query->entity == NULL) {
        _query_free(query);
        return (NULL);
    }

    query->id = strdup(id);
    if (query->id == NULL) {
        _query_free(query);
        return (NULL);
    }

    query->resource = strdup(resource);
    if (query->resource == NULL) {
        _query_free(query);
        return (NULL);
    }

    for (i = 0; i < nmemb_params; i++) {
        if (strcmp(param_names[i], "limit") == 0)
            continue;
        if (strcmp(param_names[i], "offset") == 0)
            continue;
        if (_query_add_param(query, param_names[i], param_values[i]) == NULL) {
            _query_free(query);
            return (NULL);
        }
    }

    return (query);
}


static Mb5Release
_query_add_release(struct _query *query, Mb5Release Release)
{
    void *p;

    p = realloc(
        query->releases, (query->nmemb_releases + 1) * sizeof(Mb5Release));
    if (p == NULL)
        return (NULL);

    query->releases = p;
    query->releases[query->nmemb_releases] = Release;

    return (query->releases[query->nmemb_releases++]);
}


/* Can Metadata contain a Release and a ReleaseList at the same time?
 *
 * @return The number of resulting releases added to @p query
 */
static int
_query_add_metadata(struct _query *query, Mb5Metadata Metadata)
{
    Mb5Release Release;
    Mb5ReleaseList ReleaseList;
    int i, size;

    ReleaseList = mb5_metadata_get_releaselist(Metadata);
    if (ReleaseList != NULL) {
        printf("GOT A %d-LONG RELEASELIST at %p\n",
               mb5_release_list_size(ReleaseList), ReleaseList);

        size = mb5_release_list_size(ReleaseList);
        for (i = 0; i < size; i++) {
            Release = mb5_release_list_item(ReleaseList, i);
            if (_query_add_release(query, Release) == NULL)
                return (-1);
        }

        return (size);
    }

    Release = mb5_metadata_get_release(Metadata);
    if (Release != NULL) {
        printf("GOT A RELEASE at %p\n", Release);

        if (_query_add_release(query, Release) == NULL)
            return (-1);
        return (1);
    }

    // Output not supproted
    printf("GOT NEITHER RELEASE NOR RELEASELIST\n");
    return (-1);
}


/* Returns non-zero if @p query contains the parameter defined by @p
 * name and @p value.  The query may contain additional parameters.
 */
static struct _param *
_query_has_param(struct _query *query,
                 const char *name,
                 const char *value) 
{
    size_t i;

    for (i = 0; i < query->nmemb_params; i++) {
        if (strcmp(query->params[i]->name, name) == 0 &&
            strcmp(query->params[i]->value, value) == 0) {
            return (query->params[i]);
        }
    }
    
    return (NULL);
}


/* Note that cache entries will never have the @c limit or @c offset
 * parameters.
 */
static struct _query *
_add_to_cache(struct musicbrainz_ctx *ctx, struct _query *query)
{
    void *p;

    if (pthread_mutex_lock(&_mutex_cache) != 0)
        return (NULL);

    p = realloc(_cache, (_nmemb + 1) * sizeof(struct _query *));
    if (p == NULL) {
        pthread_mutex_unlock(&_mutex_cache);
        return (NULL);
    }

    _cache = p;
    _cache[_nmemb++] = query;
    pthread_mutex_unlock(&_mutex_cache);
    return (query);
}


/* Note that cache entries will never have the @c limit or @c offset
 * parameters.
 */
static struct _query *
_find_in_cache(struct musicbrainz_ctx *ctx,
               const char *entity,
               const char *id,
               const char *resource,
               size_t nmemb_params,
               char **param_names,
               char **param_values) 
{
    struct _query *query;
    size_t i, j;

    if (pthread_mutex_lock(&_mutex_cache) != 0)
        return (NULL);

    for (i = 0; i < _nmemb; i++) {
        query = _cache[i];

        if (strcmp(query->entity, entity) != 0)
            continue;
        
        if (strcmp(query->id, id) != 0)
            continue;

        if (strcmp(query->resource, resource) != 0)
            continue;

        for (j = 0; j < nmemb_params; j++) {
            if (strcmp(param_names[j], "limit") == 0)
                continue;
            if (strcmp(param_names[j], "offset") == 0)
                continue;
            if (_query_has_param(
                    query, param_names[j], param_values[j]) == NULL) {
                break;
            }
        }

        if (j == nmemb_params) {
            pthread_mutex_unlock(&_mutex_cache);
            return (query);
        }
    }

    pthread_mutex_unlock(&_mutex_cache);
    return (NULL);
}


/* XXX Duplication w.r.t. pool.c
 */
static int
_sem_open(char **name, sem_t **sem)
{
    char *n;
    sem_t *s;
    size_t i;
    int fd;


    n = NULL;
    s = SEM_FAILED;
    for (i = 0; i < 256; i++) {
        if (n != NULL)
            free(n);

        n = strdup("/tmp/sem_req.XXXXXX");
        if (n == NULL)
            break;

        fd = mkstemp(n);
        if (fd == -1)
            break;

        s = sem_open(n, O_CREAT | O_EXCL, 0644,0);
        if (close(fd) != 0 || unlink(n) != 0)
            break;
        if (s != SEM_FAILED) {
            *name = n;
            *sem = s;
            return (0);
        } else if (errno == EPERM) {
            errno = 0;
        }
    }

    if (i == 256)
        errno = EBUSY;

    if (s != SEM_FAILED)
        sem_close(s);
    if (n != NULL)
        free(n);
    return (-1);
}


/* See _vcatf() and _catf() in acoustid.c
 */
static size_t
_vset_value(ne_buffer *buf, const char *format, va_list ap)
{
    va_list ap_loop;
    size_t n;

    for ( ; ; ) {
        va_copy(ap_loop, ap);
        n = ne_vsnprintf(buf->data, buf->length, format, ap_loop);

        if (n + 1 < buf->length)
            break;
        ne_buffer_grow(buf, buf->length + 1);
    }

    ne_buffer_altered(buf);
    return (ne_buffer_size(buf));
}


static size_t
_set_value(ne_buffer *buf, const char *format, ...)
{
    va_list ap;
    size_t ret;

    va_start(ap, format);
    ret = _vset_value(buf, format, ap);
    va_end(ap);

    return (ret);
}


/* Note somewhere else than here: offset is zero-based!
 */
static int
_process(struct _query *query, struct musicbrainz_ctx *ctx)
{
    Mb5Metadata Metadata;
    tQueryResult result;
    ne_buffer *msg, *val_limit, *val_offset;
    char **names, **values;
    size_t i, num_offset;
    int http_code, size;


    /* Construct a temporary set of name-value pairs for the
     * parameters where the limit and offset values have been
     * appended.  The offset item is guaranteed to be the last item in
     * the arrays.
     */
    names = calloc(query->nmemb_params + 2, sizeof(char *));
    if (names == NULL)
        return (-1);
    values = calloc(query->nmemb_params + 2, sizeof(char *));
    if (values == NULL) {
        free(names);
        return (-1);
    }

    for (i = 0; i < query->nmemb_params; i++) {
        names[i] = query->params[i]->name;
        values[i] = query->params[i]->value;
    }

    msg = ne_buffer_create();
    val_limit = ne_buffer_create();
    val_offset = ne_buffer_create();
    
    _set_value(val_limit, "%zd", MB_LIMIT);
    names[query->nmemb_params + 0] = "limit";
    values[query->nmemb_params + 0] = val_limit->data;

//    _set_value(val_offset, "%zd", num_offset);
    names[query->nmemb_params + 1] = "offset";
//    values[query->nmemb_params + 1] = val_offset->data;


    /* XXX Will fail here for Sid Vicious and UB40 on the old
     * database.
     *
     * Outer loop [over i or offset] for paging, inner loop [over j]
     * for retries
     */
    num_offset = 0;
    size = 0;
    do {
        _set_value(val_offset, "%zd", num_offset += size /* size + 0 */);
        values[query->nmemb_params + 1] = val_offset->data;
//        printf("INCREASING offset by %d to %s\n", size, val_offset->data);
        
        for (i = 0; ; i++) {
            /* Ensure no more requests per unit time than are allowed.
             * Rate limiting: no more than 1 request per second
             * [http://musicbrainz.org/doc/Development/XML_Web_Service/Version_2]
             *
             * Note somewhere (maybe not here): we are issueing
             * "browse" requests.
             */
            if (ratelimit_musicbrainz() != 0) {
                ne_buffer_destroy(val_offset);
                ne_buffer_destroy(val_limit);
                ne_buffer_destroy(msg);
                free(values);
                free(names);
                return (-1);
            }

//            for (size_t j = 0; j < query->nmemb_params + 2; j++) {
//                printf("PARAMETER %s=%s\n", names[j], values[j]);
//            }

            Metadata = mb5_query_query(ctx->Query,
                                       query->entity,
                                       query->id,
                                       query->resource,
                                       query->nmemb_params + 2,
                                       names,
                                       values);
            if (Metadata != NULL)
                break;


            /* 2014-11-17:
             *
             * Result: 4 [see tQueryResult enumeration]
             * HTTPCode: 503
             * ErrorMessage: '503 Service Temporarily Unavailable'
             *
             * 2015-03-05
             *
             * Result: 4
             * HTTPCode: 504
             * ErrorMessage: '504 Gateway Time-out'
             *
             * XXX Probably won't need to print result and http_code,
             * but should propagate the error message to the caller.
             */
            ne_buffer_grow(
                msg, mb5_query_get_lasterrormessage(ctx->Query, NULL, 0) + 1);
            mb5_query_get_lasterrormessage(ctx->Query, msg->data, msg->length);
            ne_buffer_altered(msg);

            result = mb5_query_get_lastresult(ctx->Query);
            http_code = mb5_query_get_lasthttpcode(ctx->Query);

            fprintf(stderr,
                    "Result: %d\n"
                    "HTTPCode: %d\n"
                    "ErrorMessage: '%s'\n",
                    result, http_code, msg->data);


            /* XXX For what errors does it make sense to retry?  See
             * https://en.wikipedia.org/wiki/List_of_HTTP_status_codes
             */
            if (http_code / 100 != 5 || i + 1 == MB_RETRIES) {
                ne_buffer_destroy(val_offset);
                ne_buffer_destroy(val_limit);
                ne_buffer_destroy(msg);
                free(values);
                free(names);
                return (-1);
            }
            sleep(MB_SLEEP);
        }

        size = _query_add_metadata(query, Metadata);
        if (size < 0) {
            ne_buffer_destroy(val_offset);
            ne_buffer_destroy(val_limit);
            ne_buffer_destroy(msg);
            free(values);
            free(names);
            return (-1);
        }
        

        /* Adjust the value for the offset item for the next iteration
         * (next page).  The offset item is guaranteed to be the last
         * name-value pair in the arrays.
         *
         * XXX Do we ever need to page this stuff?  More than 25
         * results?  Is Dylan a candidate?  Beatles's Hard Days Night?
         *
         * XXX Looks like "Appetite for Destruction" should, but does
         * not page.
         *
         * XXX Review the paging stuff on the MusicBrainz pages,
         * particlarly the thing with the offset [add zero below, not
         * one].
         */
    } while (size >= MB_LIMIT);


    /* Add the query to cache after successful completion, post the
     * semaphore.
     */
    if (_add_to_cache(ctx, query) == NULL) {
        _query_free(query);
        return (-1);
    }

    if (sem_post(_sem_processed) != 0)
        return (-1);
    
    ne_buffer_destroy(val_offset);
    ne_buffer_destroy(val_limit);
    ne_buffer_destroy(msg);
    free(values);
    free(names);

    return (0);
}


/* See _start() in pool.c
 */
static void *
_start(void *arg)
{
    struct _query *query;
    int oldstate;

    for ( ; ; ) {
        if (sem_wait(_sem_queued) != 0) {
            if (errno == EINTR) {
                errno = 0;
                continue;
            }
            return (NULL);
        }
        
        if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate) != 0)
            return (NULL);
        if (pthread_mutex_lock(&_mutex_queries) != 0)
            return (NULL);

        if (SIMPLEQ_EMPTY(&_queries)) {
            if (pthread_mutex_unlock(&_mutex_queries) != 0)
                return (NULL);
            if (pthread_setcancelstate(oldstate, NULL) != 0)
                return (NULL);
            continue;
        }
            
        query = SIMPLEQ_FIRST(&_queries);
        if (query == NULL)
            printf("*** SIMPLEQ BOGUS #1 ***\n");
        SIMPLEQ_REMOVE_HEAD(&_queries, queries);

        if (pthread_mutex_unlock(&_mutex_queries) != 0)
            return (NULL);
        if (_process(query, (struct musicbrainz_ctx *)arg) != 0)
            return (NULL);
        if (pthread_setcancelstate(oldstate, NULL) != 0)
            return (NULL);
    }

    return (NULL);
}


struct musicbrainz_ctx *
musicbrainz_new()
{
    struct musicbrainz_ctx *ctx;

    ctx = malloc(sizeof(struct musicbrainz_ctx));
    if (ctx == NULL)
        return (NULL);

    ctx->Query = mb5_query_new("cdlookupcexample-1.0", NULL, 0);
//    ctx->Query = mb5_query_new("cdlookupcexample-1.0",
//                               "gonenlab-ws3.janelia.priv",
//                               5000);
//    ctx->Query = mb5_query_new("cdlookupcexample-1.0",
//                               "beta.musicbrainz.org",
//                               0);
    if (ctx->Query == NULL) {
        musicbrainz_free(ctx);
        return (NULL);
    }

    if (_name_queued == NULL && _sem_queued == SEM_FAILED) {
        if (_sem_open(&_name_queued, &_sem_queued) != 0) {
            musicbrainz_free(ctx);
            return (NULL);
        }
    }

    if (_name_processed == NULL && _sem_processed == SEM_FAILED) {
        if (_sem_open(&_name_processed, &_sem_processed) != 0) {
            musicbrainz_free(ctx);
            return (NULL);
        }
    }
    
    if (pthread_create(&_thread, NULL, _start, ctx) != 0) {
        musicbrainz_free(ctx);
        return (NULL);
    }

    return (ctx);
}


void
musicbrainz_free(struct musicbrainz_ctx *ctx)
{
    if (pthread_cancel(_thread) == 0)
        pthread_join(_thread, NULL);

    if (pthread_mutex_lock(&_mutex_cache) == 0) {
        // XXX Free the cache.
        pthread_mutex_unlock(&_mutex_cache);
    }
    
    if (ctx->Query != NULL)
        mb5_query_delete(ctx->Query);

    // XXX Free the semaphore stuff!

    free(ctx);
}


// XXX Always called with entity = "Release"
// XXX id == NULL <=> "browse request"?
int
musicbrainz_query(struct musicbrainz_ctx *ctx,
                  const char *entity,
                  const char *id,
                  const char *resource,
                  size_t num_params,
                  char **param_names,
                  char **param_values)
{
    struct _query *query;


    /* Check the cache for a matching entry, and return immediately if
     * a matching entry exists.  Otherwise, create a new cache entry.
     *
     * XXX Where is the query freed?
     */
    query = _find_in_cache(
        ctx, entity, id, resource, num_params, param_names, param_values);
    if (query != NULL)
        return (0);

    query = _query_new(
        entity, id, resource, num_params, param_names, param_values);
    if (query == NULL)
        return (-1);

    if (pthread_mutex_lock(&_mutex_queries) == 0) {
        SIMPLEQ_INSERT_TAIL(&_queries, query, queries);
        pthread_mutex_unlock(&_mutex_queries);
        if (sem_post(_sem_queued) != 0)
            return (-1);
    }

    return (0);
}


Mb5Release
musicbrainz_get_release(struct musicbrainz_ctx *ctx,
			const char *entity,
			const char *id,
			const char *resource,
			size_t num_params,
			char **param_names,
			char **param_values,
			const char *release_id)
{
    Mb5Release Release;
    ne_buffer *ID;

    struct _query *query;
    size_t i;


    /* If there is no entry in the cache, there was probably no
     * matching call to musicbrainz_query(); protocol error?
     *
     * We will hang indefinitely on protocol error now.  XXX Should
     * probably use sem_timedwait() but Mac OS X does not seem to
     * implement it (no man page).
     */
    query = _find_in_cache(ctx,
                           entity,
                           id,
                           resource,
                           num_params,
                           param_names,
                           param_values);
    while (query == NULL) {
        //printf("PROTOCOL ERROR\n");
        if (sem_wait(_sem_processed) != 0) {
            if (errno == EINTR) {
                errno = 0;
                continue;
            }
            return (NULL);
        }

        printf("  GOT THE SEMAPHORE\n");
        query = _find_in_cache(ctx,
                               entity,
                               id,
                               resource,
                               num_params,
                               param_names,
                               param_values);
        printf("  GOT THE QUERY: %p\n", query);
    }


    /* MusicBrainz identifiers are 36 characters, plus one character
     * for NULL-termination.
     */
    ID = ne_buffer_ncreate(36 + 1);

    for (i = 0; i < query->nmemb_releases; i++) {
        Release = query->releases[i];

        ne_buffer_grow(ID, mb5_release_get_id(Release, NULL, 0) + 1);
        mb5_release_get_id(Release, ID->data, ID->length);
        ne_buffer_altered(ID);

        printf("    ReleaseList [%zd/%zd] contains %s\n",
               i + 1, query->nmemb_releases, ID->data);

        if (strcmp(ID->data, release_id) == 0) {
            ne_buffer_destroy(ID);
            return (Release);
        }
    }

    ne_buffer_destroy(ID);

    return (NULL);
}
