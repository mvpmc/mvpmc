#ifndef __REFCNT_H
#define __REFCNT_H

#define refcnt_allocate __cmyth_allocate
extern void *refcnt_allocate(size_t len);

#define refcnt_reallocate __cmyth_reallocate
extern void *refcnt_reallocate(void *p, size_t len);

#define refcnt_set_destroy __cmyth_alloc_set_destroy
extern void refcnt_set_destroy(void *block, void (*func)(void *p));

#define refcnt_strdup __cmyth_alloc_strdup
extern char *refcnt_strdup(char *str);

#define refcnt_release cmyth_release
extern void refcnt_release(void *p);

#define refcnt_hold cmyth_hold
extern void *refcnt_hold(void *p);

#endif /* REFCNT_H */
