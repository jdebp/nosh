/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <vector>
#include <deque>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <sys/signalfd.h>
#include <fcntl.h>	// Needed for fstatat(), contrary to the manual
#include <unistd.h>
#include "kqueue_linux.h"
#include "FileDescriptorOwner.h"

// This is "good enough" for the needs of the nosh toolset.
// But it is missing several things.
//
//  * It is a single-threaded implementation, for use in single-threaded programs.
//    There is no locking of the queues map or the queue objects themselves, or reference counting.
//  * Several filters are missing.
//  * Pending returned events can potentially be returned after their conditions become false.
//  * EV_ONESHOT, EV_DISPATCH, and EV_CLEAR have no effect.
//  * EVFILT_READ does not return bytes available in data.
//  * EVFILT_WRITE does not return EV_EOF.
//  * EVFILT_VNODE does not handle character devices, block devices, or FIFOs.
//  * EVFILT_READ and EVFILT_WRITE do not handle regular files (because epoll does not).
//  * User data in filters is not supported.
//
// Differences from Linux libkqueue:
//
//  * All internal file descriptors are marked close-on-exec.
//  * EVFILT_VNODE/NOTE_WRITE on a directory actually works.
//  * Reading from the inotify doesn't overflow.

namespace {

class Watch {
public:
	Watch(int, struct stat &, uint32_t);
	int fd;
	struct stat s;
	uint32_t wanted_notes;
	char * path;		/// not owned
	int notes_for(uint32_t mask);
	static uint32_t mask_for(struct stat &, unsigned int);
};

class PollFD {
public:
	PollFD() : added_events(0), enabled_events(0) {}
	uint32_t added_events, enabled_events;
	uint32_t mask() const { return added_events & enabled_events ; }
};

class Queue {
public:
	Queue(FileDescriptorOwner &);
	~Queue() {}
	FileDescriptorOwner epoll;
	FileDescriptorOwner notify;
	FileDescriptorOwner signals;
	sigset_t added_signals, enabled_signals;

	bool legal_changes(const struct kevent *, int);
	bool apply_changes(const struct kevent *, int);
	int wait(struct kevent * pevents, int nevents, const struct timespec* timeout);
	void return_event(int & n, struct kevent * pevents, int nevents, const struct kevent & k);

	std::deque<struct kevent> pending;

	typedef std::map<int, Watch> WatchMap;
	WatchMap watches;
	typedef std::map<int, PollFD> PollFDMap;
	PollFDMap pollfds;

	std::size_t signal_off;
	union {
		signalfd_siginfo signal_info;
		char signal_buf[sizeof(signalfd_siginfo)];
	};
	std::size_t notify_off;
	union {
		inotify_event notify_event;
		char notify_buf[sizeof(inotify_event) + NAME_MAX + 1];
	};
};

typedef std::map<int, Queue *> QueueMap;
QueueMap queues;

inline
int 
get_path_from_procfs(
	int fd, 
	char * & path
) {
	if (0 > fd) return errno = EINVAL, fd;
	char procfs_path[128];
	if (0 > snprintf(procfs_path, sizeof procfs_path, "/proc/self/fd/%d", fd))
		return -1;
	struct stat s;
	if (0 > fstatat(AT_FDCWD, procfs_path, &s, AT_SYMLINK_NOFOLLOW))
		return -1;
	path = static_cast<char *>(std::malloc(s.st_size + 1));
	if (!path) return errno = ENOMEM, -1;
	const int n(readlink(procfs_path, path, s.st_size + 1));
	if (0 > n)
		return -1;
	path[n] = '\0';
	return 0;
}

}

Watch::Watch(
	int pfd,
	struct stat & ps,
	uint32_t wn
) :
	fd(pfd),
	s(ps),
	wanted_notes(wn),
	path(0)
{
}

