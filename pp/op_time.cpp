/* COPYRIGHT (C) 2001 Philippe Elie
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * first written by P.Elie, many cleanup by John Levon
 */

#include <popt.h>
#include <stdlib.h>
#include <stdio.h>

#include <string>
#include <list>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <map>
#include <vector>

#include "oprofpp.h"
#include "opf_filter.h"

#include "../util/file_manip.h"
#include "../util/string_manip.h"
#include "../util/op_popt.h"

using std::string;
using std::list;
using std::cerr;
using std::cout;
using std::endl;
using std::ostringstream;
using std::vector;
using std::ifstream;
using std::multimap;
using std::pair;
using std::setw;

/* TODO: if we have a quick read samples files format we can handle a great
 * part of complexity here by using samples_files_t to handle straight
 * op_time. Just create an artificial symbol that cover the whole samples
 * files with the name of the application this allow to remove image_name
 * and sorted_map_t class and all related  stuff and to use OutputSymbol to
 * make the report
 */

/// image_name - class to store name for a samples file
struct image_name
{
	image_name(const string& samplefile_name);

	/// total number of samples for this samples file, this is a place
	/// holder to avoid separate data struct which associate image_name
	/// with a sample count.
	counter_array_t count;
	/// complete name of the samples file (without leading dir)
	string samplefile_name;
	/// application name which belong this sample file, == name if the
	/// file belong to an application or if the user have obtained
	/// this file without --separate-samples
	string app_name;
	/// image name which belong this sample file, empty if the file belong
	/// to an application or if the user have obtained this file without
	/// --separate-samples
	string lib_name;
};

typedef multimap<string, image_name> map_t;
typedef pair<map_t::iterator, map_t::iterator> pair_it_t;

/// comparator for sorted_map_t
struct sort_by_counter_t {
	sort_by_counter_t(size_t index_) : index(index_) {}

	bool operator()(const counter_array_t & lhs,
			const counter_array_t & rhs) const {
		return lhs[index] < rhs[index];
	}

	size_t index;
};


typedef multimap<counter_array_t, map_t::const_iterator, sort_by_counter_t>
  sorted_map_t;

static const char* counter_str;
static int counter;
static int sort_by_counter = -1;
static int showvers;
static int reverse_sort;
static int show_shared_libs;
static int list_symbols;
static int show_image_name;
static char * output_format;
static char * samples_dir;
static const char * session;
static const char * path;
static const char * recursive_path;

static OutSymbFlag output_format_flags;

static struct poptOption options[] = {
	{ "session", 's', POPT_ARG_STRING, &session, 0, "session to use", "name", }, 
	{ "counter", 'c', POPT_ARG_STRING, &counter_str, 0,
	  "which counter to use", "counter nr,[counter nr]", },
	{ "sort", 'C', POPT_ARG_INT, &sort_by_counter, 0, "which counter to use for sampels sort", "counter nr", }, 
	{ "show-shared-libs", 'k', POPT_ARG_NONE, &show_shared_libs, 0,
	  "show details for shared libs. Only meaningfull if you have profiled with --separate-samples", NULL, },
	{ "demangle", 'd', POPT_ARG_NONE, &demangle, 0, "demangle GNU C++ symbol names", NULL, },
	{ "show-image-name", 'n', POPT_ARG_NONE, &show_image_name, 0, "show the image name from where come symbols", NULL, },
	{ "list-symbols", 'l', POPT_ARG_NONE, &list_symbols, 0, "list samples by symbol", NULL, },
	{ "reverse", 'r', POPT_ARG_NONE, &reverse_sort, 0,
	  "reverse sort order", NULL, },
	// FIXME: clarify this
	{ "output-format", 't', POPT_ARG_STRING, &output_format, 0,
	  "choose the output format", "output-format strings", },
	{ "path", 'p', POPT_ARG_STRING, &path, 0,
	  "add path for retrieving image", "path_name[,path_name]", },
	{ "recursive-path", 'P', POPT_ARG_STRING, &recursive_path, 0,
	  "add path for retrieving image recursively", "path_name[,path_name]", },
	{ "output-format", 't', POPT_ARG_STRING, &output_format, 0,
	  "choose the output format", "output-format strings", },
	{ "version", 'v', POPT_ARG_NONE, &showvers, 0, "show version", NULL, },
	POPT_AUTOHELP
	{ NULL, 0, 0, NULL, 0, NULL, NULL, },
};

