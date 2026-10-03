/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <map>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <limits>
#include <inttypes.h>
#include <unistd.h>
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/types.h>
#include <sys/user.h>	// for kinfo_proc
#include <sys/proc.h>	// for various state/flag macros
#include <sys/stat.h>	// for S_IFCHR
#include <sys/sysctl.h>
#include <sys/param.h>
#include <sys/cpuset.h>
#include <csignal>
#elif defined(__NetBSD__)
#include <sys/sysctl.h>	// has kinfo_proc2
#include <csignal>
#endif
#include "popt.h"
#include "ttyname.h"
#include "utils.h"
#include "fdutils.h"
#include "FileDescriptorOwner.h"
#include "DirStar.h"
#include "FileStar.h"
#include "VisEncoder.h"

/* The process table and field list *****************************************
// **************************************************************************
*/

namespace {

typedef std::list<std::string> FieldList;
typedef std::map<std::string, std::string> ProcessRecord;
typedef std::list<ProcessRecord> ProcessTable;

struct wanted {
	wanted() : auxv(false), cmdline(false), environ(false), cwd(false), root(false), stat(false), cpuset(false) {}
	bool auxv, cmdline, environ, cwd, root, stat, statm, status, cpuset;
} ;

wanted
CheckFields (
	const FieldList & fields
) {
	wanted w;
	for (FieldList::const_iterator i(fields.begin()), e(fields.end()); i != e; ++i) {
		if (!w.auxv && "auxv" == *i) w.auxv = true;
		if (!w.cmdline && "args" == *i) w.cmdline = true;
		if (!w.environ && "envs" == *i) w.environ = true;
		if (!w.cwd && "cwd" == *i) w.cwd = true;
		if (!w.root && "root" == *i) w.cwd = true;
		// FIXME: Be more specific here.
		if (!w.stat) w.stat = true;
		if (!w.statm) w.statm = true;
		if (!w.status) w.status = false;
		if (!w.cpuset && ("csid" == *i || "rcsid" == *i || "csmask" == *i)) w.cpuset = true;
	}
	return w;
}

inline
std::string
display_name_for (
	const std::string & s
) {
	if ("args" == s)
		return "COMMAND";
	else
	if ("nice" == s)
		return "NI";
	else
	if ("tty" == s)
		return "TT";
	else
	if ("pcpu" == s)
		return "%CPU";
	else
	if ("etime" == s)
		return "ELAPSED";
	else
	if ("flags" == s)
		return "F";
	else
	if ("state" == s)
		return "S";
	else
	if ("address" == s)
		return "ADDR";
	else
	if ("size" == s)
		return "SZ";
	else
	if ("logname" == s)
		return "LOGIN";
	else
	{
		std::string r(s);
		for (std::string::iterator e(r.end()), p(r.begin()); p != e; ++p) {
			if (std::isalpha(*p))
				*p = std::toupper(*p);
		}
		return r;
	}
}

std::string
make_graphical_tree (
	const std::string & o
) {
	std::string r;
	for (std::string::const_iterator op(o.begin()), oe(o.end()); op != oe; ) {
		// We constructed the string with pairs of 2 characters, so we work 2 characters at a time.
		const char c0(*op++);
		if (op == oe) {
			r.push_back(c0);
			continue;
		}
		const char c1(*op++);
		switch (c0) {
			default:
			unknown:
				r.push_back(c0); r.push_back(c1);
				break;
			case '*':
				switch (c1) {
					case '-':
						// U+0000257E box drawings heavy left and light right
						r.push_back('\xe2'); r.push_back('\x95'); r.push_back('\xBE');
						// U+00002500 box drawings light horizontal
						r.push_back('\xe2'); r.push_back('\x94'); r.push_back('\x80');
						break;
					default:
						goto unknown;
				}
				break;
			case '+':
				switch (c1) {
					case '-':
						// U+0000251C box drawings light vertical and right
						r.push_back('\xe2'); r.push_back('\x94'); r.push_back('\x9C');
						// U+00002500 box drawings light horizontal
						r.push_back('\xe2'); r.push_back('\x94'); r.push_back('\x80');
						break;
					default:
						goto unknown;
				}
				break;
			case '|':
				switch (c1) {
					case '-':
						// U+0000251C box drawings light vertical and right
						r.push_back('\xe2'); r.push_back('\x94'); r.push_back('\x9C');
						// U+00002500 box drawings light horizontal
						r.push_back('\xe2'); r.push_back('\x94'); r.push_back('\x80');
						break;
					case ' ':
						// U+00002502 box drawings light vertical
						r.push_back('\xe2'); r.push_back('\x94'); r.push_back('\x82');
						r.push_back(c1);
						break;
					default:
						goto unknown;
				}
				break;
			case '`':
				switch (c1) {
					case '-':
						// U+00002514 box drawings light up and right
						r.push_back('\xe2'); r.push_back('\x94'); r.push_back('\x94');
						// U+00002500 box drawings light horizontal
						r.push_back('\xe2'); r.push_back('\x94'); r.push_back('\x80');
						break;
					default:
						goto unknown;
				}
				break;
			case '.':
				switch (c1) {
					case ' ':
						// U+0000252C box drawings light down and horizontal
						r.push_back('\xe2'); r.push_back('\x94'); r.push_back('\xAC');
						// U+0000257C box drawings light left and heavy right
						r.push_back('\xe2'); r.push_back('\x95'); r.push_back('\xBC');
						break;
					default:
						goto unknown;
				}
				break;
			case '-':
				switch (c1) {
					case ' ':
						// U+00002500 box drawings light horizontal
						r.push_back('\xe2'); r.push_back('\x94'); r.push_back('\x80');
						// U+0000257C box drawings light left and heavy right
						r.push_back('\xe2'); r.push_back('\x95'); r.push_back('\xBC');
						break;
					default:
						goto unknown;
				}
				break;
		}
	}
	return r;
}

}

/* Common utilities *********************************************************
// **************************************************************************
*/

