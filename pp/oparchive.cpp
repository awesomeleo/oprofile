/**
 * @file oparchive.cpp
 * Implement oparchive utility
 *
 * @remark Copyright 2003, 2004 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Will Cohen
 * @author John Levon
 * @author Philippe Elie
 */

#include <cstdlib>

#include <iostream>
#include <fstream>
#include <cstdlib>

#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "op_file.h"
#include "op_bfd.h"
#include "op_config.h"
#include "oparchive_options.h"
#include "file_manip.h"
#include "cverb.h"
#include "image_errors.h"
#include "string_manip.h"
#include "locate_images.h"

using namespace std;

namespace {


void copy_one_file(image_error err, string const & source, string const & dest)
{
	if (!op_file_readable(source))
		return;

	if (options::list_files) {
		cout << source << endl;
		return;
	}

	if (!copy_file(source, dest) && err == image_ok) {
		cerr << "can't copy from " << source << " to " << dest
		     << " cause: " << strerror(errno) << endl;
	}
}

static void _copy_operf_stats(string const & archive_stats,
                              string const & stats_path)
{
	struct dirent * dirent;
	string throttled_path;
	throttled_path = stats_path + "/throttled";
	DIR * dir =  opendir(throttled_path.c_str());
	while (dir && (dirent = readdir(dir))) {
		if ((!strcmp(dirent->d_name, ".")) || (!strcmp(dirent->d_name, "..")))
			continue;
		string archive_stats_path;
		string throttled_event = throttled_path + "/" + dirent->d_name;
		archive_stats_path = archive_stats + "throttled/" + dirent->d_name;
		if (!options::list_files &&
		    create_path(archive_stats_path.c_str())) {
			cerr << "Unable to create directory for "
			     <<	archive_stats_path << "." << endl;
			exit (EXIT_FAILURE);
		}
		copy_one_file(image_ok, throttled_event, archive_stats_path);
	}
	if (dir)
		closedir(dir);

	for (int i = 0; i < OPERF_MAX_STATS; i++) {
		if (i > 0 && i < OPERF_INDEX_OF_FIRST_LOST_STAT)
			continue;
		string fname = stats_path + "/" + stats_filenames[i];
		int fd = open(fname.c_str(), O_RDONLY);
		if (fd != -1) {
			string archive_stats_path = archive_stats + stats_filenames[i];
			if (!options::list_files &&
			    create_path(archive_stats_path.c_str())) {
				cerr << "Unable to create directory for "
				     <<	archive_stats_path << "." << endl;
				exit (EXIT_FAILURE);
			}
			copy_one_file(image_ok, fname, archive_stats_path);
			close(fd);
		}
	}
}

static void _copy_legacy_stats(DIR * dir, string const & archive_stats,
                               string const & stats_path)
{
	struct dirent * dirent;
	string archive_stats_path = archive_stats + "event_lost_overflow";

	if (!options::list_files &&
	    create_path(archive_stats_path.c_str())) {
		cerr << "Unable to create directory for "
		     <<	archive_stats_path << "." << endl;
		exit (EXIT_FAILURE);
	}
	copy_one_file(image_ok, stats_path + "event_lost_overflow", archive_stats_path);

	while ((dirent = readdir(dir))) {
		int cpu_nr;
		string path;
		if (sscanf(dirent->d_name, "cpu%d", &cpu_nr) != 1)
			continue;
		path = string(dirent->d_name) + "/" + "sample_lost_overflow";
		archive_stats_path = archive_stats + path;
		if (!options::list_files &&
		    create_path(archive_stats_path.c_str())) {
			cerr << "Unable to create directory for "
			     <<	archive_stats_path << "." << endl;
			exit (EXIT_FAILURE);
		}
		copy_one_file(image_ok, stats_path + path, archive_stats_path);
	}
}

void copy_stats(string const & session_samples_dir,
		string const & archive_path)
{
	DIR * dir;
	string stats_path;

	stats_path = session_samples_dir + "stats/";

	if (!(dir = opendir(stats_path.c_str()))) {
		return;
	}

	string sample_base_dir = session_samples_dir.substr(archive_path.size());
	string archive_stats = options::outdirectory + sample_base_dir + "stats/";
	_copy_legacy_stats(dir, archive_stats, stats_path);
	closedir(dir);
	_copy_operf_stats(archive_stats, stats_path);
}

int oparchive(options::spec const & spec)
{
	handle_options(spec);

	string archive_path = classes.extra_found_images.get_archive_path();

	/* Check to see if directory can be created */
	if (!options::list_files && create_path(options::outdirectory.c_str())) {
		cerr << "Unable to create directory for " 
		     <<	options::outdirectory << "." << endl;
		exit (EXIT_FAILURE);
	}

	/* copy over each of the executables and the debuginfo files */
	list<inverted_profile> iprofiles = invert_profiles(classes);

	report_image_errors(iprofiles, classes.extra_found_images);

	list<inverted_profile>::iterator it = iprofiles.begin();
	list<inverted_profile>::iterator const end = iprofiles.end();

	cverb << vdebug << "(exe_names)" << endl << endl;
	for (; it != end; ++it) {

		string exe_name = it->image;
		image_error error;
		string real_exe_name =
			classes.extra_found_images.find_image_path(it->image,
						  error, true);

		if (error == image_ok)
			exe_name = classes.extra_found_images.strip_path_prefix(real_exe_name);

		// output name must be identical to the original name, when
		// using this archive the used fixup will be identical e.g.:
		// oparchive -p /lib/modules/2.6.42/kernel -o tmp;
		// opreport  -p /lib/modules/2.6.42/kernel { archive:tmp }
		string exe_archive_file = options::outdirectory + exe_name;

		// FIXME: hacky
		if (it->error == image_not_found && is_prefix(exe_name, "anon "))
			continue;

		cverb << vdebug << real_exe_name << endl;
		/* Create directory for executable file. */
		if (!options::list_files &&
			create_path(exe_archive_file.c_str())) {
			cerr << "Unable to create directory for "
			     << exe_archive_file << "." << endl;
			exit (EXIT_FAILURE);
		}

		/* Copy actual executable files */
		copy_one_file(it->error, real_exe_name, exe_archive_file);

		/* If there are any debuginfo files, copy them over.
		 * Need to copy the debug info file to somewhere we'll
		 * find it - executable location + "/.debug"
		 * to avoid overwriting files with the same name. The
		 * /usr/lib/debug search path is not going to work.
		 */
		bfd * ibfd = open_bfd(real_exe_name);
		if (ibfd) {
			string dirname = op_dirname(real_exe_name);
			string debug_filename;
			if (find_separate_debug_file(ibfd, real_exe_name,
				debug_filename, classes.extra_found_images)) {
				/* found something copy it over */
				string dest_debug_dir = options::outdirectory +
					dirname + "/.debug/";
				if (!options::list_files &&
				    create_dir(dest_debug_dir.c_str())) {
					cerr << "Unable to create directory: "
					<< dest_debug_dir << "." << endl;
					exit (EXIT_FAILURE);
				}

				string dest_debug = dest_debug_dir +
					op_basename(debug_filename);
				copy_one_file(image_ok, debug_filename, dest_debug);
			}
			bfd_close(ibfd);
		}
	}

	/* place samples and other related material in easily found default directory */
	string dest_session_dir = options::outdirectory + string(OP_SESSION_DIR_DEFAULT);
	string dest_samples_dir = dest_session_dir + string("samples");

	/* dest_session_dir is parent of dest_samples and will also created */

	if (!options::list_files &&
	    create_path(dest_samples_dir.c_str())) {
		cerr << "Unable to create directory for "
		     <<	dest_samples_dir << "." << endl;
		exit (EXIT_FAILURE);
	}

	/* copy over each of the sample files */
	list<string>::iterator sit = sample_files.begin();
	list<string>::iterator const send = sample_files.end();

	string a_sample_file = *sit;
	int offset = a_sample_file.find('{');
	string base_samples_dir = a_sample_file.substr(0, offset);
	copy_stats(base_samples_dir, archive_path);

	cverb << vdebug << "(sample_names)" << endl << endl;

	for (; sit != send; ++sit) {
		string sample_name = *sit;
		/* determine the session name of sample file */
		int offset = sample_name.find('{');
		string base_samples_dir = sample_name.substr(0, offset-1);
		string session = basename(base_samples_dir.c_str());
		/* Get rid of the the archive_path from the name */
		string sample_base = sample_name.substr(offset);
		string sample_archive_file = dest_samples_dir + "/" + session + "/" + sample_base;
		
		cverb << vdebug << sample_name << endl;
		cverb << vdebug << " destp " << sample_archive_file << endl;
		if (!options::list_files &&
			create_path(sample_archive_file.c_str())) {
			cerr << "Unable to create directory for "
			     <<	sample_archive_file << "." << endl;
			exit (EXIT_FAILURE);
		}

		/* Copy over actual sample file. */
		copy_one_file(image_ok, sample_name, sample_archive_file);
	}

	/* copy over the <session-dir>/abi file if it exists */
	char * real_session_dir = realpath(op_session_dir, NULL);
	if (!real_session_dir) {
		cerr << "Unable to to obtain realpath for " << op_session_dir << endl;
		exit (EXIT_FAILURE);
	}
	string abi_name = string(real_session_dir) + string("/abi");
	string dest_abi_name = dest_session_dir + string("/abi");
	copy_one_file(image_ok, archive_path + abi_name, dest_abi_name);

	/* copy over the <session-dir>/samples/oprofiled.log file */
	string log_name = string(real_session_dir) + string("/samples") + string("/oprofiled.log");
	string dest_log_name = dest_samples_dir + string("/oprofiled.log");
	copy_one_file(image_ok, archive_path + log_name, dest_log_name);

	/* copy over the <session-dir>/samples/operf.log file */
	log_name = string(real_session_dir) + string("/samples") + string("/operf.log");
	dest_log_name = dest_samples_dir + string("/operf.log");
	copy_one_file(image_ok, archive_path + log_name, dest_log_name);

	free(real_session_dir);

	return 0;
}

}  // anonymous namespace


int main(int argc, char const * argv[])
{
	return run_pp_tool(argc, argv, oparchive);
}
