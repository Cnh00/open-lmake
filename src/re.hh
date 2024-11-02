// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "serialize.hh"

#if HAS_PCRE
	#define PCRE2_CODE_UNIT_WIDTH 8
	#include <pcre2.h>
#else
	#include <regex>
#endif

// /!\ this interface assumes that all variable parts are enclosed within () : this simpliies a lot prefix and suffix identification

#if HAS_PCRE
	ENUM( RegExprUse
	,	Unused
	,	Old
	,	New
	)
#endif

namespace Re {

	struct Match   ;
	struct RegExpr ;

	static const ::string SpecialChars = "()[.*+?|\\{^$" ;   // in decreasing frequency of occurrence, ...
	inline ::string escape(::string const& s) {              // ... list from https://www.pcre.org/current/doc/html/pcre2pattern.html, under chapter CHARACTERS AND METACHARACTERS
		::string res ; res.reserve(s.size()+(s.size()>>4)) ; // take a little margin for escapes
		for( char c : s ) {
			if (SpecialChars.find(c)!=Npos) res += '\\' ;    // escape specials
			/**/                            res += c    ;
		}
		return res ;
	}

	#if HAS_PCRE

		inline void swap( Match& a , Match& b ) ;
		struct Match {
			friend RegExpr ;
			friend void swap( Match& a , Match& b ) ;
		private :
			// cxtors & casts
			Match() = default ;
			//
			Match( RegExpr const& re , ::string const& s , bool chk_psfx ) ;
		public :
			~Match() {
				if (_data) pcre2_match_data_free(_data) ;
			}
			//
			Match           (Match&& m) { swap(self,m) ;               }
			Match& operator=(Match&& m) { swap(self,m) ; return self ; }
			// accesses
			bool operator+() const { return _data && pcre2_get_ovector_pointer(_data)[0]!=PCRE2_UNSET ; }
			bool operator!() const { return !+self                                                    ; }
			//
			::string_view operator[](size_t i) const {
				PCRE2_SIZE const* v = pcre2_get_ovector_pointer(_data) ;
				return { _subject.data()+v[2*i] , v[2*i+1]-v[2*i] } ;
			}
			// data
		private :
			pcre2_match_data* _data    = nullptr ;
			::string          _subject ;
		} ;
		inline void swap( Match& a , Match& b ) {
			::swap(a._data   ,b._data   ) ;
			::swap(a._subject,b._subject) ;
		}

		inline void swap( RegExpr& a , RegExpr& b ) ;
		struct RegExpr {
			friend Match ;
			friend void swap( RegExpr& a , RegExpr& b ) ;
			using Use = RegExprUse ;
			static constexpr size_t ErrMsgSz = 120 ;                           // per PCRE doc
			struct Cache {
				// cxtors & casts
				void serdes(::ostream&) const ;
				void serdes(::istream&) ;
				// accesses
				bool _has_new() const { return _n_unused<0 ; }
				// services
				bool steady() const {
					return !_n_unused ;
				}
				pcre2_code const* insert(::string const& infix) ;
				// data
			private :
				::umap_s<::pair<pcre2_code const*,Use/*use*/>> _cache    ;
				ssize_t                                        _n_unused = 0 ; // <0 if new codes
			} ;
			// statics
		private :
			static ::pcre2_code* _s_compile(::string const& infix) ;
			// static data
		public :
			static Cache s_cache ;
			// cxtors & casts
			RegExpr() = default ;
			RegExpr(::string const& pattern) ;
			//
			RegExpr           (RegExpr&& re) { swap(self,re) ;               }
			RegExpr& operator=(RegExpr&& re) { swap(self,re) ; return self ; }
			// services
			Match match( ::string const& subject , bool chk_psfx=true ) const {
				return { self , subject , chk_psfx } ;
			}
			size_t mark_count() const {
				uint32_t cnt ;
				pcre2_pattern_info( _code , PCRE2_INFO_CAPTURECOUNT , &cnt ) ;
				return cnt ;
			}
			// data
			::string pfx ;                                                     // fixed prefix
			::string sfx ;                                                     // fixed suffix
		private :
			pcre2_code const* _code = nullptr ;                                // only contains code for infix part, shared and stored in s_store
		} ;
		inline void swap( RegExpr& a , RegExpr& b ) {
			::swap(a.pfx  ,b.pfx  ) ;
			::swap(a.sfx  ,b.sfx  ) ;
			::swap(a._code,b._code) ;
		}

	#else

		struct Match : private ::smatch {
			friend RegExpr ;
			// cxtors & casts
		private :
			using ::smatch::smatch ;
			// accesses
		public :
			bool operator+() const { return !empty() ; }
			bool operator!() const { return !+self   ; }
			//
			::string_view operator[](size_t i) const {
				::sub_match sm = ::smatch::operator[](i) ;
				return {sm.first,sm.second} ;
			}
		} ;

		struct RegExpr : private ::regex {
			friend Match ;
			static constexpr flag_type Flags = ECMAScript|optimize ;
			struct Cache {                                           // there is no serialization facility and cache is not implemented, fake it
				static constexpr bool steady() { return true ; }
			} ;
			// static data
			static Cache s_cache ;
			// cxtors & casts
			RegExpr() = default ;
			RegExpr(::string const& pattern) : ::regex{pattern,Flags} {}
			// services
			Match match( ::string const& subject , bool /*chk_psfx*/=true ) const {
				Match res ;
				::regex_match(subject,res,self) ;
				return res ;
			}
			size_t mark_count() const {
				return ::regex::mark_count() ;
			}
		} ;

	#endif

}
