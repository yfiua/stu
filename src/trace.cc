#include "trace.hh"

#ifndef NDEBUG

const char *const trace_names[]= {
	"EXECUTOR", "SHOW", "TOKENIZER", "DEP"
};
static_assert(sizeof(trace_names) / sizeof(trace_names[0]) == TRACE_COUNT,
	      "sizeof(trace_names)");

FILE *Trace::trace_files[TRACE_COUNT];
string Trace::padding;
Trace::Init Trace::init;
std::vector <Trace *> Trace::stack;

Trace::Trace(Trace_Class trace_class_, const char *name, const char *file, int line)
	: trace_class(trace_class_)
{
	assert(trace_class >= 0 && trace_class < TRACE_COUNT);
	stack.push_back(this);
	if (! (trace_files[trace_class]))
		return;
	string text= fmt("%s%s()", padding, name);
	if (fprintf(Trace::trace_files[trace_class],
			print_format,				  
			strip_dir(file), line,
			text.c_str()) == EOF) {      
		perror("fprintf(trace_file)"); 
		exit(ERROR_FATAL);		
	}	
	padding += padding_one;
	prefix= padding;
}

Trace::~Trace()
{
	stack.pop_back();
	if (! (trace_files[trace_class]))
		return;
	padding.resize(padding.size() - strlen(padding_one));
}

FILE *Trace::open_logfile(const char *filename)
{
	FILE *ret= fopen(filename, "w");
	if (!ret) {
		print_errno(fmt("fopen(%s)", filename));
		exit(ERROR_FATAL);
	}
	int flags= fcntl(fileno(ret), F_GETFL, 0);
	if (flags >= 0)
		fcntl(fileno(ret), F_SETFL, flags | O_APPEND | FD_CLOEXEC);
	assert(ret);
	return ret;
}

Trace::Init::Init()
{
	FILE *file_log= nullptr;
	FILE *file_devnull= nullptr;

	for (int i= 0; i < TRACE_COUNT; ++i) {
		string name= fmt("STU_TRACE_%s", trace_names[i]);
		const char *env= getenv(name.c_str());
		bool enabled= (env && env[0]);
		if (!enabled) {
			if (!file_devnull)
				file_devnull= open_logfile("/dev/null");
			trace_files[i]= file_devnull;
		} else if (!strcmp(env, "log")) {
			if (!file_log)
				file_log= open_logfile(trace_filename);
			trace_files[i]= file_log;
		} else if (!strcmp(env, "stderr")) {
			trace_files[i]= stderr;
		} else if (!strcmp(env, "off")) {
			trace_files[i]= nullptr;
		} else {
			fprintf(stderr, "stu: error: invalid value for trace %s=%s\n",
				name.c_str(), env);
			exit(ERROR_FATAL);
		}
	}
}

const char *Trace::strip_dir(const char *s)
{
	const char *r= strchr(s, '/');
	if (!r) return s;
	return r+1;
}

#endif /* ! NDEBUG */