namespace {

#if defined(__FreeBSD__) || defined(__DragonFly__)

std::ostream &
operator << (std::ostream & os, const sigset_t & s)
{
	char codebuf[16];
	bool first(true);
	for (int signo(1); signo < NSIG; ++signo) {
		if (!sigismember(&s, signo)) continue;
		snprintf(codebuf, sizeof codebuf, "%u", signo);
		const char * sname(signame(signo));
		if (!sname) sname = codebuf;
		if (first) first = false; else os.put(',');
		os << sname;
	}
	return os;
}

#elif defined(__NetBSD__)

inline
uint32_t
(sigmask) (
	unsigned int signo1
) {
	return 1U << (signo1 & 31U);
}

inline
std::size_t
(sigword) (
	unsigned int signo1
) {
	return signo1 >> 5U;
}

inline
bool
sigismember (
	const ki_sigset_t & s,
	int signo
) {
	return signo < 1 ? false : !!(s.__bits[(sigword)(signo - 1)] & (sigmask)(signo - 1));
}

std::ostream &
operator << (std::ostream & os, const ki_sigset_t & s)
{
	char codebuf[16];
	bool first(true);
	for (int signo(1); signo < NSIG; ++signo) {
		if (!sigismember(s, signo)) continue;
		snprintf(codebuf, sizeof codebuf, "%u", signo);
		const char * sname(signame(signo));
		if (!sname) sname = codebuf;
		if (first) first = false; else os.put(',');
		os << sname;
	}
	return os;
}

#endif

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__)

std::string
rtrim (
	const char * s,
	std::size_t l
) {
	while (l > 0) {
		--l;
		if (s[l] && !std::isspace(s[l]))
			return std::string(s, l + 1);
	}
	return std::string();
}

template<typename T>
std::string
str (
	const T & x
) {
	std::ostringstream os;
	os << x;
	return os.str();
}

template<typename T>
std::string
percentstr (
	const T & x
) {
	std::ostringstream os;
	os << std::setiosflags(std::ios::fixed) << std::setprecision(2) << x;
	return os.str();
}

template<typename T>
std::string
hexstr (
	const T & x
) {
	std::ostringstream os;
	os << std::hex << x;
	return os.str();
}

std::string
timestr (
	const timeval & t,
	const char * time_format
) {
	tm b;
	localtime_r(&t.tv_sec, &b);
	char buf[64];
	std::size_t len(std::strftime(buf, sizeof buf, time_format, &b));
	return std::string(buf, len);
}

std::string
timestr (
	uint32_t sec,
	uint32_t microsec,
	const char * time_format
) {
	timeval t;
	t.tv_sec = sec;
	t.tv_usec = microsec;
	return timestr(t, time_format);
}

std::string
timestr (
	uint32_t t,
	uint32_t u
) {
	const unsigned short s(t % 60U);
	t /= 60U;
	const unsigned short m(t % 60U);
	t /= 60U;
	const unsigned short h(t % 60U);
	t /= 60U;
	char buf[64];
	const std::size_t len(
		t > 0U ? std::snprintf(buf, sizeof buf, "%" PRIu32 "d %2huh %2hum %2hu.%06" PRIu32 "s", t, h, m, s, u) :
		h > 0U ? std::snprintf(buf, sizeof buf, "%2huh %2hum %2hu.%06" PRIu32 "s", h, m, s, u) :
		m > 0U ? std::snprintf(buf, sizeof buf, "%2hum %2hu.%06" PRIu32 "s", m, s, u) :
		std::snprintf(buf, sizeof buf, "%2hu.%06" PRIu32 "s", s, u)
	);
	return std::string(buf, len);
}

std::string
timestr (
	uint64_t t
) {
	const unsigned long u(t % 1000000UL);
	t /= 1000000UL;
	const unsigned short s(t % 60U);
	t /= 60U;
	const unsigned short m(t % 60U);
	t /= 60U;
	const unsigned short h(t % 60U);
	t /= 60U;
	char buf[64];
	const std::size_t len(
		t > 0U ? std::snprintf(buf, sizeof buf, "%" PRIu64 "d %2huh %2hum %2hu.%06lus", t, h, m, s, u) :
		h > 0U ? std::snprintf(buf, sizeof buf, "%2huh %2hum %2hu.%06lus", h, m, s, u) :
		m > 0U ? std::snprintf(buf, sizeof buf, "%2hum %2hu.%06lus", m, s, u) :
		std::snprintf(buf, sizeof buf, "%2hu.%06lus", s, u)
	);
	return std::string(buf, len);
}

inline
void
convertprocstrings (
	std::list<std::string> & v,
	const std::vector<char> & buf,
	std::size_t max = std::numeric_limits<std::size_t>::max()
) {
	std::string * current(nullptr);
	for (std::vector<char>::const_iterator p(buf.begin()), e(buf.end()); p != e; ++p) {
		const char c(*p);
		if (!current) {
			if (max <= v.size()) break;
			v.push_back(std::string());
			current = &v.back();
		}
		if ('\0' == c)
			current = nullptr;
		else
			*current += c;
	}
}

#endif

}

/* Populating the process table *********************************************
// **************************************************************************
*/