/// associate filename with directory name where filename exist. Filled
/// through the -p/-P option to allow retrieving of image name when samples
/// file name contains an incorrect location for the image such ram disk
/// module at boot time. We need a multimap to warn against ambiguity between
/// mutiple time found image name.
typedef multimap<string, string> alt_filename_t;
static alt_filename_t alternate_filename;

/**
 * add_to_alternate_filename -
 * add all file name below path_name, optionnaly recursively, to the
 * the set of alternative filename used to retrieve image name when
 * a samples image name directory is not accurate
 */
void add_to_alternate_filename(const string & path_name, bool recursive)
{
	vector<string> path_names;

	separate_token(path_names, path_name, ',');

	vector<string>::iterator path;
	for (path = path_names.begin() ; path != path_names.end() ; ++path) {
		list<string> file_list;
		create_file_list(file_list, path_name, "*", recursive);
		list<string>::const_iterator it;
		for (it = file_list.begin() ; it != file_list.end() ; ++it) {
			typedef alt_filename_t::value_type value_t;
			if (recursive) {
				value_t value(basename(*it), dirname(*it));
				alternate_filename.insert(value);
			} else {
				value_t value(*it, *path);
				alternate_filename.insert(value);
			}
		}
	}
}

/**
 * handle_session_options - derive samples directory
 */
static void handle_session_options(void)
{
/*
 * This should eventually be shared amongst all programs
 * to take session names.
 */
	if (!session) {
		samples_dir = OP_SAMPLES_DIR;
		return;
	}

	if (session[0] == '/') {
		samples_dir = (char*)session;
		return;
	}

	samples_dir = (char*)xmalloc(strlen(OP_SAMPLES_DIR) + strlen(session) + 1);
	strcpy(samples_dir, OP_SAMPLES_DIR);
	strcat(samples_dir, session);
}

 
/**
 * get_options - process command line
 * \param argc program arg count
 * \param argv program arg array
 *
 * Process the arguments, fatally complaining on error.
 */
static void get_options(int argc, char const * argv[])
{
	poptContext optcon;
	char const * file;

	optcon = opd_poptGetContext(NULL, argc, argv, options, 0);

	if (showvers) {
		show_version(argv[0]);
	}

	// non-option file, must be a session name
	file = poptGetArg(optcon);
	if (file) {
		session = file;
	}

	handle_session_options();
 
	if (!counter_str)
		counter_str = "0";

	counter = counter_mask(counter_str);

	validate_counter(counter, sort_by_counter);

	if (output_format == 0) {
		output_format = "hvspni";
	} else {
		if (!list_symbols) {
			quit_error(optcon, "op_time: --output-format can be used only with --list-symbols.\n");
		}
	}

	if (list_symbols) {
		OutSymbFlag fl =
			OutputSymbol::ParseOutputOption(output_format);

		if (fl == osf_none) {
			cerr << "op_time: invalid --output-format flags.\n";
			OutputSymbol::ShowHelp();
			exit(EXIT_FAILURE);
		}

		output_format_flags = fl;
	}

	if (show_image_name)
		output_format_flags = static_cast<OutSymbFlag>(output_format_flags | osf_image_name);

	if (path) {
		add_to_alternate_filename(path, false);
	}

	if (recursive_path) {
		add_to_alternate_filename(recursive_path, true);
	}

	poptFreeContext(optcon);
}

/**
 * image_name - ctor from a sample file name
 */
image_name::image_name(const string& samplefile_name)
	:
	samplefile_name(samplefile_name)
{
	app_name = extract_app_name(samplefile_name, lib_name);
}

