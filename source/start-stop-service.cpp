/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <new>
#include <memory>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "kqueue_common.h"
#include "service-manager-client.h"
#include "popt.h"
#include "FileDescriptorOwner.h"
#include "DirStar.h"
#include "CharacterCell.h"
#include "ECMA48Output.h"
#include "TerminalCapabilities.h"

enum {
	DEFAULT = -1,
	STOP = 0,
	START = 1
};

/* Pretty coloured output ***************************************************
// **************************************************************************
*/

enum { COLOUR_DEFAULT = -1 };

static inline
void
reset_colour (
	ECMA48Output & o
) {
	o.SGRColour(true);
}

/* Utilities ****************************************************************
// **************************************************************************
*/

static bool verbose(false), pretending(false);

namespace {
struct index : public std::pair<dev_t, ino_t> {
	index(const struct stat & s) : pair(s.st_dev, s.st_ino) {}
	std::size_t hash() const { return static_cast<std::size_t>(first) + static_cast<std::size_t>(second); }
};

struct bundle;
}

typedef std::set<bundle *> bundle_pointer_set;
typedef std::list<bundle *> bundle_pointer_list;

namespace {
struct bundle {
	bundle() : 
		bundle_dir_fd(-1), 
		supervise_dir_fd(-1), 
		service_dir_fd(-1), 
		status_file_fd(-1), 
		ss_scanned(false), 
		primary_target(false),
		use_hangup(false), 
		use_final_kill(false), 
		order(-1), 
		wants(WANT_NONE), 
		job_state(INITIAL) 
	{
	}
	~bundle() { 
		if (-1 != bundle_dir_fd) { close(bundle_dir_fd); bundle_dir_fd = -1; } 
		if (-1 != supervise_dir_fd) { close(supervise_dir_fd); supervise_dir_fd = -1; } 
		if (-1 != service_dir_fd) { close(service_dir_fd); service_dir_fd = -1; } 
		if (-1 != status_file_fd) { close(status_file_fd); status_file_fd = -1; } 
	}

	enum {
		WANT_NONE = 0x0,
		WANT_START = 0x2,
		WANT_STOP = 0x4,
	};
	enum event { LOAD, LOG_LOAD, RUN_ON_EMPTY, LOG_RUN_ON_EMPTY, IS_BLOCKED, IS_BLOCKING, IS_UNBLOCKED, IS_START, IS_STOP, STOP_HARDER, STOP_HARDEST, CANNOT_START, CANNOT_STOP, IS_READY, IS_DONE };
	int bundle_dir_fd, supervise_dir_fd, service_dir_fd, status_file_fd;
	std::string path, name;
	bool ss_scanned, primary_target, use_hangup, use_final_kill;
	int order;
	unsigned wants;
	bundle_pointer_set sort_after;

	bool done() const { return job_state >= DONE; }
	bool initial() const { return job_state < BLOCKED; }
	bool blocked() const { return job_state < ACTIONED; }
	void mark_done() { job_state = DONE; }
	void mark_unblocked() { job_state = ACTIONED; }
	void mark_blocked() { job_state = BLOCKED; }
	void tick();
	bool needs_action() const { return FORCED == job_state || ORDERED == job_state || REREQUESTED == job_state || ACTIONED == job_state; }
	bool needs_initial_action() const { return ACTIONED == job_state; }
	bool needs_harder_action() const { return ORDERED == job_state || REREQUESTED == job_state; }
	bool needs_hardest_action() const { return FORCED == job_state; }
	void stop_initial() { 
		if (use_hangup)
			hangup_daemon(supervise_dir_fd); 
		stop(supervise_dir_fd); 
	}
	void stop_harder() { 
		if (use_hangup)
			hangup_daemon(supervise_dir_fd); 
		terminate_daemon(supervise_dir_fd); 
		continue_daemon(supervise_dir_fd); 
	}
	void stop_hardest() { 
		stop_harder(); 
		if (use_final_kill)
			kill_daemon(supervise_dir_fd);
	}
	void start_initial() { start(supervise_dir_fd); }
	bool has_started() const;
	bool has_stopped() const;
	void print_event(const char *, ECMA48Output &, enum event) const;
protected:
	// Our state machine guarantees that state transitions only ever increase the state value.
	// Even though we don't make use of it, our logic requires at least one state between FORCED and DONE, for timed-out jobs to sit in.
	enum { INITIAL, BLOCKED, ACTIONED, REREQUESTED = ACTIONED + 30, ORDERED = REREQUESTED + 30, FORCED = ORDERED + 30, TIMEDOUT = FORCED + 30, DONE } ;
	int job_state;
	static const char * name_of (enum event);
	static void set_colour_of (ECMA48Output &, enum event);
	static int colour_of (enum event);
};
}

