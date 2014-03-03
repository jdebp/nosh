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
#include "service-manager.h"
#include "common-manager.h"
#include "popt.h"

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
	bundle() : bundle_dir_fd(-1), supervise_dir_fd(-1), ss_scanned(false), job_state(INITIAL), order(-1), wants(WANT_NONE) {}
	~bundle() { 
		if (-1 != bundle_dir_fd) { close(bundle_dir_fd); bundle_dir_fd = -1; } 
		if (-1 != supervise_dir_fd) { close(supervise_dir_fd); supervise_dir_fd = -1; } 
	}

	enum {
		WANT_NONE = 0x0,
		WANT_START = 0x2,
		WANT_STOP = 0x4,
	};
	int bundle_dir_fd, supervise_dir_fd;
	std::string name;
	bool ss_scanned;
	enum { INITIAL, BLOCKED, CHANGING, DONE } job_state;
	int order;
	unsigned wants;
	bundle_pointer_set sort_after;
};
}

typedef std::map<struct index, bundle> bundle_info_map;

static inline
bundle *
add_bundle (
	bundle_info_map & bundles,
	const int bundle_dir_fd,
	const std::string & name,
	unsigned wants
) {
	struct stat bundle_dir_s;
	if (!is_directory(bundle_dir_fd, bundle_dir_s)) {
		const int error(errno);
		close(bundle_dir_fd);
		errno = error;
		return 0;
	}
	bundle_info_map::iterator bundle_i(bundles.find(bundle_dir_s));
	if (bundle_i != bundles.end()) {
		close(bundle_dir_fd);
		bundle_i->second.wants |= wants;
		return &bundle_i->second;
	}
	bundle & b(bundles[bundle_dir_s]);
	b.name = name;
	b.bundle_dir_fd = bundle_dir_fd;
	b.wants = wants;
	return &b;
}

static inline
bundle *
open_and_add_bundle (
	bundle_info_map & bundles,
	const std::string & path,
	const std::string & name,
	unsigned wants
) {
	const std::string paths(path + name + "/");
	const int bundle_dir_fd(open_dir_at(AT_FDCWD, paths.c_str()));
	if (0 > bundle_dir_fd) return 0;
	return add_bundle(bundles, bundle_dir_fd, name, wants);
}

static inline
bundle *
add_bundle_searching_path (
	bundle_info_map & bundles,
	const char * arg,
	unsigned wants
) {
	const std::string a(arg);
	if (const char * slash = std::strrchr(arg, '/')) {
		const std::string path(arg, slash + 1);
		const std::string name(slash + 1);
		return open_and_add_bundle(bundles, path, name, wants);
	}

	if (!local_session_mode) {
		for ( const char * const * q(bundle_prefixes); q < bundle_prefixes + sizeof bundle_prefixes/sizeof *bundle_prefixes; ++q) {
			const std::string suffix(*q);
			for ( const char * const * p(roots); p < roots + sizeof roots/sizeof *roots; ++p) {
				const std::string r(*p);
				const std::string path(r + suffix);
				if (bundle * b = open_and_add_bundle(bundles, path, a, wants))
					return b;
			}
		}
	}

	errno = ENOENT;
	return 0;
}

