/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_POPT_H)
#define INCLUDE_POPT_H

#include <vector>
#include <list>
#include <string>
#include <cstdio>

namespace popt {
	struct error {
		error(const char * a, const char * m) : arg(a), msg(m) {}
		const char * arg, * msg;
	};
	class processor;
	struct definition {
	public:
		definition() {}
		virtual bool execute(processor &, char c) = 0;
		virtual bool execute(processor &, char c, const char * s) = 0;
		virtual bool execute(processor &, const char * s) = 0;
		virtual ~definition() = 0;
	};
	class processor {
	public:
		processor(const char * n, definition & d, std::vector<const char *> & f);
		virtual ~processor() {}
		virtual const char * next_arg() = 0;
		void process(bool strictly_options_before_arguments, bool single_dash_long_options = false) {
			while (const char * arg = next_arg()) {
				if (is_stopped) break;
				if (0 == slash && ('-' == arg[0]
#if defined(__OS2__) || defined(__WIN32__) || defined(__NT__)
						|| '/' == arg[0]	// If the compiler is hosted on these platforms, this can also be the option character.
#endif
				))
					slash = arg[0];
				if (!slash || slash != arg[0]) {
					file_vector.push_back(arg);
					if (strictly_options_before_arguments)
						slash = EOF;
				} else if (arg[1] == slash) {
					if (!arg[2])
						slash = EOF;
					else if (!def.execute(*this, arg + 2))
						throw error(arg, "unrecognized option");
				} else if (!single_dash_long_options || !def.execute(*this, arg + 1)) {
					for ( const char * p(arg + 1); *p; ++p ) {
						if (def.execute(*this, *p, p + 1))
							break;
						if (!def.execute(*this, *p))
							throw error(arg, "unrecognized option(s)");
					}
				}
			}
		}
		bool stopped() const { return is_stopped; }
		void stop() { is_stopped = true; }
		std::vector<const char *> & file_vector;
		const char * name;
	protected:
		int slash;
		definition & def;
		bool is_stopped;
	};
	template <class InputIterator> class arg_processor : public processor {
	public:
		arg_processor(InputIterator b, InputIterator e, const char * n, definition & d, std::vector<const char *> & f) : processor(n, d, f), current(b), end(e) {}
		const char * next_arg() 
		{
			if (current >= end) return 0;
			return *current++;
		}
	protected:
		InputIterator current, end;
	};
	struct table_definition : public definition {
	public:
		table_definition(unsigned c, definition * const * v, const char * d) : definition(), count(c), array(v), description(d) {}
		virtual void long_usage();
		virtual void help();
		virtual ~table_definition();
	protected:
		virtual bool execute(processor &, char c);
		virtual bool execute(processor &, char c, const char * s);
		virtual bool execute(processor &, const char * s);
		void gather_combining_shorts(std::string &);
		unsigned count;
		definition *  const *array;
		const char * description;
	};
	struct top_table_definition : public table_definition {
	public:
		top_table_definition(unsigned c, definition * const * v, const char * d, const char * a) : table_definition(c, v, d), arguments_description(a) {}
		virtual ~top_table_definition();
	protected:
		void do_usage(processor &);
		void do_help(processor &);
		const char * arguments_description;
		using table_definition::execute;
		virtual bool execute(processor &, char c);
		virtual bool execute(processor &, const char * s);
	};
	struct named_definition : public definition {
	public:
		named_definition(char s, const char * l, const char * a, const char * d) : short_name(s), long_name(l), optarg_description(a), description(d) {}
		const char * query_args_description() const { return optarg_description; }
		const char * query_description() const { return description; }
		char query_short_name() const { return short_name; }
		const char * query_long_name() const { return long_name; }
		virtual ~named_definition() = 0;
	protected:
		char short_name;
		const char * long_name;
		const char * optarg_description;
		const char * description;
	};
	struct simple_named_definition : public named_definition {
	public:
		simple_named_definition(char s, const char * l, const char * d) : named_definition(s, l, 0, d) {}
		virtual ~simple_named_definition() = 0;
	protected:
		virtual void action(processor &) = 0;
		virtual bool execute(processor & proc, char c);
		virtual bool execute(processor &, char c, const char * s);
		virtual bool execute(processor & proc, const char * s);
	};
	struct compound_named_definition : public named_definition {
	public:
		compound_named_definition(char s, const char * l, const char * a, const char * d) : named_definition(s, l, a, d) {}
		virtual ~compound_named_definition() = 0;
	protected:
		virtual void action(processor &, const char *) = 0;
		virtual bool execute(processor & proc, char c);
		virtual bool execute(processor &, char c, const char * s);
		virtual bool execute(processor & proc, const char * s);
	};
	struct string_definition : public compound_named_definition {
	public:
		string_definition(char s, const char * l, const char * a, const char * d, const char * & v) : compound_named_definition(s, l, a, d), value(v) {}
		virtual ~string_definition();
	protected:
		virtual void action(processor &, const char *);
		const char * & value;
	};
	struct string_list_definition : public compound_named_definition {
	public:
		typedef std::list<std::string> list_type;
		string_list_definition(char s, const char * l, const char * a, const char * d, list_type & v) : compound_named_definition(s, l, a, d), value_list(v) {}
		virtual ~string_list_definition();
	protected:
		virtual void action(processor &, const char *);
		list_type & value_list;
	};
	struct bool_definition : public simple_named_definition {
	public:
		bool_definition(char s, const char * l, const char * d, bool & v) : simple_named_definition(s, l, d), value(v) {}
		virtual ~bool_definition();
	protected:
		virtual void action(processor &);
		bool & value;
	};
	struct integral_definition : public compound_named_definition {
	public:
		integral_definition(char s, const char * l, const char * a, const char * d) : compound_named_definition(s, l, a, d), set(false) {}
		virtual ~integral_definition() = 0;
		bool is_set() const { return set; }
	protected:
		bool set;
	};
	struct bool_string_definition : public integral_definition {
	public:
		bool_string_definition(char s, const char * l, const char * d, bool & v) : integral_definition(s, l, a, d), value(v) {}
		virtual ~bool_string_definition();
	protected:
                static const char a[];
		virtual void action(processor &, const char *);
		bool & value;
	};
	struct number_definition : public integral_definition {
	public:
		number_definition(char s, const char * l, const char * a, const char * d, int b) : integral_definition(s, l, a, d), base(b) {}
	protected:
		virtual void action(processor &, const char *) = 0;
		int base;
	};
	struct unsigned_number_definition : public number_definition {
	public:
		unsigned_number_definition(char s, const char * l, const char * a, const char * d, unsigned long & v, int b) : number_definition(s, l, a, d, b), value(v) {}
		virtual ~unsigned_number_definition();
	protected:
		virtual void action(processor &, const char *);
		unsigned long & value;
	};
	struct signed_number_definition : public number_definition {
	public:
		signed_number_definition(char s, const char * l, const char * a, const char * d, signed long & v, int b) : number_definition(s, l, a, d, b), value(v) {}
		virtual ~signed_number_definition();
	protected:
		virtual void action(processor &, const char *);
		signed long & value;
	};
	struct compound_2arg_named_definition : public named_definition {
	public:
		compound_2arg_named_definition(char s, const char * l, const char * a, const char * d) : named_definition(s, l, a, d) {}
		virtual ~compound_2arg_named_definition() = 0;
	protected:
		virtual void action(processor &, const char *, const char *) = 0;
		virtual bool execute(processor & proc, char c);
		virtual bool execute(processor &, char c, const char * s);
		virtual bool execute(processor & proc, const char * s);
	};
	struct string_pair_definition : public compound_2arg_named_definition {
	public:
		string_pair_definition(char s, const char * l, const char * a, const char * d, const char * & v1, const char * & v2) : compound_2arg_named_definition(s, l, a, d), value1(v1), value2(v2) {}
		virtual ~string_pair_definition();
	protected:
		virtual void action(processor &, const char *, const char *);
		const char * & value1, * & value2;
	};
	struct string_pair_list_definition : public compound_2arg_named_definition {
	public:
		typedef std::pair<std::string,std::string> pair_type;
		typedef std::list<pair_type> list_type;
		string_pair_list_definition(char s, const char * l, const char * a, const char * d, list_type & v) : compound_2arg_named_definition(s, l, a, d), value_list(v) {}
		virtual ~string_pair_list_definition();
	protected:
		virtual void action(processor &, const char *, const char *);
		list_type & value_list;
	};
}

#endif
