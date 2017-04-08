#include <openssl/md5.h>
#include <iostream>
#include <fstream>
#include <ctime>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

static inline
void
read_file (
	const char * prog,
	char * buf,
	std::size_t len,
	const char * name
) {
	std::ifstream f(name);
	if (!f) {
		const int error(errno);
		std::cerr << prog << ": " << name << ": " << std::strerror(error) << "\n";
		throw EXIT_FAILURE;
	}
	f.getline(buf, len);
	if (!f) {
		std::cerr << prog << ": " << name << ": Error reading 1st line\n";
		throw EXIT_FAILURE;
	}
}

static inline
void
calculate_password (
	const char * prog,
	unsigned char m[MD5_DIGEST_LENGTH]
) {
	const std::time_t t(std::time(0));
	// The mOTP algorithm is either mad or broken.
	struct tm tm;
	localtime_r(&t, &tm);
	const unsigned long long n(t + tm.tm_gmtoff);
	const unsigned long long n10(n / 10U);
	enum {
		EPOCH_LEN = 9U,
		SERVER_SECRET_LEN = 32U,
		USER_SECRET_LEN = 4U
	};
	char buf[EPOCH_LEN + SERVER_SECRET_LEN + USER_SECRET_LEN + 1];
	std::memset(buf, '\0', sizeof buf);
	std::snprintf(buf, EPOCH_LEN + 1, "%.*llu", EPOCH_LEN, n10);
	read_file(prog, buf + EPOCH_LEN, SERVER_SECRET_LEN + 1, "server_key");
	read_file(prog, buf + EPOCH_LEN + SERVER_SECRET_LEN, USER_SECRET_LEN + 1, "user_key");
	MD5(reinterpret_cast<const unsigned char *>(buf), sizeof buf - 1, m);
	std::memset(buf, '\0', sizeof buf);
}

static inline
void
write_user_and_password (
	const char * prog,
	const char u[1024],
	const unsigned char p[MD5_DIGEST_LENGTH]
) {
	{
		std::ofstream f("secret.up{new}");
		f << u;
		f.put('\n');
		for (unsigned i(0U); i < 3U; ++i) {
			char d[3];
			const int n(std::snprintf(d, sizeof d, "%02.2x", p[i]));
			f.write(d, n);
		}
		f.put('\n');
	}
	if (0 > rename("secret.up{new}", "secret.up")) {
		const int error(errno);
		std::cerr << prog << ": secret.up: " << std::strerror(error) << "\n";
		throw EXIT_FAILURE;
	}
}

int
main (
	int argc, 
	const char ** argv
) {
	if (1 != argc) {
		std::cerr << "Usage: " << argv[0] << "\n";
		return EXIT_FAILURE;
	}
	try {
		for (;;) {
			char u[1024];
			read_file(argv[0], u, sizeof u, "user_name");
			unsigned char m[MD5_DIGEST_LENGTH] = "";
			calculate_password(argv[0], m);
			write_user_and_password(argv[0], u, m);
			sleep(10);
		}
	} catch (const int i) {
		return i;
	}
	return 0;
}
