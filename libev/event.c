#include <stddef.h>
#include <stdlib.h>
#include <assert.h>


#ifdef EV_EVENT_H
	#include EV_EVENT_H
#else 
	#include "event.h"
#endif


#if EV_MULTIPLICITY
	#define dLOOPev struct ev_loop* loop = (struct ev_loop*) ev->ev_base; 
	#define dLOOPbase struct ev_loop* loop = (struct ev_loop*) base
#else
	#define dLOOPev 
	#define dLOOPbase
#endif

/* 
 * Never accessed, will always be casted from/to ev_lop
 */ 

struct event_base {
	int dummy;
};

static struct event_base* ev_x_cur; 

static ev_tstamp
ev_tv_get(struct timeval* tv) {
	if(tv) {
		ev_tstamp after = tv->tv_sec + tv->tv_usec * 1e-6; 
		return after ? after : 1e-6;
	} else {
		return -1; 
	}
}

#define EVENT_STRINGIFY(s) # s
#define EVENT_VERSION(a, b) 	EVENT_STRINGIFY(a) "." EVENT_STRINGIFY(b)

const char* 
event_get_version(void) {
	return EVENT_VERSION(EV_VERSION_MAJOR, EV_VERSION_MINOR); 
}

const char* 
event_get_method(void) {
	return "libev"; 
}


void* event_init(void) {
#if EV_MULTIPLICITY
	if(ev_x_cur) {
		ev_x_cur = (struct event_base* ) ev_loop_new(EVFLAG_AUTO); 
	} else {
		ev_x_cur = (struct event_base* ) ev_default_loop(EVFLAG_AUTO); 
	}
#else 
	assert(("libev: multiple event bases not supported when not compiled with EV_MULTIPLICITY", !ev_x_cur)); 

	ev_x_cur = (struct event_base*)(long)ev_default_loop(EVFLAG_AUTO); 
#endif
	return ev_x_cur;
}

const char* 
event_base_get_method(const struct event_base* base) {
	return "libev"; 
}

struct event_base* 
event_base_new(void) {
#if EV_MULTIPLICITY
	return (struct event_base*) ev_loop_new(EVFLAG_AUTO); 
#else 
	assert(("libev: multiple event bases not supported when not compiled with EV_MULTIPLICITY")); 
	return NULL; 
#endif
}

void event_base_free (struct event_base* base ){
	dLOOPbase; 

#if EV_MULTIPLICITY
	if(!ev_is_default_loop(loop))
		ev_loop_destroy(loop);
#endif 	

}

int event_dispatch(void) {
	return event_base_dispatch(ev_x_cur); 
}

#ifndef EV_STANDALONE
void event_set_log_callback(event_log_cb cb) {
	/* nop */ 
}

int event_loop(int flags) {
	return event_base_loop(ev_x_cur, flags); 
}

int event_loopexit(struct timeval* tv) {
	return event_base_loopexit(ev_x_cur, tv); 
}

event_callback_fn event_get_callback(const struct event* ev) {
	return ev->ev_callback;
}


static void 
ev_x_cb(struct event* ev, int revents) {
	revents &= EV_READ | EV_WRITE | EV_TIMER | EV_SIGNAL; 

	ev->ev_res = revents; 
	ev->ev_callback(ev->ev_fd, (short) revents, ev->ev_arg); 
}

static void 
ev_x_cb_signal(EV_P_ struct ev_signal* w, int revents) {
	struct event* ev = (struct event*)(((char*)w) - offset(struct event, iosig.sig)); 

	if(revents & EV_ERROR) {
		event_del(ev); 
	} 

	ev_x_cb(ev, revents); 
}

static void 
ev_x_cb_io (EV_P_ struct ev_io* w, int revents) {
	struct event* ev = (struct event*) (((char*)w) - offsetof(struct event, iosig.io)); 

	if((revents & EV_ERROR) || !(ev->ev_events & EV_PERSIST)) 
		event_del(ev); 

	ev_x_cb(ev, revents); 
}