int 
Watch::notes_for(
	uint32_t mask
) {
	int n(0);
	if (mask & IN_ATTRIB) {
		struct stat ns;
		if (0 <= fstat(fd, &ns)) {
			if (ns.st_nlink != s.st_nlink)
				n |= NOTE_LINK;
			if (ns.st_mode != s.st_mode)
				n |= NOTE_ATTRIB;
			if (ns.st_size > s.st_size)
				n |= NOTE_EXTEND;
			s = ns;
		}
	}
	if (S_ISREG(s.st_mode)) {
		if (mask & IN_MODIFY)
			n |= NOTE_WRITE;
	} else
	if (S_ISDIR(s.st_mode)) {
		if (mask & (IN_CREATE|IN_DELETE))
			n |= NOTE_WRITE;
	}
	if (mask & IN_DELETE_SELF)
		n |= NOTE_DELETE;
	if (mask & IN_MOVE_SELF)
		n |= NOTE_RENAME;
	return n & wanted_notes;
}

uint32_t 
Watch::mask_for(
	struct stat & s, 
	unsigned int notes
) {
	uint32_t m(0U);
	if (notes & (NOTE_LINK|NOTE_ATTRIB|NOTE_EXTEND))
		m |= IN_ATTRIB;
	if (S_ISREG(s.st_mode)) {
		if (notes & NOTE_WRITE)
			m |= IN_MODIFY;
	} else
	if (S_ISDIR(s.st_mode)) {
		if (notes & NOTE_WRITE)
			m |= IN_CREATE|IN_DELETE;
	}
	if (notes & NOTE_DELETE)
		m |= IN_DELETE_SELF;
	if (notes & NOTE_RENAME)
		m |= IN_MOVE_SELF;
	return m;
}

Queue::Queue(
	FileDescriptorOwner & e
) : 
	epoll(e.release()),
	notify(-1),
	signals(-1),
	added_signals(),
	enabled_signals(),
	pending(),
	watches(),
	signal_off(0),
	notify_off(0)
{
}

inline
bool 
Queue::legal_changes(
	const struct kevent * pchanges, 
	int nchanges
) {
	if (nchanges < 0) return false;

	// Several flags are mutually incompatible.
	for (int i(0); i < nchanges; ++i) {
		const struct kevent & e(pchanges[i]);
		if ((EV_DISPATCH|EV_ONESHOT) == (e.flags & (EV_DISPATCH|EV_ONESHOT)))
			return false;
		if ((EV_ADD|EV_DELETE) == (e.flags & (EV_ADD|EV_DELETE)))
			return false;
		if ((EV_ENABLE|EV_DISABLE) == (e.flags & (EV_ENABLE|EV_DISABLE)))
			return false;
		if ((EV_DELETE|EV_ENABLE) == (e.flags & (EV_DELETE|EV_ENABLE)))
			return false;
	}

	for (int i(0); i < nchanges; ++i)
		switch (pchanges[i].filter) {
			case EVFILT_READ:
			case EVFILT_WRITE:
			case EVFILT_VNODE:
#if 0 // Not implemented.  Yet.
			case EVFILT_PROC:
#endif
			case EVFILT_SIGNAL:
				break;
			default:
				return false;
		}

	return true;
}

