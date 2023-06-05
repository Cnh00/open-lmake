// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"

#include "gather_deps.hh"

using namespace Disk ;
using namespace Hash ;

ENUM( CmdKey , None )
ENUM( CmdFlag
,	AutodepMethod
,	AutoMkdir
,	IgnoreStat
,	LinkSupport
,	Out
)

int main( int argc , char* argv[] ) {
	app_init(true/*search_root*/) ;
	block_sig(SIGCHLD) ;
	SWEAR(::chdir(g_startup_dir_s->c_str())) ;
	//
	Syntax<CmdKey,CmdFlag> syntax{{
		{ CmdFlag::AutodepMethod , { .short_name='m' , .has_arg=true  , .doc="method used to detect deps (none, ld_audit, ld_preload, ptrace)" } }
	,	{ CmdFlag::AutoMkdir     , { .short_name='d' , .has_arg=false , .doc="automatically create dir upon chdir"                             } }
	,	{ CmdFlag::IgnoreStat    , { .short_name='i' , .has_arg=false , .doc="stat-like syscalls do not trigger dependencies"                  } }
	,	{ CmdFlag::LinkSupport   , { .short_name='s' , .has_arg=true  , .doc="level of symbolic link support (none, file, full)"               } }
	,	{ CmdFlag::Out           , { .short_name='o' , .has_arg=true  , .doc="output file"                                                     } }
	}} ;
	CmdLine<CmdKey,CmdFlag> cmd_line{syntax,argc,argv} ;
	//
	if (!( cmd_line.flags[CmdFlag::AutodepMethod] && cmd_line.flags[CmdFlag::LinkSupport] )) syntax.usage("must have both autodep-method and link-support options") ;
	//
	GatherDeps gather_deps { New } ;
	//
	try {
		gather_deps.autodep_method          = mk_enum<AutodepMethod>(cmd_line.flag_args[+CmdFlag::AutodepMethod]) ;
		gather_deps.autodep_env.auto_mkdir  = cmd_line.flags[CmdFlag::AutoMkdir ]                                 ;
		gather_deps.autodep_env.ignore_stat = cmd_line.flags[CmdFlag::IgnoreStat]                                 ;
		gather_deps.autodep_env.lnk_support = mk_enum<LnkSupport>(cmd_line.flag_args[+CmdFlag::LinkSupport])      ;
		gather_deps.autodep_env.root_dir    = *g_root_dir                                                         ;
	} catch (::string const& e) { syntax.usage(e) ; }
	//
	Status status ;
	//                                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	try                       { status = gather_deps.exec_child( cmd_line.args ) ; }
	//                                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	catch (::string const& e) { exit(2,e,'\n') ;                                   }
	//
	::ostream* ds       ;
	OFStream   user_out ;
	if (cmd_line.flags[CmdFlag::Out]) { user_out.open(cmd_line.flag_args[+CmdFlag::Out]) ; ds = &user_out ; }
	else                              {                                                    ds = &::cerr   ; }
	::ostream& deps_stream = *ds ;
	deps_stream << "targets :\n" ;
	for( auto [target,ai] : gather_deps.accesses )
		switch (ai.write) {
			case Maybe : deps_stream << "! " << target << '\n' ; break ;
			case Yes   : deps_stream << "> " << target << '\n' ; break ;
			default : ;
		}
	deps_stream << "deps :\n" ;
	::string prev_dep      ;
	bool     prev_parallel = false ;
	NodeIdx  critical_lvl  = 0     ;
	auto send = [&]( ::string const& dep , DepInfo info ) {                    // process deps with a delay of 1 because we need next info for ascii art
		if (!prev_dep.empty()) {
			deps_stream << setw(critical_lvl*2)<<"" ;
			if      ( !prev_parallel && info!=DepInfo::Parallel ) deps_stream << "  "  ;
			else if ( !prev_parallel && info==DepInfo::Parallel ) deps_stream << "/ "  ;
			else if (  prev_parallel && info==DepInfo::Parallel ) deps_stream << "| "  ;
			else                                                  deps_stream << "\\ " ;
			deps_stream << prev_dep << '\n' ;
		}
		prev_parallel = info==DepInfo::Parallel ;
		critical_lvl += info==DepInfo::Critical ;
		prev_dep      = dep                     ;
	} ;
	for( auto const& [dep,ai] : gather_deps.accesses ) if (ai.write==No) send(dep,ai.dep_info ) ;
	/**/                                                                 send({} ,DepInfo::Seq) ; // send last
	return status!=Status::Ok ;
}
