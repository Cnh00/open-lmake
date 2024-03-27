// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "env.hh"

using namespace Disk ;

::ostream& operator<<( ::ostream& os , AutodepEnv const& ade ) {
	os << "AutodepEnv(" ;
	if (ade.active) {
		/**/                 os <<      static_cast<RealPathEnv const&>(ade) ;
		/**/                 os <<','<< ade.service                          ;
		if (ade.auto_mkdir ) os <<",auto_mkdir"                              ;
		if (ade.ignore_stat) os <<",ignore_stat"                             ;
		if (ade.disabled   ) os <<",disabled"                                ;
	}
	return os <<')' ;
}

AutodepEnv::AutodepEnv( ::string const& env ) {
	if (!env) {
		active = false ;                                                 // nobody to report deps to
		try                     { root_dir = search_root_dir().first ; }
		catch (::string const&) { root_dir = cwd()                   ; }
		return ;
	}
	size_t pos = env.find(':'           ) ; if (pos==Npos) goto Fail ;
	/**/   pos = env.find(':',pos+1/*:*/) ; if (pos==Npos) goto Fail ;
	// service
	service = env.substr(0,pos) ;
	pos++/*:*/ ;
	// options
	for( ; env[pos]!=':' ; pos++ )
		switch (env[pos]) {
			case 'd' : disabled      = true             ; break ;
			case 'i' : ignore_stat   = true             ; break ;
			case 'm' : auto_mkdir    = true             ; break ;
			case 'n' : lnk_support   = LnkSupport::None ; break ;
			case 'f' : lnk_support   = LnkSupport::File ; break ;
			case 'a' : lnk_support   = LnkSupport::Full ; break ;
			case 'r' : reliable_dirs = true             ; break ;
			default  : goto Fail ;
		}
	//source dirs
	pos++/*:*/ ;
	for ( bool first=true ; env[pos]!=':' ; first=false ) {
		if ( !first && env[pos++/*,*/]!=',' ) goto Fail ;
		::string src_dir_s ;
		SWEAR(env[pos]=='"',env) ;
		tie(src_dir_s,pos) = parse_printable<'"'>(env,pos+1/*initial"*/) ;
		SWEAR( src_dir_s.back()=='/' , src_dir_s ) ;
		src_dirs_s.push_back(::move(src_dir_s)) ;
		pos ++/*final"*/ ;
	}
	pos++ ;
	{ SWEAR(env[pos]=='"',env) ; tie(tmp_dir ,pos) = parse_printable<'"'>(env,pos+1/*initial"*/) ; pos ++/*final"*/ ; if (env[pos]!=':') goto Fail ; pos++/*:*/ ; }
	{ SWEAR(env[pos]=='"',env) ; tie(tmp_view,pos) = parse_printable<'"'>(env,pos+1/*initial"*/) ; pos ++/*final"*/ ; if (env[pos]!=':') goto Fail ; pos++/*:*/ ; }
	{ SWEAR(env[pos]=='"',env) ; tie(root_dir,pos) = parse_printable<'"'>(env,pos+1/*initial"*/) ; pos ++/*final"*/ ; if (env[pos]!=0  ) goto Fail ; pos++/*:*/ ; }
	//
	return ;
Fail :
	fail_prod("bad autodep env format at pos",pos,":",env) ;
}

AutodepEnv::operator ::string() const {
	::string res ;
	// service
	res += service ;
	// options
	res += ':' ;
	if (disabled     ) res += 'd' ;
	if (ignore_stat  ) res += 'i' ;
	if (auto_mkdir   ) res += 'm' ;
	if (reliable_dirs) res += 'r' ;
	switch (lnk_support) {
		case LnkSupport::None : res += 'n' ; break ;
		case LnkSupport::File : res += 'f' ; break ;
		case LnkSupport::Full : res += 'a' ; break ;
	DF}
	// source dirs
	res += ':' ;
	bool first = true ;
	for( ::string const& sd_s : src_dirs_s ) {
		SWEAR( sd_s.back()=='/' , sd_s.back() ) ;
		if (first) first  = false ;
		else       res   += ','   ;
		res +=              '"'        ;
		res += mk_printable<'"'>(sd_s) ;
		res +=              '"'        ;
	}
	// other dirs
	append_to_string( res ,":\"", mk_printable<'"'>(tmp_dir) ,"\":\"", mk_printable<'"'>(tmp_view) ,"\":\"", mk_printable<'"'>(root_dir) ,'"' ) ;
	//
	return res ;
}
