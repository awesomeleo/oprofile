/**
 * @file path_filter.cpp
 * Filter paths based on globbed exclude/include list
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Philippe Elie
 * @author John Levon
 */

#include <fnmatch.h>

#include <algorithm>

#include "path_filter.h"
#include "string_manip.h"
#include "file_manip.h"

using namespace std;

bool path_filter::match(std::string const & str) const
{
	vector<string>::const_iterator cit;

	string const & base = basename(str);

	// first, if any component of the dir is listed in exclude -> no
	string comp = dirname(str);
	while (!comp.empty() && comp != "/") {
		cit = find_if(exclude.begin(), exclude.end(),
			fnmatcher(basename(comp)));
		if (cit != exclude.end())
			return false;

		// FIXME: test uneccessary, wait a decent testsuite before
		// removing
		if (comp == dirname(comp))
			break;
		comp = dirname(comp);
	}

	// now if the file name is specifically excluded -> no
	cit = find_if(exclude.begin(), exclude.end(), fnmatcher(base));
	if (cit != exclude.end())
		return false;

	// now if the file name is specifically included -> yes
	cit = find_if(include.begin(), include.end(), fnmatcher(base));
	if (cit != include.end())
		return true;

	// now if any component of the path is included -> yes
	// note that the include pattern defaults to '*'
	string compi = dirname(str);
	while (!compi.empty() && compi != "/") {
		cit = find_if(include.begin(), include.end(),
			fnmatcher(basename(compi)));
		if (cit != include.end())
			return true;
		// FIXME see above.
		if (compi == dirname(compi))
			break;
		compi = dirname(compi);
	}

	return include.empty();
}