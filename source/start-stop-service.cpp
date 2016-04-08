/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <map>
#include <set>
#include <cstddef>
#include <cstdlib>
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
#include "service-manager-client.h"
#include "popt.h"
#include "FileDescriptorOwner.h"
#include "DirStar.h"

enum {
	DEFAULT = -1,
	STOP = 0,
	START = 1
};

/* Utilities ****************************************************************
// **************************************************************************
*/

static bool verbose(false), pretending(false);

struct index : public std::pair<dev_t, ino_t> {
	index(const struct stat & s) : pair(s.st_dev, s.st_ino) {}
};

namespace {
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
		ss_scanned(false), 
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
	}

	enum {
		WANT_NONE = 0x0,
		WANT_START = 0x2,
		WANT_STOP = 0x4,
	};
	int bundle_dir_fd, supervise_dir_fd, service_dir_fd;
	std::string path, name;
	bool ss_scanned, use_hangup, use_final_kill;
	int order;
	unsigned wants;
	bundle_pointer_set sort_after;

	bool done() const { return job_state >= DONE; }
	bool blocked() const { return job_state < ACTIONED; }
	void mark_done() { job_state = DONE; }
	void mark_unblocked() { job_state = ACTIONED; }
	void mark_blocked() { job_state = BLOCKED; }
	void tick() {
		if (DONE > 1 + job_state)	// micro-optimization: only needs to calculate 1 + job_state once
			++job_state;
	}
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
protected:
	// Our state machine guarantees that state transitions only ever increase the state value.
	// Even though we don't make use of it, our logic requires at least one state between FORCED and DONE, for timed-out jobs to sit in.
	enum { INITIAL, BLOCKED, ACTIONED, REREQUESTED = ACTIONED + 30, ORDERED = REREQUESTED + 30, FORCED = ORDERED + 30, TIMEDOUT = FORCED + 30, DONE } ;
	int job_state;
};
}

static inline
unsigned
want_for (
	int service_dir_fd
) {
	const bool want(is_initially_up(service_dir_fd));
	return want ? bundle::WANT_START : bundle::WANT_STOP;
}

typedef std::map<struct index, bundle> bundle_info_map;

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
	bundle_info_map & bundles,
	const char * arg,
	int want
) {
	std::string path, name, suffix;
	const int bundle_dir_fd(open_bundle_directory ("", arg, path, name, suffix));
	if (0 > bundle_dir_fd) return 0;
	return add_bundle(bundles, bundle_dir_fd, path, name, want);
}

static inline
void
add_primary_target_bundles (
	const char * prog,
	bundle_info_map & bundles,
	const std::vector<const char *> & args,
	int want
) {
	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		bundle * bp(add_bundle_searching_path(bundles, *i, want));
		if (!bp) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, *i, std::strerror(error));
			throw EXIT_FAILURE;
		}
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
	std::auto_ptr<char> buf(new(std::nothrow) char [s.st_size + 1]);
	if (!buf.get()) return;
	const int l(readlinkat(bundle_dir_fd, path, buf.get(), s.st_size));
	if (0 > l) return;
	buf.get()[l] = '\0';
	mkdirat(bundle_dir_fd, buf.get(), mode);
}

/* System control subcommands ***********************************************
// **************************************************************************
*/