namespace {

#if defined(__FreeBSD__) || defined(__DragonFly__)

inline
std::string
statestr (
	const kinfo_proc & k
) {
	std::string r;
	switch (k.ki_stat) {
		case SIDL:	r += 'I'; break;
		case SRUN:	r += 'R'; break;
		case SSLEEP:	r += 'S'; break;
		case SSTOP:	r += 'T'; break;
		case SZOMB:	r += 'Z'; break;
		case SWAIT:	r += 'W'; break;
		case SLOCK:	r += 'L'; break;
		default:	r += '?'; break;
	}
	if (k.ki_flag & P_TRACED) r += 'P';
	if (k.ki_flag & P_PPWAIT) r += 'p';
	if (k.ki_flag & P_SUGID) r += '!';
	if (k.ki_flag & P_WEXIT) r += 'E';
	if (k.ki_flag & P_INEXEC) r += 'e';
	if (!(k.ki_flag & P_INMEM)) r += 'w';
	if (k.ki_flag & P_JAILED) r += 'J';
	if (k.ki_flag & P_STOPPED) r += 't';
	if (k.ki_flag & P_WKILLED) r += 'k';
	if (k.ki_kiflag & KI_SLEADER) r += 's';
	if (k.ki_kiflag & KI_CTTY) r += 'c';
	if (k.ki_kiflag & KI_LOCKBLOCK) r += 'l';
	return r;
}

inline
std::string
cpustr (
	uint8_t cpu
) {
	return NOCPU == cpu ? "-" : str(static_cast<unsigned int>(cpu));
}

inline
void
populate_record (
	ProcessRecord & b,
	const kinfo_proc & k,
	const char * time_format
) {
	b["paddr"] = str(static_cast<void *>(k.ki_paddr));
	b["addr"] = str(static_cast<void *>(k.ki_addr));
	b["wchan"] = str(k.ki_wchan);
	b["pid"] = str(k.ki_pid);
	b["ppid"] = str(k.ki_ppid);
	b["pgid"] = str(k.ki_pgid);
	b["tpgid"] = str(k.ki_tpgid);
	b["sid"] = str(k.ki_sid);
	b["tsid"] = str(k.ki_tsid);
	b["jobc"] = str(k.ki_jobc);
	b["siglist"] = str(k.ki_siglist);
	b["sigmask"] = str(k.ki_sigmask);
	b["sigignore"] = str(k.ki_sigignore);
	b["sigcatch"] = str(k.ki_sigcatch);
	b["uid"] = str(k.ki_uid);
	// Missing: SUS says user is the username from the uid
	b["ruid"] = str(k.ki_ruid);
	// Missing: SUS says ruser is the username from the ruid
	b["svuid"] = str(k.ki_svuid);
	b["rgid"] = str(k.ki_rgid);
	// Missing: SUS says group is the username from the gid
	// Missing: SUS says rgroup is the username from the rgid
	b["svgid"] = str(k.ki_svgid);
	b["size"] = str(k.ki_size);
	b["rss"] = str(k.ki_rssize);
	b["swrss"] = str(k.ki_swrss);
	b["tsize"] = str(k.ki_tsize);
	b["dsize"] = str(k.ki_dsize);
	b["ssize"] = str(k.ki_ssize);
	b["xstat"] = str(k.ki_xstat);
	b["acflag"] = str(k.ki_acflag);
	b["pcpu"] = percentstr((k.ki_pctcpu * 100.0) / 2048.0);
	b["estcpu"] = str(k.ki_estcpu);
	b["slptime"] = str(k.ki_slptime);
	b["swtime"] = str(k.ki_swtime);
	b["cow"] = str(k.ki_cow);
	b["time"] = timestr(k.ki_runtime);
	b["start"] = timestr(k.ki_start, time_format);
	// Missing: SUS says etime is the elapsed time since start as [[dd-]hh:]mm:ss
	b["childtime"] = timestr(k.ki_childtime, time_format);
	b["flags"] = hexstr(k.ki_flag);
	b["kiflags"] = hexstr(k.ki_kiflag);
	b["traceflags"] = hexstr(k.ki_traceflag);
	b["nice"] = str(static_cast<signed int>(k.ki_nice));
	b["lock"] = str(static_cast<signed int>(k.ki_lock));
	b["rqindex"] = str(static_cast<signed int>(k.ki_rqindex));
	b["oncpu"] = cpustr(k.ki_oncpu);
	b["lastcpu"] = cpustr(k.ki_lastcpu);
	b["tdname"] = rtrim(k.ki_tdname, sizeof k.ki_tdname);
	b["mwchan"] = rtrim(k.ki_wmesg, sizeof k.ki_wmesg);
	b["logname"] = rtrim(k.ki_login, sizeof k.ki_login);
	b["lockname"] = rtrim(k.ki_lockname, sizeof k.ki_lockname);
	const std::string comm(rtrim(k.ki_comm, sizeof k.ki_comm));
	b["emul"] = rtrim(k.ki_emul, sizeof k.ki_emul);
	b["loginclass"] = rtrim(k.ki_loginclass, sizeof k.ki_loginclass);
	b["flags2"] = hexstr(k.ki_flag2);
	b["fibnum"] = hexstr(k.ki_fibnum);
	b["crflags"] = hexstr(k.ki_cr_flags);
	b["jid"] = str(k.ki_jid);
	b["threads"] = str(k.ki_numthreads);
	b["tid"] = str(k.ki_tid);
	b["priclass"] = str(static_cast<signed int>(k.ki_pri.pri_class));
	b["prilevel"] = str(static_cast<signed int>(k.ki_pri.pri_level));
	b["prinative"] = str(static_cast<signed int>(k.ki_pri.pri_native));
	b["pri"] = str(static_cast<signed int>(k.ki_pri.pri_user));
	b["pcbaddr"] = str(static_cast<void *>(k.ki_pcb));
	b["stackaddr"] = str(static_cast<void *>(k.ki_kstack));
	b["udata"] = str(static_cast<void *>(k.ki_udata));
	b["tdaddr"] = str(static_cast<void *>(k.ki_tdaddr));
	b["sflags"] = hexstr(k.ki_sflag);
	b["tdflags"] = hexstr(k.ki_tdflags);

	b["state"] = statestr(k);
	b["vsz"] = str(k.ki_size / 1024U);
	std::string groups;
	for (short i(0); i < k.ki_ngroups; ++i) {
		if (i) groups += ",";
		groups += str(k.ki_groups[i]);
	}
	b["groups"] = groups;
	if (NODEV == static_cast<dev_t>(k.ki_tdev)) {
		// The value is not NULL for no terminal device.
		b["tt"] = b["tty"] = b["tdev"] = "";
	} else
	{
		b["tdev"] = str(k.ki_tdev);
		if (const char * tty = devname(k.ki_tdev, S_IFCHR)) {
			b["tt"] = id_field_from(tty);
			b["tty"] = tty;
		}
	}
	b["comm"] = comm;
	b["command"] = "[" + comm + "]";
	// Missing: USER MEM
}

std::list<std::string>
getprocstrings (
	const char * prog,
	const ProcessEnvironment & envs,
	int n,
	const pid_t pid
) {
	const int oid[4] = {
		CTL_KERN,
		KERN_PROC,
		n,
		pid
	};
	std::list<std::string> v;
	std::size_t len(0);
	if (0 > sysctl(oid, sizeof oid/sizeof *oid, nullptr, &len, nullptr, 0)) {
	bad_sysctl:
		if (EPERM == errno || ESRCH == errno) return v;
		die_errno(prog, envs, "sysctl");
	}
	std::vector<char> buf(len);
	if (0 > sysctl(oid, sizeof oid/sizeof *oid, buf.data(), &len, nullptr, 0)) goto bad_sysctl;
	convertprocstrings(v, buf);
	return v;
}

std::string
getprocargs (
	const char * prog,
	const ProcessEnvironment & envs,
	const pid_t pid
) {
	const std::list<std::string> v(getprocstrings(prog, envs, KERN_PROC_ARGS, pid));
	std::string r;
	for (std::list<std::string>::const_iterator b(v.begin()), e(v.end()), p(b); p != e; ++p) {
		if (p != b) r += ' ';
		r += quote_for_sh(*p);
	}
	return r;
}

std::string
getprocenvs (
	const char * prog,
	const ProcessEnvironment & envs,
	const pid_t pid
) {
	const std::list<std::string> v(getprocstrings(prog, envs, KERN_PROC_ENV, pid));
	std::string r;
	for (std::list<std::string>::const_iterator b(v.begin()), e(v.end()), p(b); p != e; ++p) {
		if (p != b) r += ' ';
		r += quote_for_conf(*p);
	}
	return r;
}

void
populate_cpuset (
	const char * prog,
	const ProcessEnvironment & envs,
	ProcessRecord & b,
	bool threads,
	const kinfo_proc & k
) {
	cpusetid_t rootcpuset, cpuset;
	if (0 > cpuset_getid(CPU_LEVEL_ROOT, threads ? CPU_WHICH_TID : CPU_WHICH_PID, threads ? k.ki_tid : k.ki_pid, &rootcpuset)
	||  0 > cpuset_getid(CPU_LEVEL_CPUSET, threads ? CPU_WHICH_TID : CPU_WHICH_PID, threads ? k.ki_tid : k.ki_pid, &cpuset)
	) {
		if (EPERM == errno) {
			b["rcsid"] = b["csid"] = b["csmask"] = "!su";
			return;
		}
		die_errno(prog, envs, "cpuset_getid");
	}
	b["rcsid"] = str(rootcpuset);
	b["csid"] = str(cpuset);
	b["csmask"] = std::string();
}

ProcessTable
read_table (
	const char * prog,
	const ProcessEnvironment & envs,
	const FieldList & fields,
	bool threads,
	const char * time_format
) {
	const int oid[3] = {
		CTL_KERN,
		KERN_PROC,
		threads ? KERN_PROC_ALL : KERN_PROC_PROC,
	};
	unsigned char * buf(nullptr);
	std::size_t len(0);

	// Measure the size needed, with NULL for buf.
	if (0 > sysctl(oid, sizeof oid/sizeof *oid, buf, &len, nullptr, 0)) {
	bad_sysctl:
		die_errno(prog, envs, "sysctl");
	}
	// The process table can change size on the fly, so keep trying until we no longer get an ENOMEM.
	for (;;) {
		buf = new unsigned char [len];
		if (0 <= sysctl(oid, sizeof oid/sizeof *oid, buf, &len, nullptr, 0)) break;
		if (ENOMEM != errno) goto bad_sysctl;
		len += (len + 7) / 8;
		delete[] buf;
	}

	const wanted w(CheckFields(fields));
	ProcessTable r;
	for (const kinfo_proc * k(reinterpret_cast<const kinfo_proc *>(buf)),
			* e(reinterpret_cast<const kinfo_proc *>(buf + len));
	     k < e;
	     ++k) {
		if (k->ki_structsize != sizeof *k) {
			std::fprintf(stderr, "%s: FATAL: Mismatch between kernel struct size %d and library header struct size %zu; application should be built with correct library.\n", prog, k->ki_structsize, sizeof *k);
			throw EXIT_FAILURE;
		}
		r.push_back(ProcessRecord());
		ProcessRecord & b(r.back());
		populate_record(b, *k, time_format);
		if (w.cmdline)
			b["args"] = getprocargs(prog, envs, k->ki_pid);
		if (w.environ)
			b["envs"] = getprocenvs(prog, envs, k->ki_pid);
		if (w.cpuset)
			populate_cpuset(prog, envs, b, threads, *k);
	}
	delete[] buf;

	return r;
}

#elif defined(__NetBSD__)

inline
std::string
statestr (
	const kinfo_proc2 & k
) {
	std::string r;
	switch (k.p_stat) {
		case SIDL:	r += 'I'; break;
		case SACTIVE:	r += 'R'; break;
		case SDYING:	r += 'D'; break;
		case SSTOP:	r += 'T'; break;
		case SZOMB:	r += 'Z'; break;
		case SDEAD:	r += 'X'; break;
		default:	r += '?'; break;
	}
	if (k.p_flag & P_TRACED) r += 'P';
	if (k.p_flag & P_PPWAIT) r += 'p';
	if (k.p_flag & P_SUGID) r += '!';
	if (k.p_flag & P_WEXIT) r += 'E';
	if (k.p_flag & P_EXEC) r += 'e';
	if (!(k.p_flag & P_INMEM)) r += 'w';
	if (k.p_eflag & EPROC_SLEADER) r += 's';
	if (k.p_flag & P_CONTROLT) r += 'c';
	if (k.p_flag & P_32) r += '3';
	if (k.p_flag & P_SYSTEM) r += '0';
	if (k.p_flag & P_CLDSIGIGN) r += 'i';
	if (k.p_flag & P_NOCLDWAIT) r += 'n';
	if (k.p_eflag & EPROC_CTTY) r += 't';
	if (k.p_traceflag) r += 'K';
	return r;
}

std::list<std::string>
getprocstrings (
	const char * prog,
	const ProcessEnvironment & envs,
	int nc,
	int nv,
	const pid_t pid
) {
	int oid[4] = {
		CTL_KERN,
		KERN_PROC_ARGS,
		pid,
		0
	};
	std::list<std::string> v;
	int c(0);
	std::size_t len(sizeof c);
	oid[3] = nc;
	if (0 > sysctl(oid, sizeof oid/sizeof *oid, &c, &len, nullptr, 0)) {
	bad_sysctl:
		if (EPERM == errno || ESRCH == errno || EINVAL == errno) return v;
		die_errno(prog, envs, "sysctl");
	}
	len = sysconf(_SC_ARG_MAX);
	oid[3] = nv;
	if (0 > sysctl(oid, sizeof oid/sizeof *oid, nullptr, &len, nullptr, 0)) goto bad_sysctl;
	std::vector<char> buf(len);
	if (0 > sysctl(oid, sizeof oid/sizeof *oid, buf.data(), &len, nullptr, 0)) goto bad_sysctl;
	convertprocstrings(v, buf, c);
	return v;
}

std::string
getprocargs (
	const char * prog,
	const ProcessEnvironment & envs,
	const pid_t pid
) {
	const std::list<std::string> v(getprocstrings(prog, envs, KERN_PROC_NARGV, KERN_PROC_ARGV, pid));
	std::string r;
	for (std::list<std::string>::const_iterator b(v.begin()), e(v.end()), p(b); p != e; ++p) {
		if (p != b) r += ' ';
		r += quote_for_sh(*p);
	}
	return r;
}

std::string
getprocenvs (
	const char * prog,
	const ProcessEnvironment & envs,
	const pid_t pid
) {
	const std::list<std::string> v(getprocstrings(prog, envs, KERN_PROC_NENV, KERN_PROC_ENV, pid));
	std::string r;
	for (std::list<std::string>::const_iterator b(v.begin()), e(v.end()), p(b); p != e; ++p) {
		if (p != b) r += ' ';
		r += quote_for_conf(*p);
	}
	return r;
}

inline
void
populate_record (
	ProcessRecord & b,
	const kinfo_proc2 & k,
	const char * time_format
) {
	b["paddr"] = str(reinterpret_cast<void *>(k.p_paddr));
	b["addr"] = str(reinterpret_cast<void *>(k.p_addr));
	b["wchan"] = str(k.p_wchan);
	b["pid"] = str(k.p_pid);
	b["ppid"] = str(k.p_ppid);
	b["pgid"] = str(k.p__pgid);
	b["tpgid"] = str(k.p_tpgid);
	b["sid"] = str(k.p_sid);
	b["jobc"] = str(k.p_jobc);
	b["siglist"] = str(k.p_siglist);
	b["sigmask"] = str(k.p_sigmask);
	b["sigignore"] = str(k.p_sigignore);
	b["sigcatch"] = str(k.p_sigcatch);
	b["uid"] = str(k.p_uid);
	// Missing: SUS says user is the username from the uid
	b["ruid"] = str(k.p_ruid);
	// Missing: SUS says ruser is the username from the ruid
	b["svuid"] = str(k.p_svuid);
	b["rgid"] = str(k.p_rgid);
	// Missing: SUS says group is the username from the gid
	// Missing: SUS says rgroup is the username from the rgid
	b["svgid"] = str(k.p_svgid);
	b["size"] = str(k.p_vm_vsize);
	b["rss"] = str(k.p_vm_rssize);
	b["tsize"] = str(k.p_vm_tsize);
	b["dsize"] = str(k.p_vm_dsize);
	b["ssize"] = str(k.p_vm_ssize);
	b["xstat"] = str(k.p_xstat);
	b["acflag"] = str(k.p_acflag);
	b["pcpu"] = percentstr((k.p_pctcpu * 100.0) / 2048.0);
	b["estcpu"] = str(k.p_estcpu);
	b["slptime"] = str(k.p_slptime);
	b["swtime"] = str(k.p_swtime);
	b["time"] = timestr(k.p_rtime_sec, k.p_rtime_usec);
	b["start"] = timestr(k.p_ustart_sec, k.p_ustart_usec, time_format);
	b["utime"] = timestr(k.p_uutime_sec, k.p_uutime_usec);
	b["stime"] = timestr(k.p_ustime_sec, k.p_ustime_usec);
	// Missing: SUS says etime is the elapsed time since start as [[dd-]hh:]mm:ss
	b["childtime"] = timestr(k.p_uctime_sec, k.p_uctime_usec);
	b["flags"] = hexstr(k.p_flag);
	b["traceflags"] = hexstr(k.p_traceflag);
	b["nice"] = str(static_cast<signed int>(k.p_nice));
	b["lastcpu"] = str(k.p_cpuid);
	b["mwchan"] = rtrim(k.p_wmesg, sizeof k.p_wmesg);
	b["logname"] = rtrim(k.p_login, sizeof k.p_login);
	b["emul"] = rtrim(k.p_ename, sizeof k.p_ename);
	b["threads"] = str(k.p_nlwps);
	b["prinative"] = str(static_cast<signed int>(k.p_priority));
	b["pri"] = str(static_cast<signed int>(k.p_usrpri));
	b["udata"] = str(reinterpret_cast<void *>(k.p_addr));

	b["state"] = statestr(k);
	b["vsz"] = str(k.p_vm_vsize / 1024U);
	std::string groups;
	for (short i(0); i < k.p_ngroups; ++i) {
		if (i) groups += ",";
		groups += str(k.p_groups[i]);
	}
	b["groups"] = groups;
	if (KERN_PROC_TTY_NODEV == static_cast<dev_t>(k.p_tdev)) {
		// The value is not NULL for no terminal device.
		b["tt"] = b["tty"] = b["tdev"] = "";
	} else
	if (KERN_PROC_TTY_REVOKE == static_cast<dev_t>(k.p_tdev)) {
		b["tt"] = b["tty"] = b["tdev"] = "revoked";
	} else
	{
		b["tdev"] = str(k.p_tdev);
		if (const char * tty = devname(k.p_tdev, S_IFCHR)) {
			b["tt"] = id_field_from(tty);
			b["tty"] = tty;
		}
	}
	const std::string comm(rtrim(k.p_comm, sizeof k.p_comm));
	b["comm"] = comm;
	b["command"] = "[" + comm + "]";
	// Missing: USER MEM
}

ProcessTable
read_table (
	const char * prog,
	const ProcessEnvironment & envs,
	const FieldList & fields,
	bool /*threads*/,
	const char * time_format
) {
	int oid[6] = {
		CTL_KERN,
		KERN_PROC2,
		KERN_PROC_ALL,
		0,
		sizeof(kinfo_proc2),
		0
	};
	unsigned char * buf(nullptr);
	std::size_t len(0);

	// Measure the size needed, with NULL for buf.
	if (0 > sysctl(oid, sizeof oid/sizeof *oid, buf, &len, nullptr, 0)) {
	bad_sysctl:
		die_errno(prog, envs, "sysctl");
	}
	// The process table can change size on the fly, so keep trying until we no longer get an ENOMEM.
	for (;;) {
		oid[5] = len / sizeof(kinfo_proc2);
		buf = new unsigned char [len];
		if (0 <= sysctl(oid, sizeof oid/sizeof *oid, buf, &len, nullptr, 0)) break;
		if (ENOMEM != errno) goto bad_sysctl;
		len += (len + 7) / 8;
		delete[] buf;
	}

	const wanted w(CheckFields(fields));
	ProcessTable r;
	for (const kinfo_proc2 * k(reinterpret_cast<const kinfo_proc2 *>(buf)),
			* e(reinterpret_cast<const kinfo_proc2 *>(buf + len));
	     k < e;
	     ++k) {
		r.push_back(ProcessRecord());
		ProcessRecord & b(r.back());
		populate_record(b, *k, time_format);
		if (w.cmdline)
			b["args"] = getprocargs(prog, envs, k->p_pid);
		if (w.environ)
			b["envs"] = getprocenvs(prog, envs, k->p_pid);
	}
	delete[] buf;

	return r;
}

#else

const char procdir[] = "/proc";

bool
is_numeric (
	const char * s
) {
	while (*s) {
		if (!std::isdigit(*s)) return false;
		++s;
	}
	return true;
}

std::list<std::string>
read_text0_file (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * procdir,
	const char * subdir,
	const char * name,
	FileDescriptorOwner & fd
) {
	FileStar f(fdopen(fd.get(), "r"));
	if (!f) {
exit_error:
		die_errno(prog, envs, procdir, subdir, name);
	}
	fd.release();
	std::list<std::string> v;
	for (std::string * current(nullptr);;) {
		int c(std::fgetc(f));
		if (std::ferror(f)) goto exit_error;
		if (std::feof(f)) break;
		if (!current) {
			v.push_back(std::string());
			current = &v.back();
		}
		if ('\0' == c) {
			current = nullptr;
			continue;
		}
		*current += char(c);
	}
	return v;
}

std::string
read_cmdline_file (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * procdir,
	const char * subdir,
	const char * name,
	FileDescriptorOwner & fd
) {
	const std::list<std::string> v(read_text0_file(prog, envs, procdir, subdir, name, fd));
	std::string r;
	for (std::list<std::string>::const_iterator b(v.begin()), e(v.end()), p(b); p != e; ++p) {
		if (p != b) r += ' ';
		r += quote_for_sh(*p);
	}
	return r;
}

std::string
read_environ_file (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * procdir,
	const char * subdir,
	const char * name,
	FileDescriptorOwner & fd
) {
	const std::list<std::string> v(read_text0_file(prog, envs, procdir, subdir, name, fd));
	std::string r;
	for (std::list<std::string>::const_iterator b(v.begin()), e(v.end()), p(b); p != e; ++p) {
		if (p != b) r += ' ';
		r += quote_for_conf(*p);
	}
	return r;
}

std::string
GetField (
	FILE * f
) {
	std::string s;
	for (;;) {
		const int c(std::fgetc(f));
		if (std::feof(f)) break;
		if (std::isspace(c)) break;
		s += char(c);
	}
	return s;
}

bool
populate_record (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * subdir,
	const FileDescriptorOwner & proc_subdir_fd,
	const wanted & w,
	ProcessRecord & b
) {
	if (w.environ) {
		FileDescriptorOwner environ_fd(open_read_at(proc_subdir_fd.get(), "environ"));
		if (0 > environ_fd.get()) {
			if (EACCES == errno || EPERM == errno) {
				b["envs"] = "!su";
			} else if (ENOENT != errno)
				die_errno(prog, envs, procdir, subdir, "environ");
		} else
			b["envs"] = read_environ_file(prog, envs, procdir, subdir, "environ", environ_fd);
	}
	if (w.cmdline) {
		FileDescriptorOwner cmdline_fd(open_read_at(proc_subdir_fd.get(), "cmdline"));
		if (0 > cmdline_fd.get()) {
			if (EACCES == errno || EPERM == errno) {
				b["args"] = "!su";
			} else if (ENOENT != errno)
				die_errno(prog, envs, procdir, subdir, "cmdline");
		} else
			b["args"] = read_cmdline_file(prog, envs, procdir, subdir, "cmdline", cmdline_fd);
	}
	if (w.stat) {
		FileDescriptorOwner stat_fd(open_read_at(proc_subdir_fd.get(), "stat"));
		if (0 > stat_fd.get()) {
	bad_stat_file:
			if (ENOENT == errno) return false;
			die_errno(prog, envs, procdir, subdir, "stat");
		}
		FileStar stat_file(fdopen(stat_fd.get(), "r"));
		if (!stat_file) goto bad_stat_file;
		stat_fd.release();
		b["stat_pid"] = GetField(stat_file);
		b["command"] = b["comm"] = GetField(stat_file); // including the brackets
		b["state"] = GetField(stat_file);
		b["ppid"] = GetField(stat_file);
		b["pgid"] = GetField(stat_file);
		b["sid"] = GetField(stat_file);
		b["tdev"] = GetField(stat_file); //tty
		b["tpgid"] = GetField(stat_file);
		b["flags"] = GetField(stat_file);
		b["minflt"] = GetField(stat_file);
		b["cminflt"] = GetField(stat_file);
		b["majflt"] = GetField(stat_file);
		b["cmajflt"] = GetField(stat_file);
		b["usertime"] = GetField(stat_file); //utime
		b["systime"] = GetField(stat_file); //stime
		b["cusertime"] = GetField(stat_file); //cutime
		b["csystime"] = GetField(stat_file); //cstime
		b["pri"] = GetField(stat_file); //priority
		b["nice"] = GetField(stat_file);
		GetField(stat_file);
		b["itrealvalue"] = GetField(stat_file);
		b["start"] = GetField(stat_file);
		b["vsz"] = GetField(stat_file); //vsize
		b["rss"] = GetField(stat_file);
		b["rlim"] = GetField(stat_file);
		b["startcode"] = GetField(stat_file);
		b["endcode"] = GetField(stat_file);
		b["startstack"] = GetField(stat_file);
		b["kstkesp"] = GetField(stat_file);
		b["kstkeip"] = GetField(stat_file);
		b["signal"] = GetField(stat_file);
		b["blocked"] = GetField(stat_file);
		b["sigignore"] = GetField(stat_file);
		b["sigcatch"] = GetField(stat_file);
		b["wchan"] = GetField(stat_file);
		b["nswap"] = GetField(stat_file);
		b["cnswap"] = GetField(stat_file);
		b["exitsignal"] = GetField(stat_file);
		b["processor"] = GetField(stat_file);
		b["rt_priority"] = GetField(stat_file);
		b["policy"] = GetField(stat_file);
	}
	if (w.statm) {
		FileDescriptorOwner statm_fd(open_read_at(proc_subdir_fd.get(), "statm"));
		if (0 > statm_fd.get()) {
	bad_statm_file:
			if (ENOENT == errno) return false;
			die_errno(prog, envs, procdir, subdir, "statm");
		}
		FileStar statm_file(fdopen(statm_fd.get(), "r"));
		if (!statm_file) goto bad_statm_file;
		statm_fd.release();
		GetField(statm_file);
		GetField(statm_file);
		GetField(statm_file);
		GetField(statm_file);
		GetField(statm_file);
		GetField(statm_file);
		GetField(statm_file);
	}
	if (w.status) {
		FileDescriptorOwner status_fd(open_read_at(proc_subdir_fd.get(), "status"));
		if (0 > status_fd.get()) {
	bad_status_file:
			if (ENOENT == errno) return false;
			die_errno(prog, envs, procdir, subdir, "status");
		}
		FileStar status_file(fdopen(status_fd.get(), "r"));
		if (!status_file) goto bad_status_file;
		status_fd.release();
		GetField(status_file);
		GetField(status_file);
		GetField(status_file);
		GetField(status_file);	// pid
		GetField(status_file);	// pid?
		GetField(status_file);	// ttdev?
		GetField(status_file);	// flags for ctty and sldr
		GetField(status_file);	// stime as a timeval?
		GetField(status_file);	// ? as a timeval?
		GetField(status_file);	// ? as a timeval?
		GetField(status_file);
		GetField(status_file);
		GetField(status_file);
		GetField(status_file);
	}
	b["pid"] = subdir;
	return true;
}

ProcessTable
read_table (
	const char * prog,
	const ProcessEnvironment & envs,
	const FieldList & fields,
	bool threads,
	const char * /*time_format*/
) {
	const wanted w(CheckFields(fields));
	FileDescriptorOwner proc_dir_fd(open_dir_at(AT_FDCWD, procdir));
	const DirStar proc_dir(proc_dir_fd);
	if (!proc_dir) {
exit_scan:
		die_errno(prog, envs, procdir);
	}
	proc_dir_fd.release();
	ProcessTable r;
	for (;;) {
		errno = 0;
		const dirent * entry(readdir(proc_dir));
		if (!entry) {
			if (errno) goto exit_scan;
			break;
		}
#if defined(_DIRENT_HAVE_D_NAMLEN)
		if (1 > entry->d_namlen) continue;
#endif
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_DIR != entry->d_type) continue;
#endif
		if (!is_numeric(entry->d_name)) continue;
		const FileDescriptorOwner proc_subdir_fd(open_dir_at(proc_dir.fd(), entry->d_name));
		if (0 > proc_subdir_fd.get()) {
bad_subdir:
			if (ENOTDIR == errno) continue;
			die_errno(prog, envs, procdir, entry->d_name);
		}
		struct stat s;
		if (0 > fstat(proc_subdir_fd.get(), &s)) goto bad_subdir;
		if (!S_ISDIR(s.st_mode))
			continue;
		r.push_back(ProcessRecord());
		if (!populate_record(prog, envs, entry->d_name, proc_subdir_fd, w, r.back())) {
			r.pop_back();
			continue;
		}
	}
	return r;
}

