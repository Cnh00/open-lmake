// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "env.hh"

using namespace Disk ;

::ostream& operator<<( ::ostream& os , AutodepEnv const& ade ) {
	/**/                         os << "AutodepEnv(" << static_cast<RealPathEnv const&>(ade) ;
	/**/                         os <<','<< ade.service                                      ;
	if ( ade.auto_mkdir        ) os <<",auto_mkdir"                                          ;
	if ( ade.ignore_stat       ) os <<",ignore_stat"                                         ;
	return                       os <<')'                                                    ;
}

AutodepEnv::AutodepEnv( ::string const& env ) {
	if (env.empty()) return ;
	size_t sz  = 0/*garbage*/        ;
	size_t pos = env.find(':'      ) ; if (pos==Npos) goto Fail ;
	/**/   pos = env.find(':',pos+1) ; if (pos==Npos) goto Fail ;
	// service
	service = env.substr(0,pos) ;
	pos++ ;
	// options
	for( ; env[pos]!=':' ; pos++ )
		switch (env[pos]) {
			case 'd' : auto_mkdir  = true             ; break ;
			case 'i' : ignore_stat = true             ; break ;
			case 'n' : lnk_support = LnkSupport::None ; break ;
			case 'f' : lnk_support = LnkSupport::File ; break ;
			case 'a' : lnk_support = LnkSupport::Full ; break ;
			default  : goto Fail ;
		}
	//source dirs
	pos++ ;
	for ( bool first=true ; env[pos]!=':' ; first=false ) {
		if ( !first && env[pos++]!=',' ) goto Fail ;
		::string src_dir_s ;
		SWEAR(env[pos]=='"',env) ;
		tie(src_dir_s,sz) = parse_printable<'"'>(env,pos+1) ;
		SWEAR( src_dir_s.back()=='/' , src_dir_s ) ;
		src_dirs_s.push_back(::move(src_dir_s)) ;
		pos += sz+2 ;                                                          // account for quotes
	}
	pos++ ;
	{ SWEAR(env[pos]=='"',env) ; tie(tmp_dir ,sz) = parse_printable<'"'>(env,pos+1) ; pos += sz+2 ; if (env[pos++]!=':') goto Fail ; } // account for quotes
	{ SWEAR(env[pos]=='"',env) ; tie(tmp_view,sz) = parse_printable<'"'>(env,pos+1) ; pos += sz+2 ; if (env[pos++]!=':') goto Fail ; } // .
	{ SWEAR(env[pos]=='"',env) ; tie(root_dir,sz) = parse_printable<'"'>(env,pos+1) ; pos += sz+2 ; if (env[pos  ]!=0  ) goto Fail ; } // .
	return ;
Fail :
	fail_prod( "bad autodep env format at pos ",pos," : " , env ) ;
}

AutodepEnv::operator ::string() const {
	::string res ;
	// service
	res += service ;
	// options
	res += ':' ;
	if (auto_mkdir ) res += 'd' ;
	if (ignore_stat) res += 'i' ;
	switch (lnk_support) {
		case LnkSupport::None : res += 'n' ; break ;
		case LnkSupport::File : res += 'f' ; break ;
		case LnkSupport::Full : res += 'a' ; break ;
		default : FAIL(lnk_support) ;
	}
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