void
start_stop_common ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	const char * prog,
	int want
) {
	const int socket_fd(connect_service_manager_socket(!per_user_mode, prog));
	if (0 > socket_fd) throw EXIT_FAILURE;

	// Create the list of primary target bundles from the command-line arguments, then add in all of the bundles that they relate to.
	bundle_info_map bundles;
	add_primary_target_bundles(prog, bundles, args, want);
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
						add_related_bundles(bundles, i->second, "required-by/", STOP);
						break;
				}
				done_one = true;
			}
		}
		if (!done_one) break;
	}

	// Check that we aren't starting and stopping a bundle at the same time.
	bool conflicts(false);
	for (bundle_info_map::const_iterator i(bundles.begin()); bundles.end() != i; ++i) {
		switch (i->second.wants) {
			case bundle::WANT_NONE:
			case bundle::WANT_START:
			case bundle::WANT_STOP:
				break;
			default:
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, i->second.name.c_str(), "Conflicting job requirements.");
				conflicts = true;
				break;
		}
	}
	if (conflicts) throw EXIT_FAILURE;

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
	for (bundle_pointer_list::const_iterator i(sorted.begin()); sorted.end() != i; ++i) {
		bundle & b(**i);
		if (bundle::WANT_NONE == b.wants) continue;
		make_symlink_target(b.bundle_dir_fd, "supervise", 0755);
		make_supervise (b.bundle_dir_fd);
		b.supervise_dir_fd = open_supervise_dir(b.bundle_dir_fd);
		if (0 > b.supervise_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, b.name.c_str(), "supervise", std::strerror(error));
		}
	}

	// Load any services (into the service manager) that are about to be started but that are not already loaded.
	// Do the same for their log services, even if those log services are not part of the calculated bundle set.
	// This is because the service manager must have the log service loaded in order to plumb the main service's output to the right place, even if the log service isn't being acted upon here.
	for (bundle_pointer_list::const_iterator i(sorted.begin()); sorted.end() != i; ++i) {
		bundle & b(**i);
		if (bundle::WANT_START != b.wants) continue;
		if (0 > b.supervise_dir_fd) continue;

		const bool was_already_loaded(is_ok(b.supervise_dir_fd));
		if (!was_already_loaded) {
			const bool run_on_empty(!is_done_after_exit(b.service_dir_fd));
			if (verbose)
				std::fprintf(stderr, "%s: LOAD: %s%s\n", prog, b.name.c_str(), run_on_empty ? " (remain)" : "");
			if (!pretending) {
				make_supervise_fifos (b.supervise_dir_fd);
				load(prog, socket_fd, b.name.c_str(), b.supervise_dir_fd, b.service_dir_fd);
				if (run_on_empty)
					make_run_on_empty(prog, socket_fd, b.supervise_dir_fd);
				make_pipe_connectable(prog, socket_fd, b.supervise_dir_fd);
				if (!wait_ok(b.supervise_dir_fd, 5000)) {
					std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, b.name.c_str(), "ok", "Unable to load service bundle.");
					continue;
				}
			}
		}

		const FileDescriptorOwner log_bundle_dir_fd(open_dir_at(b.bundle_dir_fd, "log/"));
		const FileDescriptorOwner log_supervise_dir_fd(open_supervise_dir(log_bundle_dir_fd.get()));
		const FileDescriptorOwner log_service_dir_fd(open_service_dir(log_bundle_dir_fd.get()));
		if (0 <= log_supervise_dir_fd.get() && 0 <= log_service_dir_fd.get()) {
			const bool log_was_already_loaded(is_ok(log_supervise_dir_fd.get()));
			if (!log_was_already_loaded) {
				const bool run_on_empty(!is_done_after_exit(log_service_dir_fd.get()));
				if (verbose)
					std::fprintf(stderr, "%s: LOAD: %s%s\n", prog, (b.name + "/log").c_str(), run_on_empty ? " (remain)" : "");
				if (!pretending) {
					make_supervise_fifos (log_supervise_dir_fd.get());
					load(prog, socket_fd, (b.name + "/log").c_str(), log_supervise_dir_fd.get(), log_service_dir_fd.get());
					if (run_on_empty)
						make_run_on_empty(prog, socket_fd, log_supervise_dir_fd.get());
					make_pipe_connectable(prog, socket_fd, log_supervise_dir_fd.get());
				}
			}
			if (!pretending)
				plumb(prog, socket_fd, b.supervise_dir_fd, log_supervise_dir_fd.get());
		}
	}

	// The main enacting loop; where we keep trying to start/stop any remaining services with pending actions until no more are left.
	for (;;) {
		bool pending(false);
		for (bundle_pointer_list::const_iterator i(sorted.begin()); sorted.end() != i; ++i) {
			bundle & b(**i);
			if (!b.done()) {
				bool is_done(true);
				switch (b.wants) {
					case bundle::WANT_START:
						is_done = 0 <= b.supervise_dir_fd && is_ok(b.supervise_dir_fd) && 0 < running_status(b.supervise_dir_fd);
						break;
					case bundle::WANT_STOP:
						is_done = 0 <= b.supervise_dir_fd && (!is_ok(b.supervise_dir_fd) || 0 < stopped_status(b.supervise_dir_fd));
						break;
				}
				if (is_done) {
					if (verbose)
						std::fprintf(stderr, "%s: DONE: %s%s\n", prog, b.path.c_str(), b.name.c_str());
					b.mark_done();
				}
			}
			if (b.done()) continue;
			pending = true;
			if (b.blocked()) {
				b.mark_unblocked();
				const bundle_pointer_set & a(b.sort_after);
				for (bundle_pointer_set::const_iterator j(a.begin()); a.end() != j; ++j) {
					bundle * p(*j);
					if (!p->done()) {
						if (verbose)
							std::fprintf(stderr, "%s: %s%s: BLOCKED by %s%s\n", prog, b.path.c_str(), b.name.c_str(), p->path.c_str(), p->name.c_str());
						b.mark_blocked();
						break;
					}
				}
				if (b.blocked()) continue;
			} 
			if (b.needs_action()) {
				switch (b.wants) {
					case bundle::WANT_START:
					{
						if (0 > b.supervise_dir_fd) break;
						const bool was_already_loaded(is_ok(b.supervise_dir_fd));
						if (!was_already_loaded)
							std::fprintf(stderr, "%s: CANNOT START: %s%s\n", prog, b.path.c_str(), b.name.c_str());
						else 
						if (b.needs_initial_action()) {
							if (verbose)
								std::fprintf(stderr, "%s: START: %s%s\n", prog, b.path.c_str(), b.name.c_str());
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
							std::fprintf(stderr, "%s: CANNOT STOP: %s%s\n", prog, b.path.c_str(), b.name.c_str());
						else
						if (b.needs_hardest_action()) {
							if (verbose)
								std::fprintf(stderr, "%s: STOP (hardest): %s%s\n", prog, b.path.c_str(), b.name.c_str());
							if (!pretending)
								b.stop_hardest();
						} else 
						if (b.needs_harder_action()) {
							if (verbose)
								std::fprintf(stderr, "%s: STOP (harder): %s%s\n", prog, b.path.c_str(), b.name.c_str());
							if (!pretending)
								b.stop_harder();
						} else 
						if (b.needs_initial_action()) {
							if (verbose)
								std::fprintf(stderr, "%s: STOP: %s%s\n", prog, b.path.c_str(), b.name.c_str());
							if (!pretending)
								b.stop_initial();
						}
						break;
					}
				}
			}
			b.tick();
		}
		if (!pending) break;
		sleep(1);
	}

	throw EXIT_SUCCESS;
}