#endif

}

/* Main function ************************************************************
// **************************************************************************
*/

void
list_process_table [[gnu::noreturn]]  (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	std::vector<const char *> next_args;
	FieldList fields;
	bool threads(false), non_unicode(false);
	const char * time_format("%F %T %z");
	try {
		popt::bool_definition threads_option('H', "threads", "List threads of processes.", threads);
		popt::string_list_definition field_option('F', "field", "field", "Include this field.", fields);
		popt::string_definition time_format_option('\0', "time-format", "format-string", "Use an alternative time display format.", time_format);
		popt::tui_level_definition tui_level_option('T', "tui-level", "Specify the level of TUI character set.");
		popt::definition * top_table[] = {
			&field_option,
			&threads_option,
			&tui_level_option,
			&time_format_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "PIDs");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		non_unicode = tui_level_option.value() >= 2;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	ProcessTable t(read_table(prog, envs, fields, threads, time_format));

	// Print the headings.
	for (FieldList::const_iterator b(fields.begin()), e(fields.end()), i(b); i != e; ++i) {
		if (i != b) std::cout.put('\t');
		std::cout << display_name_for(*i);
	}
	std::cout.put('\n');

	// Sort the table into tree form and construct the tree.
	struct TableIndexEntry {
		TableIndexEntry(ProcessRecord & e) : record(e) {}
		ProcessRecord & record;
	} ;
	struct SortIndexEntry : public TableIndexEntry {
		SortIndexEntry(ProcessRecord & r, pid_t p, pid_t pp) : TableIndexEntry(r), id(p), parent(pp) {}
		pid_t id, parent;
		std::string tree;
	} ;
	typedef std::list<SortIndexEntry> SortingIndex;
	typedef std::list<TableIndexEntry> TableIndex;
	SortingIndex pending, presorted;
	TableIndex sorted;
	for (ProcessTable::iterator te(t.end()), p(t.begin()); p != te; ++p) {
		ProcessRecord & r(*p);
		const ProcessRecord::const_iterator pid_p(p->find("pid"));
		const ProcessRecord::const_iterator ppid_p(p->find("ppid"));
		if (pid_p == p->end() || ppid_p == p->end() || "" == pid_p->second || "" == ppid_p->second) {
			sorted.push_back(r);	// should never happen; eliminates need for later sanity checking
			sorted.back().record["tree"] = "!!";
		} else {
			SortIndexEntry e(r, val(pid_p->second), val(ppid_p->second));
			if (
#if defined(__FreeBSD__) || defined(__DragonFly__)
			0 == e.id
#elif defined(__LINUX__) || defined(__linux__)
			2 == e.id || 1 == e.id
#else
			1 == e.id
#endif
			) {
				e.tree = "*-";
				pending.push_back(e);
			} else {
				bool done(false);
				for (SortingIndex::const_iterator pe(presorted.end()), pp(presorted.begin()); pp != pe; ++pp) {
					const SortIndexEntry & o(*pp);
					if (o.id > e.id) {
						presorted.insert(pp, e);
						done = true;
						break;
					}
				}
				if (!done)
					presorted.push_back(e);
			}
		}
	}
	while (!pending.empty()) {
		// Take the first item off the pending lists and append it to the sorted list.
		SortIndexEntry r(pending.front());
		pending.pop_front();
		sorted.push_back(r);
		// Find all of the immediate children of that item and prepend them to the pending list, so that they are processed next.
		SortingIndex::iterator insertion_point(pending.begin()), sentinel(pending.end()), last(sentinel), first(sentinel);
		for (SortingIndex::iterator e(presorted.end()), p(presorted.begin()); p != e; ) {
			SortIndexEntry & o(*p);
			if (o.parent == r.id) {
				std::string parent_tree(r.tree.substr(0, r.tree.length() - 1) + " ");
				char & finalc(parent_tree[parent_tree.length() - 2]);
				if ('-' == finalc || '`' == finalc || '*' == finalc)
					finalc = ' ';
				else if ('+' == finalc)
					finalc = '|';
				o.tree = parent_tree + "|-";
				insertion_point = pending.insert(insertion_point, o);
				if (first == sentinel) first = insertion_point;
				last = insertion_point;
				++insertion_point;
				p = presorted.erase(p);
			} else
				++p;
		}
		r.tree += "- ";
		if (first != sentinel) {
			// If we appended some children, make appropriate modifications to the trees in the parent and first and last child.
			r.tree[r.tree.length() - 2] = '.';
			if (first != last)
				first->tree[first->tree.length() - 2] = '+';
			last->tree[last->tree.length() - 2] = '`';
		}
		// Transfer the tree of the item just appended to the sorted list.
		sorted.back().record["tree"] = non_unicode ? r.tree : make_graphical_tree(r.tree);
	}

	// Filter the processes that are wanted.
	if (!args.empty()) {
		for (TableIndex::const_iterator te(sorted.end()), tb(sorted.begin()), tp(tb); tp != te; ) {
			const ProcessRecord & r(tp->record);
			const ProcessRecord::const_iterator pid(r.find("pid"));
			if (pid == r.end()) {
				std::fprintf(stderr, "%s: ERROR: No PID field to match %zu process IDs.\n", prog, args.size());
				tp = sorted.erase(tp);
				continue;
			}
			bool wanted(false);
			for (std::vector<const char *>::const_iterator ap(args.begin()), ae(args.end()); ap != ae; ++ap) {
				if (pid->second == *ap) {
					wanted = true;
					break;
				}
			}
			if (wanted)
				++tp;
			else
				tp = sorted.erase(tp);
		}
	}

	// Print the table rows.
	for (TableIndex::const_iterator te(sorted.end()), tb(sorted.begin()), p(tb); p != te; ++p) {
		const ProcessRecord & r(p->record);
		for (FieldList::const_iterator fb(fields.begin()), fe(fields.end()), f(fb); f != fe; ++f) {
			if (f != fb) std::cout.put('\t');
			const ProcessRecord::const_iterator v(r.find(*f));
			if (v != r.end()) std::cout << VisEncoder::process(v->second);
		}
		std::cout.put('\n');
	}

	throw EXIT_SUCCESS;
}
