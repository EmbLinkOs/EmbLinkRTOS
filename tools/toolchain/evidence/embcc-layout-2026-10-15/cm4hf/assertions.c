#include <emb/storage.h>
#include <embk/kernel.h>
#include <embk/sync.h>
#include <stdalign.h>
_Static_assert(EMB_THREAD_STORAGE_SIZE == sizeof(embk_thread_t), "size");
_Static_assert(EMB_THREAD_STORAGE_ALIGN == alignof(embk_thread_t), "alignment");
_Static_assert(sizeof(emb_thread_storage_t) >= sizeof(embk_thread_t), "fits");
_Static_assert(alignof(emb_thread_storage_t) >= alignof(embk_thread_t), "aligned");
_Static_assert(EMB_SEM_STORAGE_SIZE == sizeof(embk_sem_t), "size");
_Static_assert(EMB_SEM_STORAGE_ALIGN == alignof(embk_sem_t), "alignment");
_Static_assert(sizeof(emb_sem_storage_t) >= sizeof(embk_sem_t), "fits");
_Static_assert(alignof(emb_sem_storage_t) >= alignof(embk_sem_t), "aligned");
_Static_assert(EMB_MUTEX_STORAGE_SIZE == sizeof(embk_mutex_t), "size");
_Static_assert(EMB_MUTEX_STORAGE_ALIGN == alignof(embk_mutex_t), "alignment");
_Static_assert(sizeof(emb_mutex_storage_t) >= sizeof(embk_mutex_t), "fits");
_Static_assert(alignof(emb_mutex_storage_t) >= alignof(embk_mutex_t), "aligned");