/**
 * samples_file_exist - test for a samples file existence
 * \param filename the base samples filename
 *
 * return true if filename exist
 */
static bool file_exist(const std::string & filename)
{
	ifstream in(filename.c_str());

	return in;
}

/**
 * sort_file_list_by_name - insert in result a file list sorted by app name
 * \param result where to put result
 * \param file_list a list of string which must be insert in result
 *
 * for each filename try to extract if possible the app name and
 * use it as a key to insert the filename in result
 *  filename are on the form
 *   }usr}sbin}syslogd}}}lib}libc-2.1.2.so (shared lib
 *     which belong to app /usr/sbin/syslogd)
 *  or
 *   }bin}bash (application)
 *
 * The sort order used is likely to get in result file name as:
 * app_name_A, all samples filenames from shared libs which belongs
 *  to app_name_A,
 * app_name_B, etc.
 *
 * This sort is used later to find samples filename which belong
 * to the same application. Note than the sort is correct even if
 * the input file list contains only app_name (i.e. samples file
 * obtained w/o --separate-samples) in this case shared libs are
 * treated as application
 */
static void sort_file_list_by_name(map_t & result,
				   const list<string> & file_list)
{
	list<string>::const_iterator it;
	for (it = file_list.begin() ; it != file_list.end() ; ++it) {
		// the file list is created with /base_dir/* pattern but with
		// the lazilly samples creation it is perfectly correct for one
		// samples file belonging to counter 0 exist and not exist for
		// counter 1, so we must filter them.
		int i;
		for (i = 0 ; i < OP_MAX_COUNTERS ; ++i) {
			if ((counter & (1 << i)) != 0) {
				std::ostringstream s;
				s << string(samples_dir) << "/" << *it 
				  << '#' << i;
				if (file_exist(s.str()) == true) {
					break;
				}
			}
		}

		if (i < OP_MAX_COUNTERS) {
			image_name image(*it);
			map_t::value_type value(image.app_name, image);
			result.insert(value);
		}
	}
}

/**
 * out_filename - display a filename and it associated ratio of samples
 */
static void out_filename(const string& app_name,
			 const counter_array_t & app_count,
			 const counter_array_t & count, 
			 double total_count[OP_MAX_COUNTERS])
{
	for (size_t i = 0 ; i < OP_MAX_COUNTERS; ++i) {
		if ((counter & (1 << i)) != 0) {
			// feel to rewrite with cout and its formated output
#if 1
			printf("%-9d ", count[i]);
			double ratio = total_count[i] >= 1.0 
				? count[i] / total_count[i] : 0.0;

			if (ratio < 10.00 / 100.0)
				printf(" ");
			printf("%2.4f", ratio * 100);

			ratio = app_count[i] >= 1.0
				? count[i] / app_count[i] : 0.0;

			if (ratio < 10.00 / 100.0)
				printf(" ");
			printf("%2.4f", ratio * 100);
#else
			cout << count[i] << " ";
			
			if (total_count[i] > 1) {
				double ratio = count[i] / total_count[i];
				cout << ratio * 100 << "%";
			} else {
				cout << "0%";
			}

			if (app_count[i] != 0) {
				double ratio = count[i] / double(app_count[i]);
				cout << " (" << ratio * 100 << "%)";
			}
#endif
			cout << " ";
		}
	}

	cout << demangle_filename(app_name);
	cout << endl;
}

/**
 * output_image_samples_count - output the samples ratio for some images
 * \param first the start iterator
 * \param last the end iterator
 * \param total_count the total samples count
 *
 * iterator parameters are intended to be of type map_t::iterator or
 * map_t::reverse_iterator
 */
template <class Iterator>
static void output_image_samples_count(Iterator first, Iterator last,
				       const counter_array_t & app_count,
				       double total_count[OP_MAX_COUNTERS])
{
	for (Iterator it = first ; it != last ; ++it) {
		string name = it->second->second.lib_name;
		if (name.length() == 0)
			name = it->second->second.samplefile_name;

		cout << "  ";
		out_filename(name, app_count, it->first, total_count);
	}
}

