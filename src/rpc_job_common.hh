// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "config.hh"

#include "disk.hh"
#include "hash.hh"
#include "serialize.hh"
#include "time.hh"

#include "autodep/env.hh"

template<class E,class T> static constexpr bool _chk_flags_tab(::array<::pair<E,T>,N<E>> tab) {
	bool res = true ;
	for( E e : iota(All<E>) ) res &= tab[+e].first==e ;
	return res ;
}

// START_OF_VERSIONING
ENUM_2( Dflag        // flags for deps
,	NRule = Required // number of Dflag's allowed in rule definition
,	NDyn  = Static   // number of Dflag's allowed in side flags
	//
,	Critical         // if modified, ignore following deps
,	Essential        // show when generating user oriented graphs
,	IgnoreError      // dont propagate error if dep is in error (Error instead of Err because name is visible from user)
,	Required         // dep must be buildable
,	Static           // is static dep, for internal use only
)
// END_OF_VERSIONING
static constexpr ::amap<Dflag,char,N<Dflag>> DflagChars {{
	{ Dflag::Critical    , 'c' }
,	{ Dflag::Essential   , 'E' }
,	{ Dflag::IgnoreError , 'e' }
,	{ Dflag::Required    , 'r' }
,	{ Dflag::Static      , 'S' }
}} ;
using Dflags = BitMap<Dflag> ;
static_assert(_chk_flags_tab(DflagChars)) ;

// START_OF_VERSIONING
ENUM_1( ExtraDflag
,	NRule          // all flags allowed
,	Top
,	Ignore
,	StatReadData
)
// END_OF_VERSIONING
static constexpr ::amap<ExtraDflag,char,N<ExtraDflag>> ExtraDflagChars {{
	{ ExtraDflag::Top          , 0   }
,	{ ExtraDflag::Ignore       , 'I' }
,	{ ExtraDflag::StatReadData , 'd' }
}} ;
using ExtraDflags = BitMap<ExtraDflag> ;
static_assert(_chk_flags_tab(ExtraDflagChars)) ;

// START_OF_VERSIONING
ENUM_2( Tflag      // flags for targets
,	NRule = Static // number of Tflag's allowed in rule definition
,	NDyn  = Phony  // number of Tflag's allowed inside flags
,	Essential      // show when generating user oriented graphs
,	Incremental    // reads are allowed (before earliest write if any)
,	NoUniquify     // target is uniquified if it has several links and is incremental
,	NoWarning      // warn if target is either uniquified or unlinked and generated by another rule
,	Phony          // accept that target is not generated
,	Static         // is static  , for internal use only, only if also a Target
,	Target         // is a target, for internal use only
)
// END_OF_VERSIONING
static constexpr ::amap<Tflag,char,N<Tflag>> TflagChars {{
	{ Tflag::Essential   , 'E' }
,	{ Tflag::Incremental , 'i' }
,	{ Tflag::NoUniquify  , 'u' }
,	{ Tflag::NoWarning   , 'w' }
,	{ Tflag::Phony       , 'p' }
,	{ Tflag::Static      , 'S' }
,	{ Tflag::Target      , 'T' }
}} ;
using Tflags = BitMap<Tflag> ;
static_assert(_chk_flags_tab(TflagChars)) ;
inline bool static_phony(Tflags tf) {
	return tf[Tflag::Target] && (tf[Tflag::Static]||tf[Tflag::Phony]) ;
}

// START_OF_VERSIONING
ENUM_1( ExtraTflag
,	NRule = Allow  // number of Tflag's allowed in rule definition
,	Top
,	Ignore
,	Optional
,	SourceOk       // ok to overwrite source files
,	Allow          // writing to this target is allowed (for use in clmake.target and ltarget)
,	Wash           // target was unlinked when washing before job execution
)
// END_OF_VERSIONING
static constexpr ::amap<ExtraTflag,char,N<ExtraTflag>> ExtraTflagChars {{
	{ ExtraTflag::Top      , 0   }
,	{ ExtraTflag::Ignore   , 'I' }
,	{ ExtraTflag::Optional , 0   }
,	{ ExtraTflag::SourceOk , 's' }
,	{ ExtraTflag::Allow    , 'a' }
,	{ ExtraTflag::Wash     , 0   }
}} ;
using ExtraTflags = BitMap<ExtraTflag> ;
static_assert(_chk_flags_tab(ExtraTflagChars)) ;
