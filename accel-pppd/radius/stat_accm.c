#include <string.h>

#include "radius_p.h"
#include "mempool.h"
#include "memdebug.h"

struct item_t
{
	struct list_head entry;
	unsigned int val;
	time_t ts;
};

struct stat_accm_t
{
	pthread_mutex_t lock;
	struct list_head items;
	unsigned int items_cnt;
	unsigned int time;
	unsigned long total;
};

static mempool_t item_pool;

struct stat_accm_t *stat_accm_create(unsigned int time)
{
	struct stat_accm_t *s = _malloc(sizeof(*s));

	memset(s, 0, sizeof(*s));
	pthread_mutex_init(&s->lock, NULL);
	INIT_LIST_HEAD(&s->items);
	s->time = time;

	return s;
}

static void stat_accm_clean(struct stat_accm_t *s)
{
	struct item_t *it;
	time_t ts = time(NULL);

	while (!list_empty(&s->items)) {
		it = list_entry(s->items.next, typeof(*it), entry);
		if (ts - it->ts > s->time) {
			list_del(&it->entry);
			--s->items_cnt;
			s->total -= it->val;
			mempool_free(it);
		} else
			break;
	}
}

void stat_accm_add(struct stat_accm_t *s, unsigned int val)
{
	struct item_t *it;

	pthread_mutex_lock(&s->lock);
	
	stat_accm_clean(s);

	it = mempool_alloc(item_pool);
	it->ts = time(NULL);
	it->val = val;
	list_add_tail(&it->entry, &s->items);
	++s->items_cnt;
	s->total += val;
	
	pthread_mutex_unlock(&s->lock);
}

unsigned long stat_accm_get_cnt(struct stat_accm_t *s)
{
	pthread_mutex_lock(&s->lock);
	stat_accm_clean(s);
	pthread_mutex_unlock(&s->lock);

	return s->items_cnt;
}

unsigned long stat_accm_get_avg(struct stat_accm_t *s)
{
	unsigned long val;
	pthread_mutex_lock(&s->lock);
	stat_accm_clean(s);
	val = s->items_cnt ? s->total/s->items_cnt : 0;
	pthread_mutex_unlock(&s->lock);
	
	return val;
}

static void __init init(void)
{
	item_pool = mempool_create(sizeof(struct item_t));
}