inline
void
bundle::tick() 
{
	if (DONE > 1 + job_state)	// micro-optimization: only needs to calculate 1 + job_state once
		++job_state;
}

inline
bool
bundle::has_started() const
{
	if (0 > supervise_dir_fd || !is_ok(supervise_dir_fd)) return false;
	if (is_ready_after_run(service_dir_fd)) {
		const bool include_stopped(is_done_after_exit(service_dir_fd));
		return 0 < after_run_status_file(status_file_fd, include_stopped);
	} else
		return 0 < running_status_file(status_file_fd);
}

inline
bool
bundle::has_stopped() const
{
	return 0 <= supervise_dir_fd && (!is_ok(supervise_dir_fd) || 0 < stopped_status_file(status_file_fd));
}

inline
const char *
bundle::name_of (
	enum event e
) {
	switch (e) {
		case LOAD:			return "load";
		case LOG_LOAD:			return "remain";
		case RUN_ON_EMPTY:		return "log-load";
		case LOG_RUN_ON_EMPTY:		return "log-remain";
		case IS_BLOCKED:		return "blocked";
		case IS_BLOCKING:		return "blocker";
		case IS_UNBLOCKED:		return "unblocked";
		case IS_START:			return "start";
		case IS_STOP:			return "stop";
		case STOP_HARDER:		return "stop harder";
		case STOP_HARDEST:		return "stop hardest";
		case CANNOT_START:		return "cannot start";
		case CANNOT_STOP:		return "cannot stop";
		case IS_READY:			return "ready";
		case IS_DONE:			return "done";
		default:			return "unknown";
	}
}

inline
int
bundle::colour_of (
	enum event e
) {
	switch (e) {
		case LOAD:			return COLOUR_BLUE;
		case LOG_LOAD:			return COLOUR_BLUE;
		case RUN_ON_EMPTY:		return COLOUR_BLUE;
		case LOG_RUN_ON_EMPTY:		return COLOUR_BLUE;
		case IS_BLOCKED:		return COLOUR_MAGENTA;
		case IS_BLOCKING:		return COLOUR_MAGENTA;
		case IS_UNBLOCKED:		return COLOUR_CYAN;
		case IS_START:			return COLOUR_YELLOW;
		case IS_STOP:			return COLOUR_YELLOW;
		case STOP_HARDER:		return COLOUR_YELLOW;
		case STOP_HARDEST:		return COLOUR_YELLOW;
		case CANNOT_START:		return COLOUR_RED;
		case CANNOT_STOP:		return COLOUR_RED;
		case IS_READY:			return COLOUR_GREEN;
		case IS_DONE:			return COLOUR_GREEN;
		default:			return COLOUR_DEFAULT;
	}
}

inline
void
bundle::set_colour_of (
	ECMA48Output & o,
	enum bundle::event e
) {
	const int colour(colour_of(e));
	if (0 > colour)
		o.SGRColour(true);
	else
		o.SGRColour(true, Map256Colour(colour));
}

inline
void 
bundle::print_event(
	const char * prog,
	ECMA48Output & o,
	enum event e
) const {
	std::fprintf(stderr, "%s: ", prog);
	set_colour_of(o, e);
	std::fprintf(stderr, "%s", name_of(e));
	reset_colour(o);
	std::fprintf(stderr, " %s%s\n", path.c_str(), name.c_str());
}

namespace std {
template <> struct hash<struct index> {
	size_t operator() (const struct index & v) const { return v.hash(); }
};
}
typedef std::unordered_map<struct index, bundle> bundle_info_map;

static inline
unsigned
want_for (
	int service_dir_fd
) {
	const bool want(is_initially_up(service_dir_fd));
	return want ? bundle::WANT_START : bundle::WANT_STOP;
}