/**
 * build_sorted_map_by_count - insert element in a sorted_map_t
 * \param first the start iterator
 * \param last the end iterator
 * \param total_count the total samples count
 *
 */
static void build_sorted_map_by_count(sorted_map_t & sorted_map, pair_it_t p_it)
{
	map_t::iterator it;
	for (it = p_it.first ; it != p_it.second ; ++it) {
		sorted_map_t::value_type value(it->second.count, it);

		sorted_map.insert(value);
	}
}

/**
 * output_files_count - open each samples file to cumulate samples count
 * and display a sorted list of filename and samples ratio
 * \param files the file list to treat.
 *
 * print the whole cumulated count for all selected filename (currently
 * the whole base_dir directory), does not show any details about symbols
 */
static void output_files_count(map_t& files)
{
	double total_count[OP_MAX_COUNTERS] = { 0.0 };

	/* 1st pass: accumulate for each image_name the samples count and
	 * update the total_count of samples */

	map_t::const_iterator it;
	for (it = files.begin(); it != files.end() ; ) {
		pair_it_t p_it = files.equal_range(it->first);

		// the range [p_it.first, p_it.second[ belongs to application
		// it.first->first
		for ( ; p_it.first != p_it.second ; ++p_it.first) {
			for (int i = 0 ; i < OP_MAX_COUNTERS ; ++i) {
				std::ostringstream s;
				s << string(samples_dir) << "/"
				  << p_it.first->second.samplefile_name
				  << "#" << i;
				if (file_exist(s.str()) == false)
					continue;

				samples_file_t samples(s.str());

				u32 count =
					samples.count(0, ~0);

				p_it.first->second.count[i] = count;
				total_count[i] += count;
			}
		}

		it = p_it.second;
	}

	bool empty = true;
	for (int i = 0 ; i < OP_MAX_COUNTERS; ++i) {
		if (total_count[i] != 0.0) {
			empty = false;
		}
	}

	if (empty) {
		cerr << "no samples files found\n";
		return;	// Would exit(EXIT_FAILURE); perhaps
	}

	/* 2nd pass: insert the count of samples for each application and
	 * associate with an iterator to a image name e.g. insert one
	 * item for each application */

	sort_by_counter_t compare(sort_by_counter);
	sorted_map_t sorted_map(compare);
	for (it = files.begin(); it != files.end() ; ) {
		pair_it_t p_it = files.equal_range(it->first);
		counter_array_t count;
		for ( ; p_it.first != p_it.second ; ++p_it.first) {
			count += p_it.first->second.count;
		}

		sorted_map.insert(sorted_map_t::value_type(count, it));

		it = p_it.second;
	}

	/* 3rd pass: we can output the result, during the output we optionnaly
	 * build the set of image_name which belongs to one application and
	 * display these results too */

	/* this if else are only different by the type of iterator used, we
	 * can't easily templatize the block on iterator type because we use
	 * in one case begin()/end() and in another rbegin()/rend(), it's
	 * worthwhile to try to factorize these two piece of code */
	if (reverse_sort) {
		sorted_map_t::reverse_iterator s_it = sorted_map.rbegin();
		for ( ; s_it != sorted_map.rend(); ++s_it) {
			map_t::const_iterator it = s_it->second;
			counter_array_t temp;

			out_filename(it->first, temp, s_it->first, 
				     total_count);

			if (show_shared_libs) {
				pair_it_t p_it = files.equal_range(it->first);
				sort_by_counter_t compare(sort_by_counter);
				sorted_map_t temp_map(compare);

				build_sorted_map_by_count(temp_map, p_it);

				output_image_samples_count(temp_map.rbegin(),
							   temp_map.rend(),
							   s_it->first,
							   total_count);
			}
		}
	} else {
		sorted_map_t::iterator s_it = sorted_map.begin();
		for ( ; s_it != sorted_map.end() ; ++s_it) {
			map_t::const_iterator it = s_it->second;

			counter_array_t temp;
			out_filename(it->first, temp, s_it->first,
				     total_count);

			if (show_shared_libs) {
				pair_it_t p_it = files.equal_range(it->first);
				sort_by_counter_t compare(sort_by_counter);
				sorted_map_t temp_map(compare);

				build_sorted_map_by_count(temp_map, p_it);

				output_image_samples_count(temp_map.begin(),
							   temp_map.end(),
							   s_it->first,
							   total_count);
			}
		}
	}
}