void
activate ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition verbose_option('v', "verbose", "Display verbose information.", verbose);
		popt::bool_definition pretending_option('n', "pretend", "Pretend to take action, without telling the service manager to do anything.", pretending);
		popt::definition * main_table[] = {
			&user_option,
			&verbose_option,
			&pretending_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "service(s)...");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	start_stop_common(next_prog, args, prog, START);
}

void
deactivate ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition verbose_option('v', "verbose", "Display verbose information.", verbose);
		popt::bool_definition pretending_option('n', "pretend", "Pretend to take action, without telling the service manager to do anything.", pretending);
		popt::definition * main_table[] = {
			&user_option,
			&verbose_option,
			&pretending_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "service(s)...");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	start_stop_common(next_prog, args, prog, STOP);
}

void
isolate ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition verbose_option('v', "verbose", "Display verbose information.", verbose);
		popt::bool_definition pretending_option('n', "pretend", "Pretend to take action, without telling the service manager to do anything.", pretending);
		popt::definition * main_table[] = {
			&user_option,
			&verbose_option,
			&pretending_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "service(s)...");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	start_stop_common(next_prog, args, prog, START);
}

void
reset ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition verbose_option('v', "verbose", "Display verbose information.", verbose);
		popt::bool_definition pretending_option('n', "pretend", "Pretend to take action, without telling the service manager to do anything.", pretending);
		popt::definition * main_table[] = {
			&user_option,
			&verbose_option,
			&pretending_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "service(s)...");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	start_stop_common(next_prog, args, prog, DEFAULT);
}