// This routine assumes that all illegal combinations have been eliminated.
inline
bool 
Queue::apply_changes(
	const struct kevent * pchanges, 
	int nchanges
) {
	// Ensure that the singleton file descriptors are opened by any additions.
	for (int i(0); i < nchanges; ++i) {
		const struct kevent & c(pchanges[i]);
		if (!(c.flags & EV_ADD))
			continue;
		switch (c.filter) {
			case EVFILT_VNODE:
			{
				if (-1 != notify.get()) 
					break;
				notify.reset(inotify_init1(IN_CLOEXEC|IN_NONBLOCK));
				if (-1 == notify.get())
					return false;
				epoll_event e;
				e.events = EPOLLIN;
				e.data.fd = notify.get();
				if (0 > epoll_ctl(epoll.get(), EPOLL_CTL_ADD, notify.get(), &e)) {
					const int error(errno);
					notify.reset(-1);
					errno = error;
					return false;
				}
				break;
			}
#if 0 // Not implemented.  Yet.
			case EVFILT_PROC:
#endif
			case EVFILT_SIGNAL:
			{
				if (-1 != signals.get()) 
					break;
				sigemptyset(&added_signals);
				sigemptyset(&enabled_signals);
				sigset_t mask;
				sigandset(&mask, &enabled_signals, &added_signals);
				signals.reset(signalfd(-1, &mask, SFD_CLOEXEC|SFD_NONBLOCK));
				if (-1 == signals.get())
					return false;
				epoll_event e;
				e.events = EPOLLIN;
				e.data.fd = signals.get();
				if (0 > epoll_ctl(epoll.get(), EPOLL_CTL_ADD, signals.get(), &e)) {
					const int error(errno);
					signals.reset(-1);
					errno = error;
					return false;
				}
				break;
			}
		}
	}

	// Ensure that updates have already-open singleton file descriptors.
	for (int i(0); i < nchanges; ++i) {
		const struct kevent & c(pchanges[i]);
		if (c.flags & (EV_ADD|EV_DELETE))
			continue;
		switch (c.filter) {
			case EVFILT_VNODE:
				if (-1 == notify.get()) 
					return errno = EINVAL, false;
				break;
#if 0 // Not implemented.  Yet.
			case EVFILT_PROC:
#endif
			case EVFILT_SIGNAL:
				if (-1 == signals.get())
					return errno = EINVAL, false;
				break;
		}
	}

	// Perform changes.
	// The various EV_xxx flags are action triggers, not states.
	// The underlying mechanisms have three states: 
	//	* present and enabled, 
	//	* present but disabled, and 
	//	* absent.
	bool mask_changed(false);
	for (int i(0); i < nchanges; ++i) {
		const struct kevent & c(pchanges[i]);
		switch (c.filter) {
			case EVFILT_READ:
			case EVFILT_WRITE:
			{
				epoll_event e;
				e.data.fd = c.ident;
				const uint32_t mask(EVFILT_READ == c.filter ? EPOLLIN|EPOLLHUP|EPOLLRDHUP : EPOLLOUT);
				if (c.flags & EV_ADD) {
					std::pair<PollFDMap::iterator, bool> r(pollfds.insert(PollFDMap::value_type(c.ident, PollFD())));
					const PollFDMap::iterator & pi(r.first);
					pi->second.added_events |= mask;
					if (c.flags & EV_DISABLE)
						pi->second.enabled_events &= ~mask;
					else
						pi->second.enabled_events |= mask;
					e.events = pi->second.mask();
					if (r.second) {
						if (0 > epoll_ctl(epoll.get(), EPOLL_CTL_ADD, c.ident, &e))
							return false;
					} else {
						if (0 > epoll_ctl(epoll.get(), EPOLL_CTL_MOD, c.ident, &e))
							return false;
					}
				} else
				if (c.flags & EV_DELETE) {
					const PollFDMap::iterator pi(pollfds.find(c.ident));
					if (pi != pollfds.end()) {
						pi->second.added_events &= ~mask;
						if (!pi->second.added_events) {
							e.events = 0;
							if (0 > epoll_ctl(epoll.get(), EPOLL_CTL_DEL, c.ident, &e))
								return false;
							pollfds.erase(pi);
						} else {
							e.events = pi->second.mask();
							if (0 > epoll_ctl(epoll.get(), EPOLL_CTL_MOD, c.ident, &e))
								return false;
						}
					} else
						return errno = EINVAL, false;
				} else
				if (c.flags & EV_ENABLE) {
					const PollFDMap::iterator pi(pollfds.find(c.ident));
					if (pi != pollfds.end()) {
						pi->second.enabled_events |= mask;
						e.events = pi->second.mask();
						if (0 > epoll_ctl(epoll.get(), EPOLL_CTL_MOD, c.ident, &e))
							return false;
					} else
						return errno = EINVAL, false;
				} else
				if (c.flags & EV_DISABLE) {
					const PollFDMap::iterator pi(pollfds.find(c.ident));
					if (pi != pollfds.end()) {
						pi->second.enabled_events &= ~mask;
						e.events = pi->second.mask();
						if (0 > epoll_ctl(epoll.get(), EPOLL_CTL_MOD, c.ident, &e))
							return false;
					} else
						return errno = EINVAL, false;
				}
				break;
			}
			case EVFILT_VNODE:
			{
				const int fd(c.ident);
				if (c.flags & EV_ADD) {
					struct stat s;
					if (0 > fstat(fd, &s))
						return false;
					if (!S_ISDIR(s.st_mode) && !S_ISREG(s.st_mode))
						return errno = EBADF, false;
					char * path(0);
					if (0 > get_path_from_procfs(fd, path))
						return false;
					const uint32_t mask(c.flags & EV_DISABLE ? IN_OPEN : Watch::mask_for(s, c.fflags));
					const int wd(inotify_add_watch(notify.get(), path, mask));
					if (0 > wd) {
						const int error(errno);
						std::free(path);
						errno = error;
						return false;
					}
					std::pair<WatchMap::iterator, bool> r(watches.insert(WatchMap::value_type(wd, Watch(fd, s, c.fflags))));
					if (!r.second)
						r.first->second.wanted_notes = c.fflags;
					free(r.first->second.path), r.first->second.path = path;
				} else
				if (c.flags & EV_DELETE) {
					for (WatchMap::iterator j(watches.begin()); watches.end() != j; ) {
						if (j->second.fd != fd) {
							++j;
							continue;
						}
						if (0 > inotify_rm_watch(notify.get(), j->first))
							return false;
						free(j->second.path), j->second.path = 0;
						j = watches.erase(j);
					}
				} else
				if (c.flags & EV_ENABLE) {
					for (WatchMap::iterator j(watches.begin()); watches.end() != j; ++j) {
						if (j->second.fd != fd)
							continue;
						const uint32_t mask(Watch::mask_for(j->second.s, c.fflags));
						if (0 > inotify_add_watch(notify.get(), j->second.path, mask))
							return false;
					}
				} else
				if (c.flags & EV_DISABLE) {
					for (WatchMap::iterator j(watches.begin()); watches.end() != j; ++j) {
						if (j->second.fd != fd)
							continue;
						if (0 > inotify_add_watch(notify.get(), j->second.path, IN_OPEN))
							return false;
					}
				}
				break;
			}
#if 0 // Not implemented.  Yet.
			case EVFILT_PROC:
#endif
			case EVFILT_SIGNAL:
				mask_changed = true;

				if (c.flags & EV_DELETE)
					sigdelset(&added_signals, c.ident);
				else
				if (c.flags & EV_ADD)
					sigaddset(&added_signals, c.ident);

				if (c.flags & EV_DISABLE)
					sigdelset(&enabled_signals, c.ident);
				else
				if (c.flags & (EV_ADD|EV_ENABLE))
					sigaddset(&enabled_signals, c.ident);

				break;
		}
	}
	if (mask_changed) {
		if (sigisemptyset(&added_signals)) {
			epoll_event e;
			e.events = EPOLLIN;
			e.data.fd = signals.get();
			if (0 > epoll_ctl(epoll.get(), EPOLL_CTL_DEL, signals.get(), &e))
				return false;
			signals.reset(-1);
		} else {
			sigset_t mask;
			sigandset(&mask, &enabled_signals, &added_signals);
			signalfd(signals.get(), &mask, SFD_CLOEXEC|SFD_NONBLOCK);
		}
	}

	return true;
}

