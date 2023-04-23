using namespace std;

#include "buffer.cc"
#include "buffering.cc"
#include "canonicalize.cc"
#include "color.cc"
#include "concat_executor.cc"
#include "debug.cc"
#include "dep.cc"
#include "dynamic_executor.cc"
#include "error.cc"
#include "executor.cc"
#include "explain.cc"
#include "file_executor.cc"
#include "flags.cc"
#include "format.cc"
#include "job.cc"
#include "main_loop.cc"
#include "options.cc"
#include "parser.cc"
#include "root_executor.cc"
#include "rule.cc"
#include "target.cc"
#include "text.cc"
#include "timestamp.cc"
#include "token.cc"
#include "tokenizer.cc"
#include "transient_executor.cc"

int main(int argc, char **argv, char **envp)
{
	dollar_zero= argv[0];
	envp_global= (const char **) envp;
	init_buffering();
	Job::init_tty();
	Color::set();
	int error= 0;

	/* Refuse to run when $STU_STATUS is set */
	const char *const stu_status= getenv("STU_STATUS");
	if (stu_status != nullptr) {
		print_error(frmt("Refusing to run recursive Stu; "
				 "unset %s$STU_STATUS%s to circumvent",
				 Color::word, Color::end));
		exit(ERROR_FATAL);
	}

	try {
		vector <string> filenames;
		/* Filenames passed using the -f option.  Entries are
		 * unique and sorted as they were given, except for
		 * duplicates. */

		vector <shared_ptr <const Dep> > deps;
		/* Assemble targets here */

		shared_ptr <const Place_Param_Target> target_first;
		/* Set to the first rule when there is one */

		Place place_first;
		/* Place of first file when no rule is contained */

		bool had_option_target= false;
		/* Whether any target(s) was passed through one of the
		 * options -c, -C, -o, -p, -n, -0.  Also set when zero
		 * targets are passed through one of these, e.g., when
		 * -n is used on an empty file.  */

		bool had_option_f= false; /* Both lower and upper case */

		/* Parse $STU_OPTIONS */
		const char *stu_options= getenv("STU_OPTIONS");
		if (stu_options != nullptr) {
			while (*stu_options) {
				char c= *stu_options++;
				if (c == '-' || isspace(c))
					continue;
				if (! option_setting(c)) {
					Place place(Place::Type::ENV_OPTIONS);
					place << fmt("invalid option %s",
						     multichar_format_err(frmt("-%c", c)));
					exit(ERROR_FATAL);
				}
			}
		}

		for (int c; (c= getopt(argc, argv, OPTIONS)) != -1;) {
			if (option_setting(c))
				continue;

			switch (c) {
			case 'a':  option_a= true;         break;
			case 'd':  option_d= true;         break;
			case 'g':  option_g= true;         break;
			case 'h':  fputs(HELP, stdout);    exit(0);
			case 'i':  set_option_i();         break;
			case 'j':  set_option_j(optarg);   break;
			case 'J':  option_J= true;         break;
			case 'k':  option_k= true;         break;
			case 'K':  option_K= true;         break;
			case 'm':  set_option_m(optarg);   break;
			case 'M':  set_option_M(optarg);   break;
			case 'P':  option_P= true;         break;
			case 'q':  option_q= true;         break;
			case 'V':  set_option_V();  	   exit(0);

			case 'c':  {
				had_option_target= true;
				Place place(Place::Type::OPTION, 'c');
				if (*optarg == '\0') {
					place << "expected a non-empty argument";
					exit(ERROR_FATAL);
				}
				deps.push_back
					(make_shared <Plain_Dep>
					 (0, Place_Param_Target
					  (0, Place_Name(optarg, place))));
				break;
			}

			case 'C':  {
				had_option_target= true;
				Parser::add_deps_option_C(deps, optarg);
				break;
			}

			case 'f':
				if (*optarg == '\0') {
					Place(Place::Type::OPTION, 'f') <<
						"expected non-empty argument";
					exit(ERROR_FATAL);
				}

				for (string &filename:  filenames) {
					/* Silently ignore duplicate input file on command line */
					if (filename == optarg)  goto end;
				}
				had_option_f= true;
				filenames.push_back(optarg);
				Parser::get_file(optarg, -1, Executor::rule_set,
						 target_first, place_first);
			end:
				break;

			case 'F':
				had_option_f= true;
				Parser::get_string(optarg, Executor::rule_set,
						   target_first);
				break;

			case 'n':
			case '0':  {
				had_option_target= true;
				Place place(Place::Type::OPTION, c);
				if (*optarg == '\0') {
					place << "expected a non-empty argument";
					exit(ERROR_FATAL);
				}
				deps.push_back
					(make_shared <Dynamic_Dep>
					 (0, make_shared <Plain_Dep>
					  (1 << flag_get_index(c),
					   Place_Param_Target
					   (0, Place_Name(optarg, place)))));
				break;
			}

			case 'o':
			case 'p':  {
				had_option_target= true;
				Place place(Place::Type::OPTION, c);
				if (*optarg == '\0') {
					place << "expected a non-empty argument";
					exit(ERROR_FATAL);
				}
				Place places[C_PLACED];
				places[c == 'p' ? I_PERSISTENT : I_OPTIONAL]= place;
				deps.push_back
					(make_shared <Plain_Dep>
					 (c == 'p' ? F_PERSISTENT : F_OPTIONAL, places,
					  Place_Param_Target(0, Place_Name(optarg, place))));
				break;
			}

			default:
				/* Invalid option -- an error message was
				 * already printed by getopt() */
				string text= name_format_err(frmt("%s -h", dollar_zero));
				fprintf(stderr,
					"To get a list of all options, use %s\n",
					text.c_str());
				exit(ERROR_FATAL);
			}
		}

		order_vec= (order == Order::RANDOM);

		if (option_i && option_parallel) {
			Place(Place::Type::OPTION, 'i')
				<< fmt("parallel mode using %s cannot be used in interactive mode",
				       multichar_format_err("-j"));
			exit(ERROR_FATAL);
		}

		/* Targets passed on the command line, outside of options */
		for (int i= optind;  i < argc;  ++i) {
			/* The number I may not be the index that the
			 * argument had originally, because getopt() may
			 * reorder its arguments. (I.e., using GNU
			 * getopt.)  This is why we can't put I into the
			 * trace.  */
			Place place(Place::Type::ARGUMENT);
			if (*argv[i] == '\0') {
				place << fmt("%s: name must not be empty",
					     name_format_err(argv[i]));
				if (! option_k)
					throw ERROR_LOGICAL;
				error |= ERROR_LOGICAL;
			} else if (option_J)
				deps.push_back(make_shared <Plain_Dep>
					       (0, Place_Param_Target
						(0, Place_Name(argv[i], place))));
		}

		if (! option_J)
			Parser::get_target_arg(deps, argc - optind, argv + optind);

		/* Use the default Stu script if -f/-F are not used */
		if (! had_option_f) {
			filenames.push_back(FILENAME_INPUT_DEFAULT);
			int file_fd= open(FILENAME_INPUT_DEFAULT, O_RDONLY);
			if (file_fd >= 0) {
				Parser::get_file("", file_fd,
						 Executor::rule_set, target_first,
						 place_first);
			} else {
				if (errno == ENOENT) {
					/* The default file does not exist --
					 * fail if no target is given */
					if (deps.empty() && ! had_option_target
					    && ! option_P) {
						print_error(fmt("Expected a target or the default file %s",
								name_format_err(FILENAME_INPUT_DEFAULT)));

						explain_no_target();
						throw ERROR_LOGICAL;
					}
				} else {
					/* Other errors by open() are fatal */
					print_error_system(FILENAME_INPUT_DEFAULT);
					exit(ERROR_FATAL);
				}
			}
		}

		if (option_P) {
			Executor::rule_set.print();
			exit(0);
		}

		/* If no targets are given on the command line,
		 * use the first non-variable target */
		if (deps.empty() && ! had_option_target) {
			if (target_first == nullptr) {
				if (! place_first.empty()) {
					place_first
						<< "expected a rule, because no target is given";
				} else {
					print_error("No rules and no targets given");
				}
				exit(ERROR_FATAL);
			}
			if (target_first->place_name.is_parametrized()) {
				target_first->place <<
					fmt("the first target %s must not be parametrized if no target is given",
					    target_first->format_err());
				exit(ERROR_FATAL);
			}
			deps.push_back(make_shared <Plain_Dep> (*target_first));
		}

		main_loop(deps);
	} catch (int e) {
		assert(e >= 1 && e <= 3);
		error= e;
	}

	/*
	 * Code executed before exiting:  This must be executed even if
	 * Stu fails (but not for fatal errors).
	 */

	if (option_z)
		Job::print_statistics();

	if (fclose(stdout)) {
		perror("fclose(stdout)");
		exit(ERROR_FATAL);
	}
	/* No need to flush stderr, because it is line buffered, and if
	 * we used it, it means there was an error anyway, so we're not
	 * losing any information  */
	exit(error);
}