/**
 * check_image_name - check than image_name belonging to samples_filename
 * exist. If not it try to retrieve it through the alternate_filename
 * location.
 */
string check_image_name(const string & image_name,
			const string & samples_filename)
{
	// FIXME: this isn't polite enough for a permissions problem. 
	if (file_exist(image_name))
		return image_name;

	typedef alt_filename_t::const_iterator it_t;
	std::pair<it_t, it_t> p_it =
		alternate_filename.equal_range(basename(image_name));

	if (p_it.first == p_it.second) {

		static bool first_warn = true;
		if (first_warn) {
			cerr << "I can't locate some binary image file, all\n"
			     << "of this file(s) will be ignored in statistics"
			     << endl
			     << "Have you provided the right -p/-P option ?"
			     << endl;
			first_warn = false;
		}

		cerr << "warning: can't locate image file for samples files : "
		     << samples_filename << endl;

		return string();
	}

	if (std::distance(p_it.first, p_it.second) != 1) {
		cerr << "the image name for samples files : "
		     << samples_filename << " is ambiguous\n"
		     << "so this file file will be ignored" << endl;
		return string();
	}

	return p_it.first->second + '/' + p_it.first->first;
}

/**
 * output_symbols_count - open each samples file to cumulate samples count
 * and display a sorted list of symbols and samples ratio
 * \param files the file list to treat.
 * \param counter on which counter to work
 *
 * print the whole cumulated count for all symbols in selected filename
 * (currently the whole base_dir directory)
 */
static void output_symbols_count(map_t& files, int counter)
{
	samples_files_t samples(false, output_format_flags, false, counter);

	map_t::iterator it_f;
	for (it_f = files.begin() ; it_f != files.end() ; ++it_f) {

		string filename = it_f->second.samplefile_name;
		string samples_filename = string(samples_dir) + "/" + filename;

		string lib_name;
		string image_name = extract_app_name(filename, lib_name);

		// if the samples file belongs to a shared lib we need to get
		// the right binary name
		if (lib_name.length())
			image_name = lib_name;

		image_name = demangle_filename(image_name);

		// if the image files does not exist try to retrieve it
		image_name = check_image_name(image_name, samples_filename);

		// check_image_name have already warned the user if something
		// feel bad.
		if (file_exist(image_name)) {
			opp_samples_files samples_file(samples_filename,
						       counter);

			opp_bfd abfd(samples_file, image_name);

			samples.add(samples_file, abfd);
		}
	}

	// select the symbols
	vector<const symbol_entry *> symbols;

	samples.select_symbols(symbols, sort_by_counter, 0.0, false);

	OutputSymbol out(samples, counter);

	out.SetFlag(output_format_flags);

	out.Output(cout, symbols, reverse_sort == 0);
}

/**
 * yet another main ...
 */
int main(int argc, char const * argv[])
{
	get_options(argc, argv);

	if (list_symbols && show_shared_libs) {
		cerr << "You can't specifiy --show-shared-libs and "
		     << "--list-symbols together" << endl;
		exit(EXIT_FAILURE);
	}

	/* TODO: allow as op_merge to specify explicitly name of samples
	 * files rather getting the whole directory. Code in op_merge can
	 * be probably re-used */
	list<string> file_list;
	get_sample_file_list(file_list, samples_dir, "*#*");

	map_t file_map;
	sort_file_list_by_name(file_map, file_list);

	if (list_symbols) {
		output_symbols_count(file_map, counter);
	} else {
		output_files_count(file_map);
	}

	return 0;
}