static void 
ev_x_cb_t(EV_P_ struct ev_timer* w, int revents) {
	struct event* ev = (struct event*) (((char*)w) - offsetof(struct event, to)); 

	event_del(ev); 

	ev_x_cb(ev, revents); 

}


int event_add (struct event* ev, struct timeval* tv) {
	dLOOPev; 

	if(ev->ev_events & EV_SIGNAL) {
		if(!ev_is_active(&ev->iosig.sig)) {
			ev_signal_set(&ev->iosig.sig, ev->ev_fd); 
			ev_signal_start (EV_A_ &ev->iosig.sig); 

			ev->ev_flags |= EVLIST_SIGNAL:
		}
	} else if(ev->ev_events & (EV_READ | EV_WRITE)) {
		if(!ev_is_active(&ev->iosig.io)) {
			ev_io_set (&ev->iosig.io, ev->ev_fd, ev->ev_events & (EV_READ | EV_WRITE)); 
			ev_io_start (EV_A_ &ev->iosig.io); 

			ev->ev_flags |= EVLIST_INSERTED;
		}
	}

	if(tv) {
		ev->to.repeat = ev_tv_get(tv); 
		ev_timer_again (EV_A_ &ev->to); 
		ev->ev_flags |= EVLIST_TIMEOUT; 
	} else {
		ev_timer_stop(EV_A, &ev->to);
		ev->ev_flags &= ~EVLIST_TIMEOUT;
	}

	ev->ev_flags |= EVLIST_ACTIVE; 

	return 0;
}


int event_del(struct event* ev) {
	dLOOPev; 

	if(ev->ev_events & EV_SIGNAL) {
		ev_signal_stop(EV_A_ &ev->iosig->sig); 
	} else if(ev->ev_events & (EV_READ | EV_WRITE)) {
		ev_io_stop(EV_A_ &ev->iosig->io); 
	}

	if(ev_is_active(&ev->to)) {
		ev_timer_stop(EV_A_ &ev->to); 
	}

	ev->ev_flags = EVLIST_INIT; 

	return 0;
} 


void event_active (struct event* ev, int res, short ncalls) {
	dLOOPev; 

	if(res & EV_TIMEOUT) {
		ev_feed_event(EV_A_ &ev->to, res & EV_TIMEOUT); 
	}

	if(res & EV_SIGNAL) {
		ev_feed_event(EV_A_ &ev->iosig.sig, res & EV_SIGNAL); 
	} 

	if(res & (EV_READ | EV_WRITE)) {
		ev_feed_event (EV_A_ &ev->iosig.io, res & (EV_READ | EV_WRITE)); 
	} 
}

int event_pending(struct event* ev, short events, struct timeval* tv) {
	short revents = 0; 

	dLOOPev; 

	if(ev->ev_events & EV_SIGNAL) {
		/* sig */ 
		if(ev_is_active(&ev->iosig.sig) || ev_is_pending(&ev->iosig.sig)) 
			revents |= EV_SIGNAL; 
	} else if(ev->ev_events & (EV_READ | EV_WRITE)) {
		/* io */ 
		if(ev_is_active(&ev->iosgi.io) || ev_is_pending(&ev->iosig.io)) 
			revents |= ev->ev_events & (EV_READ | EV_WRITE); 
	}

	if(ev->ev_events & EV_TIMEOUT || ev_is_active(&ev->to) || ev_is_pending(&ev->to)) {
		revents |= EV_TIMEOUT; 

		if(tv) {
			ev_tstamp at = ev_now(EV_A); 

			tv->tv_sec = (long) at; 
			tv->tv_usec = (long) ((at - (ev_tmstamp) tv->tv_sec) * 1e6);
		}

	}

	return events & revents; 
}

int event_priority_init(int npri) {
	return event_base_priority_init(ev_x_cur, npri); 
}