static inline
bundle *
add_bundle (
	bundle_info_map & bundles,
	const int bundle_dir_fd,
	const std::string & path,
	const std::string & name,
	int want
) {
	struct stat bundle_dir_s;
	if (!is_directory(bundle_dir_fd, bundle_dir_s)) {
		const int error(errno);
		close(bundle_dir_fd);
		errno = error;
		return 0;
	}
	FileDescriptorOwner service_dir_fd(open_service_dir(bundle_dir_fd));
	const unsigned wants (
		STOP == want ? static_cast<unsigned>(bundle::WANT_STOP) :
		START == want ? static_cast<unsigned>(bundle::WANT_START) :
		want_for(service_dir_fd.get())
	);
	bundle_info_map::iterator bundle_i(bundles.find(bundle_dir_s));
	if (bundle_i != bundles.end()) {
		close(bundle_dir_fd);
		bundle_i->second.wants |= wants;
		return &bundle_i->second;
	}
	bundle & b(bundles[bundle_dir_s]);
	b.name = name;
	b.path = path;
	b.bundle_dir_fd = bundle_dir_fd;
	b.service_dir_fd = service_dir_fd.release();
	b.wants = wants;
	b.use_hangup = is_use_hangup_signal(b.service_dir_fd);
	b.use_final_kill = is_use_kill_signal(b.service_dir_fd);
	return &b;
}

static inline
bundle *
add_bundle_searching_path (
	const ProcessEnvironment & envs,
	bundle_info_map & bundles,
	const char * arg,
	int want
) {
	std::string path, name, suffix;
	const int bundle_dir_fd(open_bundle_directory(envs, "", arg, path, name, suffix));
	if (0 > bundle_dir_fd) return 0;
	return add_bundle(bundles, bundle_dir_fd, path, name, want);
}

static inline
void
add_primary_target_bundles (
	const char * prog,
	const ProcessEnvironment & envs,
	bundle_info_map & bundles,
	const std::vector<const char *> & args,
	int want
) {
	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		bundle * bp(add_bundle_searching_path(envs, bundles, *i, want));
		if (!bp) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, *i, std::strerror(error));
			throw EXIT_FAILURE;
		}
		bp->primary_target = true;
	}
}

static inline
void
add_related_bundles (
	bundle_info_map & bundles,
	const bundle & b,
	const char * subdir_name,
	int want
) {
	FileDescriptorOwner subdir_fd(open_dir_at(b.bundle_dir_fd, subdir_name));
	if (0 > subdir_fd.get()) return;
	const DirStar subdir_dir(subdir_fd);
	if (!subdir_dir) return;
	for (;;) {
		const dirent * entry(readdir(subdir_dir));
		if (!entry) break;
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_DIR != entry->d_type && DT_LNK != entry->d_type) continue;
#endif
		if ('.' == entry->d_name[0]) continue;
		const int dir_fd(open_dir_at(subdir_dir.fd(), entry->d_name));
		if (0 > dir_fd) continue;
		add_bundle(bundles, dir_fd, (b.path + b.name + "/") + subdir_name, entry->d_name, want);
	}
}

static inline
bundle *
lookup_without_adding (
	bundle_info_map & bundles,
	const int bundle_dir_fd
) {
	struct stat bundle_dir_s;
	if (!is_directory(bundle_dir_fd, bundle_dir_s))
		return 0;
	bundle_info_map::iterator bundle_i(bundles.find(bundle_dir_s));
	if (bundle_i != bundles.end())
		return &bundle_i->second;
	return 0;
}

static inline
bundle_pointer_set
lookup_without_adding (
	bundle_info_map & bundles,
	const bundle & b,
	const char * subdir_name
) {
	bundle_pointer_set r;
	FileDescriptorOwner subdir_fd(open_dir_at(b.bundle_dir_fd, subdir_name));
	if (0 > subdir_fd.get()) return r;
	const DirStar subdir_dir(subdir_fd);
	if (!subdir_dir) return r;
	for (;;) {
		const dirent * entry(readdir(subdir_dir));
		if (!entry) break;
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_DIR != entry->d_type && DT_LNK != entry->d_type) continue;
#endif
		if ('.' == entry->d_name[0]) continue;
		const FileDescriptorOwner dir_fd(open_dir_at(subdir_dir.fd(), entry->d_name));
		if (0 > dir_fd.get()) continue;
		if (bundle * p = lookup_without_adding(bundles, dir_fd.get()))
			r.insert(p);
	}
	return r;
}

