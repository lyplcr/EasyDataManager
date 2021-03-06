/*
 * This file is part of the EasyDataManager Library.
 *
 * Copyright (C) 2013 by Armink <armink.ztl@gmail.com>
 *
 * Function: process and manage the cache data
 * Created on: 2013-10-31
 * Note: Cache > CacheData=Data > Value     in logic
 */

#include "cache.h"

#define LOG_TAG    "edm.cache"
#define assert     ELOG_ASSERT
#define log_e(...) elog_e(LOG_TAG, __VA_ARGS__)
#define log_w(...) elog_w(LOG_TAG, __VA_ARGS__)
#define log_i(...) elog_i(LOG_TAG, __VA_ARGS__)

#if EDM_DEBUG
    #define log_d(...) elog_d(LOG_TAG, __VA_ARGS__)
#else
    #define log_d(...)
#endif

static pCacheData hasData(pCache const cache, const char* name);
static CacheErrCode addData(pCache const cache, const char* name,
        uint8_t length, uint16_t* value,
        void* (*valueChangedListener)(void *arg));
static CacheErrCode delData(pCache const cache, const char* name);
static CacheErrCode getValue(pCache const cache, const char* name,
        uint16_t* value);
static CacheErrCode setValue(pCache const cache, const char* name,
        uint16_t* value);
static CacheErrCode getSize(pCache const cache, uint32_t* length,
        uint32_t* size);

/**
 * This function will initialize cache model.
 *
 * @param cache the cache pointer
 * @param name the cache name
 * @param maxThreadNum the cache use max threads number in thread pool
 * @param threadStackSize the thread stack size in thread pool
 *
 * @return error code
 */
CacheErrCode initCache(pCache const cache, const char* name, uint8_t maxThreadNum,
        uint32_t threadStackSize) {
    CacheErrCode errorCode = CACHE_NO_ERR;

    if ((name == NULL) || (strlen(name) > CACHE_NAME_MAX)) {
        log_i("the name of %s can not be create for list", name);
        errorCode = CACHE_NAME_ERROR;
    } else {
        strcpy(cache->name, name);
    }
    cache->has = hasData;
    cache->add = addData;
    cache->del = delData;
    cache->get = getValue;
    cache->set = setValue;
    cache->getSize = getSize;
    cache->dataHead = NULL;
    cache->dataTail = NULL;
    /* initialize the thread pool */
    cache->pool = (pThreadPool) malloc(sizeof(ThreadPool));
    assert(cache->pool != NULL);
    initThreadPool(cache->pool, "cache", maxThreadNum, threadStackSize);
    return errorCode;
}

/**
 * This function will find the data in CacheData list.
 *
 * @param cache the cache pointer
 * @param name the name of CacheData
 *
 * @return the CacheData point which has found,If not found will return NULL.
 */
static pCacheData hasData(pCache const cache, const char* name) {
    pCacheData data = cache->dataHead;

    assert((name != NULL) && (strlen(name) <= CACHE_NAME_MAX));
    //this cache is null
    if (cache->dataHead == NULL) {
        log_d("the %s's data list is NULL,find data fail", cache->name);
        return NULL;
    }
    /* search the data from list*/
    for (;;) {
        if (!strcmp(data->name, name)) {
            return data;
        } else {
            if (data->next == NULL) {/* list tail */
                log_d("could not find %s", name);
                break;
            }
            data = data->next;
        }
    }
    return NULL;
}

/**
 * This function will add CacheData to list.
 *
 * @param cache the cache pointer
 * @param name the name of CacheData
 * @param length the value length
 * @param value the value point
 * @param valueChangedListener the changed value's callback function
 *
 * @return error code
 */
static CacheErrCode addData(pCache const cache, const char* name, uint8_t length,
        uint16_t* value, void* (*valueChangedListener)(void *arg)) {
    CacheErrCode errorCode = CACHE_NO_ERR;
    pCacheData data;
    /* lock the thread pool synchronized lock */
    cache->pool->lock(cache->pool);

    data = (pCacheData) malloc(sizeof(CacheData));
    assert(data != NULL);

    if (hasData(cache, name) != NULL) {/* the data must not exist in list */
        log_d("the name of %s data is already exist in cache data list", name);
        errorCode = CACHE_NAME_ERROR;
    } else {
        strcpy(data->name, name);
    }

    if (errorCode == CACHE_NO_ERR) {
        if (length > CACHE_LENGTH_MAX) {
            log_d("the name %s is too long,can't add to list", name);
            errorCode = CACHE_LENGTH_ERROR;
        } else {
            data->length = length;
        }
    }

    if (errorCode == CACHE_NO_ERR) {
        /* malloc data value space */
        data->value = (uint16_t*) malloc(length * sizeof(uint16_t));
        assert(data->value != NULL);
        memcpy(data->value, value, length * sizeof(uint16_t));
        data->valueChangedListener = valueChangedListener;
        data->next = NULL;
        /* if list is NULL ,then head node is equal of tail node*/
        if (cache->dataHead == NULL) {
            cache->dataHead = cache->dataTail = data;
        } else if ((cache->dataHead == cache->dataTail) /* the list has one node */
        && (cache->dataHead != NULL)) {
            cache->dataHead->next = data;
            cache->dataTail = data;
        } else { /* the list has more than one node*/
            cache->dataTail->next = data;
            cache->dataTail = data;
        }
        log_d("add %s to data list is success", name);
    } else if (errorCode != CACHE_NO_ERR) {
        free(data);
        data = NULL;
    }
    /* unlock the thread pool synchronized lock */
    cache->pool->unlock(cache->pool);
    return errorCode;
}