static inline
void
add_primary_target_bundles (
	const char * prog,
	bundle_info_map & bundles,
	const std::vector<const char *> & args,
	unsigned wants
) {
	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		bundle * bp(add_bundle_searching_path(bundles, *i, wants));
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
	unsigned wants
) {
	const int subdir_fd(open_dir_at(b.bundle_dir_fd, subdir_name));
	if (0 > subdir_fd) return;
	DIR * subdir_dir(fdopendir(subdir_fd));
	if (!subdir_dir) return;
	for (;;) {
		const dirent * entry(readdir(subdir_dir));
		if (!entry) break;
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_DIR != entry->d_type && DT_LNK != entry->d_type) continue;
#endif
		if ('.' == entry->d_name[0]) continue;
		const int dir_fd(open_dir_at(subdir_fd, entry->d_name));
		if (0 > dir_fd) continue;
		add_bundle(bundles, dir_fd, entry->d_name, wants);
	}
	closedir(subdir_dir);
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
	const int subdir_fd(open_dir_at(b.bundle_dir_fd, subdir_name));
	if (0 > subdir_fd) return r;
	DIR * subdir_dir(fdopendir(subdir_fd));
	if (!subdir_dir) return r;
	for (;;) {
		const dirent * entry(readdir(subdir_dir));
		if (!entry) break;
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_DIR != entry->d_type && DT_LNK != entry->d_type) continue;
#endif
		if ('.' == entry->d_name[0]) continue;
		const int dir_fd(open_dir_at(subdir_fd, entry->d_name));
		if (0 > dir_fd) continue;
		if (bundle * p = lookup_without_adding(bundles, dir_fd))
			r.insert(p);
		close(dir_fd);
	}
	closedir(subdir_dir);
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
	const char * & next_prog,
	std::vector<const char *> & args,
	const char * prog,
	unsigned want
) {
	const int socket_fd(connect_service_manager_socket(!local_session_mode, prog));
	if (0 > socket_fd) throw EXIT_FAILURE;

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
						add_related_bundles(bundles, i->second, "wants/", bundle::WANT_START);
						add_related_bundles(bundles, i->second, "conflicts/", bundle::WANT_STOP);
						break;
					case bundle::WANT_STOP:
						add_related_bundles(bundles, i->second, "required-by/", bundle::WANT_STOP);
						break;
				}
				done_one = true;
			}
		}
		if (!done_one) break;
	}

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

	for (bundle_pointer_set::const_iterator i(unsorted.begin()); unsorted.end() != i; ++i)
		mark_paths_in_order(0, *i);
	bundle_pointer_list sorted;
	for (bundle_pointer_set::const_iterator i(unsorted.begin()); unsorted.end() != i; ++i)
		insertion_sort(sorted, *i);

	umask(0);
	for (bundle_pointer_list::const_iterator i(sorted.begin()); sorted.end() != i; ++i) {
		bundle & b(**i);
		if (bundle::WANT_NONE == b.wants) continue;
		make_symlink_target(b.bundle_dir_fd, "supervise", 0755);
		make_supervise (b.bundle_dir_fd);
		b.supervise_dir_fd = open_dir_at(b.bundle_dir_fd, "supervise/");
		if (0 > b.supervise_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, b.name.c_str(), "supervise", std::strerror(error));
		}
	}

	for (bundle_pointer_list::const_iterator i(sorted.begin()); sorted.end() != i; ++i) {
		bundle & b(**i);
		if (bundle::WANT_START != b.wants) continue;
		if (0 > b.supervise_dir_fd) continue;

		const int service_dir_fd(open_dir_at(b.bundle_dir_fd, "service/"));
		if (0 > service_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, b.name.c_str(), "service", std::strerror(error));
			continue;
		}
		const bool was_already_loaded(is_ok(b.supervise_dir_fd));
		if (!was_already_loaded) {
			if (verbose)
				std::fprintf(stderr, "%s: LOAD: %s\n", prog, b.name.c_str());
			if (!pretending) {
				make_supervise_fifos (b.supervise_dir_fd);
				load(prog, socket_fd, b.name.c_str(), b.supervise_dir_fd, service_dir_fd, true, true);
				if (!wait_ok(b.supervise_dir_fd, 5000)) {
					std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, b.name.c_str(), "ok", "Unable to load service bundle.");
					close(service_dir_fd);
					continue;
				}
			}
		}
		close(service_dir_fd);

		const int log_supervise_dir_fd(open_dir_at(b.bundle_dir_fd, "log/supervise/"));
		const int log_service_dir_fd(open_dir_at(b.bundle_dir_fd, "log/service/"));
		if (0 <= log_supervise_dir_fd && 0 <= log_service_dir_fd) {
			const bool log_was_already_loaded(is_ok(log_supervise_dir_fd));
			if (!log_was_already_loaded) {
				if (verbose)
					std::fprintf(stderr, "%s: LOAD: %s\n", prog, (b.name + "/log").c_str());
				if (!pretending) {
					make_supervise_fifos (log_supervise_dir_fd);
					load(prog, socket_fd, (b.name + "/log").c_str(), log_supervise_dir_fd, log_service_dir_fd, true, true);
				}
			}
			if (!pretending)
				plumb(prog, socket_fd, b.supervise_dir_fd, log_supervise_dir_fd);
		}
		if (0 <= log_supervise_dir_fd)
			close(log_supervise_dir_fd);
		if (0 <= log_service_dir_fd)
			close(log_service_dir_fd);
	}

	for (;;) {
		bool pending(false);
		for (bundle_pointer_list::const_iterator i(sorted.begin()); sorted.end() != i; ++i) {
			bundle & b(**i);
			if (b.DONE != b.job_state) {
				bool is_done(true);
				switch (b.wants) {
					case bundle::WANT_START:
						is_done = 0 <= b.supervise_dir_fd && is_ok(b.supervise_dir_fd) && is_up(b.supervise_dir_fd);
						break;
					case bundle::WANT_STOP:
						is_done = 0 <= b.supervise_dir_fd && !(is_ok(b.supervise_dir_fd) && is_up(b.supervise_dir_fd));
						break;
				}
				if (is_done) {
					if (verbose)
						std::fprintf(stderr, "%s: DONE: %s\n", prog, b.name.c_str());
					b.job_state = b.DONE;
				}
			}
			if (b.DONE == b.job_state) continue;
			pending = true;
			if (b.CHANGING == b.job_state) continue;

			b.job_state = b.CHANGING;
			const bundle_pointer_set & a(b.sort_after);
			for (bundle_pointer_set::const_iterator j(a.begin()); a.end() != j; ++j) {
				bundle * p(*j);
				if (p->DONE != p->job_state) {
					if (verbose)
						std::fprintf(stderr, "%s: %s: BLOCKED by %s\n", prog, b.name.c_str(), p->name.c_str());
					b.job_state = b.BLOCKED;
					break;
				}
			}
			if (b.CHANGING != b.job_state) continue;

			switch (b.wants) {
				case bundle::WANT_START:
				{
					if (0 > b.supervise_dir_fd) break;
					const bool was_already_loaded(is_ok(b.supervise_dir_fd));
					if (was_already_loaded) {
						if (verbose)
							std::fprintf(stderr, "%s: START: %s\n", prog, b.name.c_str());
						if (!pretending)
							start(prog, b.supervise_dir_fd);
					} else
						std::fprintf(stderr, "%s: CANNOT START: %s\n", prog, b.name.c_str());
					break;
				}
				case bundle::WANT_STOP:
				{
					if (0 > b.supervise_dir_fd) break;
					const bool was_already_loaded(is_ok(b.supervise_dir_fd));
					if (was_already_loaded) {
						if (verbose)
							std::fprintf(stderr, "%s: STOP: %s\n", prog, b.name.c_str());
						if (!pretending)
							stop(prog, b.supervise_dir_fd);
					} else
						std::fprintf(stderr, "%s: CANNOT STOP: %s\n", prog, b.name.c_str());
					break;
				}
			}
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
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
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

	start_stop_common(next_prog, args, prog, bundle::WANT_START);
}

void
deactivate ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
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

	start_stop_common(next_prog, args, prog, bundle::WANT_STOP);
}

void
isolate ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
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

	start_stop_common(next_prog, args, prog, bundle::WANT_START);
}