static
void
mark_paths_in_order (
	bundle * parent,
	bundle * b
) {
	if (0 == b->order) {
		std::fprintf(stderr, "%s: %s: %s\n", parent ? parent->name.c_str() : "", b->name.c_str(), "Ordering loop.");
		return;
	}
	if (0 > b->order) {
		b->order = 0;	// fencepost "marking in progress" value
		const bundle_pointer_set & a(b->sort_after);
		for (bundle_pointer_set::const_iterator j(a.begin()); a.end() != j; ++j) {
			bundle * p(*j);
			mark_paths_in_order(b, p);
			if (p->order > b->order) b->order = p->order;
		}
		++b->order;	// Never assigns order zero.
	}
}

static
void
insertion_sort (
	bundle_pointer_list & sorted,
	bundle * b
) {
	for (bundle_pointer_list::iterator j(sorted.begin()); sorted.end() != j; ++j) {
		if ((*j)->order > b->order) {
			sorted.insert(j, b);
			return;
		}
	}
	sorted.push_back(b);
}

static inline
void
make_symlink_target (
	int bundle_dir_fd, 
	const char * path,
	mode_t mode
) {
	struct stat s;
	if (0 > fstatat(bundle_dir_fd, path, &s, AT_SYMLINK_NOFOLLOW)) return;
	if (!S_ISLNK(s.st_mode)) return;
	std::vector<char> buf(s.st_size + 1, char());
	const int l(readlinkat(bundle_dir_fd, path, buf.data(), s.st_size));
	if (0 > l) return;
	buf[l] = '\0';
	mkdirat(bundle_dir_fd, buf.data(), mode);
}

static inline
bool
load (
	const char * prog,
	ECMA48Output & o,
	bundle & b,
	const int socket_fd,
	const int supervise_dir_fd,
	const int service_dir_fd,
	const std::string name,
	bundle::event load_event,
	bundle::event run_on_empty_event
) {
	const bool was_already_loaded(is_ok(supervise_dir_fd));
	if (was_already_loaded) return true;
	const bool run_on_empty(!is_done_after_exit(service_dir_fd));
	if (verbose) {
		b.print_event(prog, o, load_event);
		if (run_on_empty)
			b.print_event(prog, o, run_on_empty_event);
	}
	if (pretending) return true;
	make_supervise_fifos (supervise_dir_fd);
	load(prog, socket_fd, name.c_str(), supervise_dir_fd, service_dir_fd);
	if (run_on_empty)
		make_run_on_empty(prog, socket_fd, supervise_dir_fd);
	make_pipe_connectable(prog, socket_fd, supervise_dir_fd);
	return wait_ok(supervise_dir_fd, 5000);
}

/* System control subcommands ***********************************************
// **************************************************************************
*/