/**
 * This function will delete the data from CacheData list.
 *
 * @param cache the cache pointer
 * @param name the cache data name
 *
 * @return error code
 */
static CacheErrCode delData(pCache const cache, const char* name) {
    CacheErrCode errorCode = CACHE_NO_ERR;
    pCacheData data = cache->dataHead, dataTemp;
    /* lock the thread pool synchronized lock */
    cache->pool->lock(cache->pool);

    assert((name != NULL) && (strlen(name) <= CACHE_NAME_MAX));
    /* check cache initialize */
    if (cache->dataHead == NULL) {
        log_d("the %s's data list is NULL,delete data fail", cache->name);
        errorCode = CACHE_NO_VALUE;
    }
    /* search the data from list*/
    if (errorCode == CACHE_NO_ERR) {
        if (strcmp(data->name, name)) { /* list head  */
            for (;;) {
                if (data->next == NULL) {/* list tail */
                    log_d("could not find %s", name);
                    errorCode = CACHE_NAME_ERROR;
                    break;
                } else {
                    if (!strcmp(data->next->name, name)) {
                        break;
                    } else {
                        data = data->next;
                    }
                }
            }
        }
    }
    if (errorCode == CACHE_NO_ERR) {
        /* delete data is head node */
        if (data == cache->dataHead) {
            /* the list has one node */
            if ((cache->dataHead == cache->dataTail)
                    && (cache->dataHead != NULL)) {
                cache->dataHead = cache->dataTail = NULL;
            } else { /* the list has more than one node*/
                cache->dataHead = data->next;
            }
        } else if (data->next == cache->dataTail) {/* delete data is tail node */
            cache->dataTail = data;
            cache->dataTail->next = NULL;
            data = data->next; /* data will be freed in the end */
        } else {
            dataTemp = data->next;
            data->next = data->next->next;
            data = dataTemp; /* data will be freed in the end */
        }
        free(data->value);
        data->value = NULL;
        free(data);
        data = NULL;
        dataTemp = NULL;
        log_d("delete %s data node is success", name);
    }
    /* unlock the thread pool synchronized lock */
    cache->pool->unlock(cache->pool);
    return errorCode;
}

/**
 * This function will get the value from CacheData list.
 *
 * @param cache the cache pointer
 * @param name the cache data name
 * @param value the value which has getted
 *
 * @return error code
 */
static CacheErrCode getValue(pCache const cache, const char* name, uint16_t* value) {
    CacheErrCode errorCode = CACHE_NO_ERR;
    pCacheData data;
    uint8_t i;
    uint16_t* dataValue;
    /* the data must exist in list */
    if ((data = hasData(cache, name)) == NULL) {
        errorCode = CACHE_NAME_ERROR;
    }
    /* return the value */
    if (errorCode == CACHE_NO_ERR) {
        dataValue = data->value;
        for (i = 0; i < data->length; i++) {
            log_d("get %s value%d is %d ", data->name, i, *(dataValue));
            *(value++) = *(dataValue++);
        }
    }

    return errorCode;
}

/**
 * This function will set the value in CacheData list.
 *
 * @param cache the cache pointer
 * @param name the cache data name
 * @param value the value which will set
 *
 * @return error code
 */
static CacheErrCode setValue(pCache const cache, const char* name, uint16_t* value) {
    CacheErrCode errorCode = CACHE_NO_ERR;
    pCacheData data = cache->dataHead;
    uint8_t i, isValueChanged = false;
    uint16_t* dataValue;
    /* the data must exist in list */
    if ((data = hasData(cache, name)) == NULL) {
        errorCode = CACHE_NAME_ERROR;
    } else { /* set the value */
        dataValue = data->value;
        for (i = 0; i < data->length; i++) {
            log_d("set %s value%d is %d", data->name, i, *(value));
            if (*(dataValue) != *(value)) {
                isValueChanged = true;
            }
            *(dataValue++) = *(value++);
        }
    }
    /* If value was changed.Execution valueChangedListener */
    if ((isValueChanged) && (data->valueChangedListener != NULL)) {
        cache->pool->addTask(cache->pool, data->valueChangedListener, data);
    }
    return errorCode;
}

/**
 * This function will get the CacheData list total length.
 *
 * @param cache the cache pointer
 * @param length the total length of list
 *
 * @return error code
 */
static CacheErrCode getSize(pCache const cache, uint32_t* length, uint32_t* size) {
    CacheErrCode errorCode = CACHE_NO_ERR;
    pCacheData data = cache->dataHead;
    *length = 0;
    *size = 0;
    for (;;) {
        if (data == NULL) {
            log_d("the %s's length is %d,size is %ld", cache->name, *length,
                    *size);
            break;
        } else {
            (*length)++;
            (*size) += data->length * sizeof(uint16_t);
            data = data->next;
        }
    }
    if ((*length) == 0)
        errorCode = CACHE_NOT_INIT;
    return errorCode;
}