inline
void 
Queue::return_event(
	int & n, 
	struct kevent * pevents, 
	int nevents, 
	const struct kevent & k
) {
	if (n < nevents)
		pevents[n++] = k;
	else
		pending.push_back(k);
}

inline
int 
Queue::wait(
	struct kevent * pevents,
	int nevents,
	const struct timespec* timeout
) {
	if (nevents < 0) 
		return errno = EINVAL, -1;
	if (nevents == 0)
		return 0;

	int nreturn(0);

	if (!pending.empty()) {
		while (nreturn < nevents) {
			pevents[nreturn++] = pending.front();
			pending.pop_front();
			if (pending.empty()) break;
		}
		return nreturn;
	}

	if (timeout) {
		pollfd p[1];
		p[0].fd = epoll.get();
		p[0].events = POLLIN;
#if 0 // This code seems unnecessary for a signalfd.
		sigset_t masked_signals_during_poll;
		sigprocmask(SIG_SETMASK, 0, &masked_signals_during_poll);
		sigset_t signals_for_signalfd;
		sigandset(&signals_for_signalfd, &enabled_signals, &added_signals);
		for (int signo(1); signo < _NSIG; ++signo) {
			if (sigismember(&signals_for_signalfd, signo))
				sigdelset(&masked_signals_during_poll,signo);
		}
		const int rc(ppoll(p, sizeof p/sizeof *p, timeout, &masked_signals_during_poll));
#else
		const int rc(ppoll(p, sizeof p/sizeof *p, timeout, 0));
#endif
		if (0 >= rc) return rc;
	}

	std::vector<epoll_event> events(nevents);

	const int rc(epoll_wait(epoll.get(), events.data(), events.size(), -1));
	if (0 > rc) return rc;

	for (int i(0); i < rc; ++i) {
		const struct epoll_event & e(events[i]);
		if (signals.get() == e.data.fd) {
			if (!(e.events & EPOLLIN))
				continue;
			for (;;) {
				const int n(read(signals.get(), signal_buf + signal_off, sizeof signal_buf - signal_off));
				if (0 >= n) break;
				signal_off += n;
				while (signal_off >= sizeof signal_info) {
					struct kevent k;
					// The signal count is not available on Linux.
					EV_SET(&k, signal_info.ssi_signo, EVFILT_SIGNAL, 0, 0, 1, 0);
					return_event(nreturn, pevents, nevents, k);
					signal_off -= sizeof signal_info;
					std::memmove(signal_buf, signal_buf + sizeof signal_info, signal_off);
				}
			}
		} else
		if (notify.get() == e.data.fd) {
			if (!(e.events & EPOLLIN))
				continue;
			for (;;) {
				const int n(read(notify.get(), notify_buf + notify_off, sizeof notify_buf - notify_off));
				if (0 >= n) break;
				notify_off += n;
				while (notify_off >= sizeof notify_event && notify_off >= sizeof notify_event + notify_event.len) {
					WatchMap::iterator wi(watches.find(notify_event.wd));
					if (wi != watches.end()) {
						Watch & w(wi->second);
						if (IN_OPEN != notify_event.mask) {
							struct kevent k;
							EV_SET(&k, w.fd, EVFILT_VNODE, 0, w.notes_for(notify_event.mask), 0, 0);
							return_event(nreturn, pevents, nevents, k);
						}
					}
					notify_off -= sizeof notify_event + notify_event.len;
					std::memmove(notify_buf, notify_buf + sizeof notify_event + notify_event.len, notify_off);
				}
			}
		} else
		{
			if (e.events & EPOLLOUT) {
				struct kevent k;
				EV_SET(&k, e.data.fd, EVFILT_WRITE, 0, 0, 0, 0);
				return_event(nreturn, pevents, nevents, k);
			}
			if (e.events & (EPOLLIN|EPOLLRDHUP|EPOLLHUP)) {
				struct kevent k;
				const int n(e.events & (EPOLLHUP|EPOLLRDHUP) ? EV_EOF : 0);
				EV_SET(&k, e.data.fd, EVFILT_READ, n, 0, 0, 0);
				return_event(nreturn, pevents, nevents, k);
			}
		}
	}

	return nreturn;
}

extern "C"
int 
kqueue_linux()
{
	FileDescriptorOwner fd(epoll_create1(EPOLL_CLOEXEC));
	if (0 > fd.get()) return fd.release();

	Queue * & pq(queues[fd.get()]);
	if (pq) 
		delete pq;
	pq = new (std::nothrow) Queue(fd);
	if (!pq) 
		return -1;

	return pq->epoll.get();
}

extern "C" 
int 
kevent_linux(
	int fd,
	const struct kevent * pchanges,
	int nchanges,
	struct kevent * pevents,
	int nevents,
	const struct timespec* timeout
) {
	QueueMap::iterator i(queues.find(fd));
	if (queues.end() == i)
		return errno = EBADF, -1;
	Queue & q(*i->second);

	if (!q.legal_changes(pchanges, nchanges))
		return errno = EINVAL, -1;

	if (!q.apply_changes(pchanges, nchanges))
		return -1;

	return q.wait(pevents, nevents, timeout);
}