void
start_stop_common [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	const ProcessEnvironment & envs,
	const char * prog,
	int want,
	bool colours
) {
	TerminalCapabilities caps(envs);
	ECMA48Output o(caps, stderr, true /* C1 is 7-bit aliased */);
	if (!colours)
		caps.colour_level = caps.NO_COLOURS;

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	const FileDescriptorOwner socket_fd(connect_service_manager_socket(!per_user_mode, prog));
	if (0 > socket_fd.get()) throw EXIT_FAILURE;

	// Create the list of primary target bundles from the command-line arguments, then add in all of the bundles that they relate to.
	bundle_info_map bundles;
	add_primary_target_bundles(prog, envs, bundles, args, want);
	for (;;) {
		bool done_one(false);
		for (bundle_info_map::iterator i(bundles.begin()); bundles.end() != i; ++i) {
			if (!i->second.ss_scanned) {
				i->second.ss_scanned = true;
				switch (i->second.wants) {
					case bundle::WANT_NONE:
						break;
					case bundle::WANT_START:
						add_related_bundles(bundles, i->second, "wants/", START);
						add_related_bundles(bundles, i->second, "conflicts/", STOP);
						break;
					case bundle::WANT_STOP:
#if 0 /// TODO \todo Maybe, in the future.
						add_related_bundles(bundles, i->second, "on-stop/", START);
#endif
						add_related_bundles(bundles, i->second, "required-by/", STOP);
						break;
				}
				done_one = true;
			}
		}
		if (!done_one) break;
	}

	// Check that we aren't starting and stopping a bundle at the same time.
	bool any_conflicts(false);
	for (bundle_info_map::const_iterator i(bundles.begin()); bundles.end() != i; ++i) {
		switch (i->second.wants) {
			case bundle::WANT_NONE:
			case bundle::WANT_START:
			case bundle::WANT_STOP:
				break;
			default:
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, i->second.name.c_str(), "Conflicting job requirements.");
				any_conflicts = true;
				break;
		}
	}
	if (any_conflicts) throw EXIT_FAILURE;

	// Apply the bundle orderings to each bundle, now that we have the full set of bundles upon which we are operating (and it is self-consistent).
	// This is complicated by the fact that ordering depends from whether a predecessor/successor is being started or stopped and whether this bundle is being started or stopped.
	bundle_pointer_set unsorted;
	for (bundle_info_map::iterator i(bundles.begin()); bundles.end() != i; ++i) {
		const bundle_pointer_set a(lookup_without_adding(bundles, i->second, "after/"));
		const bundle_pointer_set b(lookup_without_adding(bundles, i->second, "before/"));
		switch (i->second.wants) {
			case bundle::WANT_START:
				for (bundle_pointer_set::const_iterator j(a.begin()); a.end() != j; ++j) {
					switch ((*j)->wants) {
						case bundle::WANT_START:
							i->second.sort_after.insert(*j);
							break;
						case bundle::WANT_STOP:
							i->second.sort_after.insert(*j);
							break;
						default:
							break;
					}
				}
				for (bundle_pointer_set::const_iterator j(b.begin()); b.end() != j; ++j) {
					switch ((*j)->wants) {
						case bundle::WANT_START:
							(*j)->sort_after.insert(&i->second);
							break;
						case bundle::WANT_STOP:
							(*j)->sort_after.insert(&i->second);
							break;
						default:
							break;
					}
				}
				break;
			case bundle::WANT_STOP:
				for (bundle_pointer_set::const_iterator j(a.begin()); a.end() != j; ++j) {
					switch ((*j)->wants) {
						case bundle::WANT_START:
							i->second.sort_after.insert(*j);
							break;
						case bundle::WANT_STOP:
							(*j)->sort_after.insert(&i->second);
							break;
						default:
							break;
					}
				}
				for (bundle_pointer_set::const_iterator j(b.begin()); b.end() != j; ++j) {
					switch ((*j)->wants) {
						case bundle::WANT_START:
							(*j)->sort_after.insert(&i->second);
							break;
						case bundle::WANT_STOP:
							i->second.sort_after.insert(*j);
							break;
						default:
							break;
					}
				}
				break;
			default:
				break;
		}
		unsorted.insert(&i->second);
	}

	// Do a topological sort on the bundles.
	// This isn't strictly necessary, as checks in the enacting loop will ensure that no bundle has action taken if a predecessor action has yet to happen.
	// But for large targets, with lots of prerequisites, this yields a consistent and fairly sensible ordering of actions in the log output, for humans.
	for (bundle_pointer_set::const_iterator i(unsorted.begin()); unsorted.end() != i; ++i)
		mark_paths_in_order(0, *i);
	bundle_pointer_list sorted;
	for (bundle_pointer_set::const_iterator i(unsorted.begin()); unsorted.end() != i; ++i)
		insertion_sort(sorted, *i);

	// Make the various "supervise" directories, if they are in a RAM volume, and open file descriptors for them.
	umask(0022);
	bool any_missing_supervise(false);
	for (bundle_pointer_list::const_iterator i(sorted.begin()); sorted.end() != i; ++i) {
		bundle & b(**i);
		if (bundle::WANT_NONE == b.wants) continue;
		make_symlink_target(b.bundle_dir_fd, "supervise", 0755);
		make_supervise (b.bundle_dir_fd);
		b.supervise_dir_fd = open_supervise_dir(b.bundle_dir_fd);
		if (0 > b.supervise_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, b.name.c_str(), "supervise", std::strerror(error));
			if (b.primary_target)
				any_missing_supervise = true;
			else
				b.wants = b.WANT_NONE;
		}
	}
	if (any_missing_supervise) throw EXIT_FAILURE;

	// Load any services (into the service manager) that are about to be started but that are not already loaded.
	// Do the same for their log services, even if those log services are not part of the calculated bundle set.
	// This is because the service manager must have the log service loaded in order to plumb the main service's output to the right place, even if the log service isn't being acted upon here.
	bool any_not_loaded(false);
	for (bundle_pointer_list::const_iterator i(sorted.begin()); sorted.end() != i; ++i) {
		bundle & b(**i);
		if (bundle::WANT_START != b.wants) continue;
		if (0 > b.supervise_dir_fd) continue;

		if (!load(prog, o, b, socket_fd.get(), b.supervise_dir_fd, b.service_dir_fd, b.name, b.LOAD, b.RUN_ON_EMPTY)) {
			std::fprintf(stderr, "%s: ERROR: %s/%s/%s: %s\n", prog, b.path.c_str(), b.name.c_str(), "ok", "Unable to load service bundle.");
			if (b.primary_target)
				any_not_loaded = true;
			else
				b.wants = b.WANT_NONE;
			continue;
		}
		const FileDescriptorOwner log_bundle_dir_fd(open_dir_at(b.bundle_dir_fd, "log/"));
		if (0 <= log_bundle_dir_fd.get()) {
			const FileDescriptorOwner log_supervise_dir_fd(open_supervise_dir(log_bundle_dir_fd.get()));
			const FileDescriptorOwner log_service_dir_fd(open_service_dir(log_bundle_dir_fd.get()));
			if (0 <= log_supervise_dir_fd.get() && 0 <= log_service_dir_fd.get()) {
				const std::string log_name(b.name + "/log");
				if (!load(prog, o, b, socket_fd.get(), log_supervise_dir_fd.get(), log_service_dir_fd.get(), log_name, b.LOG_LOAD, b.LOG_RUN_ON_EMPTY)) {
					std::fprintf(stderr, "%s: ERROR: %s/%s/%s: %s\n", prog, b.path.c_str(), log_name.c_str(), "ok", "Unable to load service bundle.");
					continue;
				}
				if (!pretending)
					plumb(prog, socket_fd.get(), b.supervise_dir_fd, log_supervise_dir_fd.get());
			}
		}
	}
	if (any_not_loaded) throw EXIT_FAILURE;

	// Open all of the status files.
	bool any_status_not_opened(false);
	for (bundle_pointer_list::const_iterator i(sorted.begin()); sorted.end() != i; ++i) {
		bundle & b(**i);
		if (0 > b.supervise_dir_fd) continue;
		b.status_file_fd = open_read_at(b.supervise_dir_fd, "status");
		if (0 > b.status_file_fd) {
			const int error(errno);
			if (bundle::WANT_START == b.wants) {
				std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, b.name.c_str(), "supervise/status", std::strerror(error));
				if (b.primary_target)
					any_status_not_opened = true;
			}
		}
	}
	if (any_status_not_opened) throw EXIT_FAILURE;

	// The main enacting loop; where we keep trying to start/stop any remaining services with pending actions until no more are left.
	timespec one_second;
	bool timed_out(true);
	one_second.tv_sec = 1;
	one_second.tv_nsec = 0;
	std::vector<struct kevent> revents(256);
	for (;;) {
		bool any_more_pending(false);
		for (bundle_pointer_list::const_iterator i(sorted.begin()); sorted.end() != i; ++i) {
			bundle & b(**i);
			
			// Check for any outward transitions that we can make.

			// Finishing the action transitions the state machine to the done state from any state.
			if (!b.done()) {
				bool is_done(true);
				switch (b.wants) {
					case bundle::WANT_START:	is_done = b.has_started(); break;
					case bundle::WANT_STOP:		is_done = b.has_stopped(); break;
				}
				if (is_done) {
					if (verbose)
						b.print_event(prog, o, bundle::WANT_START == b.wants ? b.IS_READY: b.IS_DONE);
					b.mark_done();
					struct kevent k;
					set_event(&k, b.status_file_fd, EVFILT_VNODE, EV_DELETE|EV_DISABLE, NOTE_WRITE, 0, 0);
					kevent(queue.get(), &k, 1, 0, 0, 0);
				}
			}
			if (b.done()) continue;
			any_more_pending = true;
			if (b.blocked()) {
				// All dependencies finishing causes transition from BLOCKED to ACTIONED.
				bool is_blocked(false);
				const bundle_pointer_set & a(b.sort_after);
				for (bundle_pointer_set::const_iterator j(a.begin()); a.end() != j; ++j) {
					bundle * p(*j);
					if (!p->done()) {
						if (verbose && b.initial()) {
							b.print_event(prog, o, b.IS_BLOCKED);
							p->print_event(prog, o, p->IS_BLOCKING);
						}
						is_blocked = true;
						break;
					}
				}
				if (is_blocked) {
					if (b.initial())
						b.mark_blocked();
					continue;
				}
				if (verbose)
					b.print_event(prog, o, b.IS_UNBLOCKED);
				b.mark_unblocked();
				struct kevent k;
				set_event(&k, b.status_file_fd, EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, 0);
				kevent(queue.get(), &k, 1, 0, 0, 0);
			} else {
				// Timing out transitions all states at ACTIONED and above to the next state.
				if (!timed_out) continue;
				b.tick();
			}

			// At this point, we have just transitioned into a new state.
			// Check for actions on entering the state.

			if (b.needs_action()) {
				switch (b.wants) {
					case bundle::WANT_START:
					{
						if (0 > b.supervise_dir_fd) break;
						const bool was_already_loaded(is_ok(b.supervise_dir_fd));
						if (!was_already_loaded)
							b.print_event(prog, o, b.CANNOT_START);
						else 
						if (b.needs_initial_action()) {
							if (verbose)
								b.print_event(prog, o, b.IS_START);
							if (!pretending)
								b.start_initial();
						}
						break;
					}
					case bundle::WANT_STOP:
					{
						if (0 > b.supervise_dir_fd) break;
						const bool was_already_loaded(is_ok(b.supervise_dir_fd));
						if (!was_already_loaded)
							b.print_event(prog, o, b.CANNOT_STOP);
						else
						if (b.needs_hardest_action()) {
							if (verbose)
								b.print_event(prog, o, b.STOP_HARDEST);
							if (!pretending)
								b.stop_hardest();
						} else 
						if (b.needs_harder_action()) {
							if (verbose)
								b.print_event(prog, o, b.STOP_HARDER);
							if (!pretending)
								b.stop_harder();
						} else 
						if (b.needs_initial_action()) {
							if (verbose)
								b.print_event(prog, o, b.IS_STOP);
							if (!pretending)
								b.stop_initial();
						}
						break;
					}
				}
			}
		}
		if (!any_more_pending) break;
		const int ne(kevent(queue.get(), 0, 0, revents.data(), revents.size(), &one_second));
		timed_out = 0 == ne;
	}

	throw EXIT_SUCCESS;
}

