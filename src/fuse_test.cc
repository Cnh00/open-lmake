// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"

#include "fuse.hh"

using namespace Disk ;

int main( int /*argc*/ , char* /*argv*/[] ) {
	t_thread_key = '=' ;
	::cerr<<t_thread_key<<" "<<"start "<<cwd_s()<<endl ;

	Fuse::Mount fm { "a" , "b" } ;

	sleep(1) ;
	::cerr<<t_thread_key<<" "<<"before1 "<<cwd_s()<<endl ;
	try                       { ::cout<<read_content("a/x") ;                    }
	catch (::string const& e) { ::cerr<<t_thread_key<<" "<<"error : "<<e<<endl ; }
	::cerr<<t_thread_key<<" "<<"after1\n";

	return 0 ;
}