void
activate [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	bool colours(isatty(STDERR_FILENO));
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition colours_option('\0', "colour", "Force output in colour even if standard error is not a terminal.", colours);
		popt::bool_definition verbose_option('v', "verbose", "Display verbose information.", verbose);
		popt::bool_definition pretending_option('n', "pretend", "Pretend to take action, without telling the service manager to do anything.", pretending);
		popt::definition * main_table[] = {
			&user_option,
			&colours_option,
			&verbose_option,
			&pretending_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "[service(s)...]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	start_stop_common(next_prog, args, envs, prog, START, colours);
}

void
deactivate [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	bool colours(isatty(STDERR_FILENO));
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition colours_option('\0', "colour", "Force output in colour even if standard error is not a terminal.", colours);
		popt::bool_definition verbose_option('v', "verbose", "Display verbose information.", verbose);
		popt::bool_definition pretending_option('n', "pretend", "Pretend to take action, without telling the service manager to do anything.", pretending);
		popt::definition * main_table[] = {
			&user_option,
			&colours_option,
			&verbose_option,
			&pretending_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "[service(s)...]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	start_stop_common(next_prog, args, envs, prog, STOP, colours);
}

void
isolate [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	bool colours(isatty(STDERR_FILENO));
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition colours_option('\0', "colour", "Force output in colour even if standard error is not a terminal.", colours);
		popt::bool_definition verbose_option('v', "verbose", "Display verbose information.", verbose);
		popt::bool_definition pretending_option('n', "pretend", "Pretend to take action, without telling the service manager to do anything.", pretending);
		popt::definition * main_table[] = {
			&user_option,
			&colours_option,
			&verbose_option,
			&pretending_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "[service(s)...]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	start_stop_common(next_prog, args, envs, prog, START, colours);
}

void
reset [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	bool colours(isatty(STDERR_FILENO));
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition colours_option('\0', "colour", "Force output in colour even if standard error is not a terminal.", colours);
		popt::bool_definition verbose_option('v', "verbose", "Display verbose information.", verbose);
		popt::bool_definition pretending_option('n', "pretend", "Pretend to take action, without telling the service manager to do anything.", pretending);
		popt::definition * main_table[] = {
			&user_option,
			&colours_option,
			&verbose_option,
			&pretending_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "[service(s)...]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	start_stop_common(next_prog, args, envs, prog, DEFAULT, colours);
}
