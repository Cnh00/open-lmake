// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "config.hh"

#include "disk.hh"
#include "hash.hh"
#include "lib.hh"
#include "serialize.hh"
#include "time.hh"

ENUM( Status       // result of job execution
,	New            // job was never run
,	Lost           // job was lost (it disappeared for an unknown reason)
,	Killed         // <=Killed means job was killed
,	ChkDeps        // dep check failed
,	Garbage        // <=Garbage means job has not run reliably
,	Ok             // job execution ended successfully
,	Frozen         // job behaves as a source
,	Err            // >=Err means job ended in error
,	ErrFrozen      // job is frozen in error
,	Timeout        // job timed out
,	SystemErr      // a system error occurrred during job execution
)

ENUM( JobProc
,	None
,	Start
,	ReportStart
,	Continue       // req is killed but job is necessary for some other req
,	NotStarted     // req was killed before it actually started
,	ChkDeps
,	DepCrcs
,	LiveOut
,	End
)

ENUM( DepInfo
,	Parallel       // dep is parallel with prev ont
,	Seq            // dep is sequential
,	Critical       // dep starts a new critical section
)

ENUM( DepAccess    // content which syscall has access to
,	Stat           // syscall sees inode   content (implied by other accesses)
,	Lnk            // syscall sees link    content if dep is a link
,	Reg            // syscall sees regular content if dep is regular
)
using DepAccesses = BitMap<DepAccess> ;                                        // a syscall may have access to any of the contents
constexpr DepAccesses DepAccessesData =  DepAccess::Lnk|DepAccess::Reg ;

ENUM_1( JobReasonTag                   // see explanations in table below
,	HasNode = ClashTarget              // if >=HasNode, a node is associated
,	None
// with reason
,	ChkDeps
,	Cmd
,	Force
,	Garbage
,	Killed
,	Lost
,	New
,	OldError
,	Rsrcs
// with node
,	ClashTarget
,	DepChanged
,	DepNotReady
,	DepOutOfDate
,	NoTarget
,	PrevTarget
// with error
,	DepErr                             // if >=DepErr, job did not complete because of a dep
,	DepOverwritten
,	StaticDepMissing
)
static constexpr const char* JobReasonTagStrs[] = {
	"no reason"                                            // None
// with reason
,	"dep check requires rerun"                             // ChkDeps
,	"command changed"                                      // Cmd
,	"job forced"                                           // Force
,	"job ran with unstable data"                           // Garbage
,	"job was killed"                                       // Killed
,	"job was lost"                                         // Lost
,	"job was never run"                                    // New
,	"job was in error"                                     // OldError
,	"resources changed and job was in error"               // Rsrcs
// with node
,	"multiple simultaneous writes"                         // ClashTarget
,	"dep changed"                                          // DepChanged
,	"dep not ready"                                        // DepNotReady
,	"dep out of date"                                      // DepOutOfDate
,	"target missing"                                       // NoTarget
,	"target previously existed"                            // PrevTarget
// with error
,	"dep in error"                                         // DepErr
,	"dep has been overwritten"                             // DepOverwritten
,	"static dep missing"                                   // StaticDepMissing
} ;
static_assert(sizeof(JobReasonTagStrs)/sizeof(const char*)==+JobReasonTag::N) ;

ENUM_1( Flag
,	Internal = NativeStar              // >=Internal means for internal use only
,	Crc                                // generate a crc for this target (compulsery if Match)
,	Dep                                // reads not followed by writes trigger dependencies
,	Incremental                        // reads   are allowed (before earliest write if any)
,	ManualOk                           // ok to overwrite manual files
,	Match                              // make target non-official (no match on it)
,	Optional                           // unlinks are allowed (possibly followed by reads which are ignored)
,	SourceOk                           // ok to overwrite source files
,	Star                               // target is a star target, even if no star stems
,	Stat                               // inode accesses (stat-like) are not ignored
,	Warning                            // warn if target is unlinked and was generated by another rule
,	Write                              // writes  are allowed (possibly followed by reads which are ignored)
,	NativeStar                         // target contains star stems
)
using Flags = BitMap<Flag> ;
static constexpr Flags DfltFlags      { Flag::Crc , Flag::Dep ,                     Flag::Match , Flag::Stat , Flag::Warning , Flag::Write } ; // default flags for targets
static constexpr Flags UnexpectedFlags{             Flag::Dep , Flag::Incremental ,               Flag::Stat                               } ; // flags used for accesses that are not targets

struct JobStats {
	using Delay = Time::Delay ;
	// data
	Delay  cpu   ;
	Delay  job   ;                     // elapsed in job
	Delay  total ;                     // elapsed including overhead
	size_t mem   = 0 ;                 // in bytes
} ;

struct DepDigest {
	friend ::ostream& operator<<( ::ostream& , DepDigest const& ) ;
	using Date = Time::DiskDate ;
	// cxtors & casts
	DepDigest( Date d , DepInfo di=DepInfo::Unknown ) : date{d} , garbage{false} , info{di} {}
	DepDigest(          DepInfo di=DepInfo::Unknown ) :           garbage{true } , info{di} {}
	// data
	Date    date    ;                                      // if !garbage
	bool    garbage = true             ;
	DepInfo info    = DepInfo::Unknown ;
} ;

struct TargetDigest {
	friend ::ostream& operator<<( ::ostream& , TargetDigest const& ) ;
	using Crc  = Hash::Crc  ;
	// cxtors & casts
	TargetDigest( VarIdx t=-1 , DepAccesses d={} , bool w=false , Crc c={} ) : tgt_idx{t} , das{d} , write{w} , crc{c} {}
	// services
	template<IsStream S> void serdes(S& s) {
		if (::is_base_of_v<istream,S>) crc = Crc::Unknown ;
		/**/       ::serdes(s,tgt_idx) ;
		/**/       ::serdes(s,das    ) ;
		/**/       ::serdes(s,write  ) ;
		if (write) ::serdes(s,crc    ) ;
	}
	// data
	VarIdx      tgt_idx = -1    ;
	DepAccesses das     ;
	bool        write   = false ;
	Crc         crc     ;              // if write
} ;

struct JobDigest {
	friend ::ostream& operator<<( ::ostream& , JobDigest const& ) ;
	// services
	template<IsStream T> void serdes(T& s) {
		::serdes(s,status ) ;
		::serdes(s,targets) ;
		::serdes(s,deps   ) ;
		::serdes(s,stderr ) ;
		::serdes(s,stats  ) ;
	}
	// data
	Status                 status  = Status::New ;
	::vmap_s<TargetDigest> targets = {}          ;
	::vmap_s<DepDigest   > deps    = {}          ;
	::string               stderr  = {}          ;
	JobStats               stats   = {}          ;
} ;

struct JobRpcReq {
	using Proc = JobProc ;
	friend ::ostream& operator<<( ::ostream& , JobRpcReq const& ) ;
	// cxtors & casts
	JobRpcReq() = default ;
	JobRpcReq( Proc p , SeqId ui , JobIdx j , in_port_t pt                  ) : proc{p} , seq_id{ui} , job{j} , port{pt}                     { SWEAR( p==Proc::Start                       ) ; }
	JobRpcReq( Proc p , SeqId ui , JobIdx j , Status s                      ) : proc{p} , seq_id{ui} , job{j} , digest{.status=s           } { SWEAR( p==Proc::End && s<=Status::Garbage   ) ; }
	JobRpcReq( Proc p ,            JobIdx j , Status s , ::string const& e  ) : proc{p} ,              job{j} , digest{.status=s,.stderr{e}} { SWEAR( p==Proc::End && s==Status::Err       ) ; }
	JobRpcReq( Proc p , SeqId ui , JobIdx j , ::string_view const& t        ) : proc{p} , seq_id{ui} , job{j} , txt   {t                   } { SWEAR( p==Proc::LiveOut                     ) ; }
	JobRpcReq( Proc p , SeqId ui , JobIdx j , JobDigest const& d            ) : proc{p} , seq_id{ui} , job{j} , digest{d                   } { SWEAR( p==Proc::End                         ) ; }
	JobRpcReq( Proc p , SeqId ui , JobIdx j , ::vmap_s<DepDigest> const& ds ) : proc{p} , seq_id{ui} , job{j} , digest{.deps=ds            } { SWEAR( p==Proc::ChkDeps || p==Proc::DepCrcs ) ; }
	// services
	template<IsStream T> void serdes(T& s) {
		if (::is_base_of_v<::istream,T>) *this = JobRpcReq() ;
		::serdes(s,proc  ) ;
		::serdes(s,seq_id) ;
		::serdes(s,job   ) ;
		switch (proc) {
			case Proc::Start   : ::serdes(s,port  ) ; break ;
			case Proc::LiveOut : ::serdes(s,txt   ) ; break ;
			case Proc::ChkDeps :
			case Proc::DepCrcs :
			case Proc::End     : ::serdes(s,digest) ; break ;
			default            : ;
		}
	}
	// data
	Proc      proc   = Proc::None ;
	SeqId     seq_id = 0          ;
	JobIdx    job    = 0          ;
	in_port_t port   = 0          ; // if proc==Start
	JobDigest digest ;              // if proc==ChkDeps || DepCrcs || End
	::string  txt    ;              // if proc==LiveOut
} ;

struct JobReason {
	friend ::ostream& operator<<( ::ostream& , JobReason const& ) ;
	using Tag = JobReasonTag ;
	// cxtors & casts
	JobReason(                   ) = default ;
	JobReason( Tag t             ) : tag{t}           { SWEAR( t< Tag::HasNode       ) ; }
	JobReason( Tag t , NodeIdx n ) : tag{t} , node{n} { SWEAR( t>=Tag::HasNode && +n ) ; }
	// accesses
	bool operator+() const { return +tag              ; }
	bool operator!() const { return !tag              ; }
	bool has_err  () const { return tag>=Tag::DepErr  ; }
	// services
	JobReason  operator| (JobReason jr) const { if (!*this) return jr  ; return *this ; }
	JobReason& operator|=(JobReason jr)       { if (!*this) *this = jr ; return *this ; }
	// data
	Tag     tag  = JobReasonTag::None ;
	NodeIdx node = 0                  ;
} ;

struct TargetSpec {
	friend ::ostream& operator<<( ::ostream& , TargetSpec const& ) ;
	// cxtors & casts
	TargetSpec( ::string const& p={} , Flags f={} , ::vector<VarIdx> c={} ) : pattern{p} , flags{f} , conflicts{c} {}
	template<IsStream S> void serdes(S& s) {
		::serdes(s,pattern  ) ;
		::serdes(s,flags    ) ;
		::serdes(s,conflicts) ;
	}
	// services
	bool operator==(TargetSpec const&) const = default ;
	// data
	::string         pattern   ;
	Flags            flags     ;
	::vector<VarIdx> conflicts ;       // the idx of the previous targets that may conflict with this one
} ;

ENUM_2( AutodepMethod
,	Ld   = LdAudit                                         // >=Ld means a lib is pre-loaded (through LD_AUDIT or LD_PRELOAD)
,	Dflt =                                                 // by default, use most reliable available method
		HAS_PTRACE   ? AutodepMethod::Ptrace
	:	HAS_LD_AUDIT ? AutodepMethod::LdAudit
	:	               AutodepMethod::LdPreload
,	None
,	Ptrace
,	LdAudit
,	LdPreload
)

struct JobRpcReply {
	friend ::ostream& operator<<( ::ostream& , JobRpcReply const& ) ;
	using Crc  = Hash::Crc ;
	using Proc = JobProc   ;
	// cxtors & casts
	JobRpcReply(                                  ) = default ;
	JobRpcReply( Proc p                           ) : proc{p}                      {                              }
	JobRpcReply( Proc p , Bool3                o  ) : proc{p} , ok{o  }            { SWEAR(proc==Proc::ChkDeps) ; }
	JobRpcReply( Proc p , ::vector<Crc> const& cs ) : proc{p} , ok{Yes} , crcs{cs} { SWEAR(proc==Proc::DepCrcs) ; }
	// services
	template<IsStream S> void serdes(S& s) {
		if (is_base_of_v<::istream,S>) *this = JobRpcReply() ;
		::serdes(s,proc) ;
		switch (proc) {
			case Proc::None    :                                     break ;
			case Proc::DepCrcs : ::serdes(s,ok) ; ::serdes(s,crcs) ; break ;
			case Proc::ChkDeps : ::serdes(s,ok) ;                    break ;
			case Proc::Start :
				::serdes(s,addr            ) ;
				::serdes(s,ancillary_file  ) ;
				::serdes(s,autodep_method  ) ;
				::serdes(s,auto_mkdir      ) ;
				::serdes(s,chroot          ) ;
				::serdes(s,cwd             ) ;
				::serdes(s,env             ) ;
				::serdes(s,force_deps      ) ;
				::serdes(s,hash_algo       ) ;
				::serdes(s,host            ) ;
				::serdes(s,ignore_stat     ) ;
				::serdes(s,interpreter     ) ;
				::serdes(s,is_python       ) ;
				::serdes(s,job_id          ) ;
				::serdes(s,job_tmp_dir     ) ;
				::serdes(s,keep_tmp        ) ;
				::serdes(s,kill_sigs       ) ;
				::serdes(s,live_out        ) ;
				::serdes(s,lnk_support     ) ;
				::serdes(s,reason          ) ;
				::serdes(s,remote_admin_dir) ;
				::serdes(s,root_dir        ) ;
				::serdes(s,rsrcs           ) ;
				::serdes(s,script          ) ;
				::serdes(s,seq_id          ) ;
				::serdes(s,small_id        ) ;
				::serdes(s,stdin           ) ;
				::serdes(s,stdout          ) ;
				::serdes(s,targets         ) ;
				::serdes(s,timeout         ) ;
			break ;
			default : FAIL(proc) ;
		}
	}
	// data
	Proc                 proc             = Proc         ::None ;
	in_addr_t            addr             = 0                   ;              // proc == Start, the address at which server can contact job, it is assumed that it can be used by subprocesses
	::string             ancillary_file   ;                                    // proc == Start
	AutodepMethod        autodep_method   = AutodepMethod::None ;              // proc == Start
	bool                 auto_mkdir       = false               ;              // proc == Start, if true <=> auto mkdir in case of chdir
	::string             chroot           ;                                    // proc == Start
	::string             cwd              ;                                    // proc == Start
	::vmap_ss            env              ;                                    // proc == Start
	::vector_s           force_deps       ;                                    // proc == Start, deps that may clash with targets
	Hash::Algo           hash_algo        = Hash::Algo::Unknown ;              // proc == Start
	::string             host             ;                                    // proc == Start, filled in job_exec
	bool                 ignore_stat      = false               ;              // proc == Start, if true <=> stat-like syscalls do not trigger dependencies
	::vector_s           interpreter      ;                                    // proc == Start, actual interpreter used to execute script
	bool                 is_python        = false               ;              // proc == Start, if true <=> script is a Python script
	JobIdx               job_id           = 0                   ;              // proc == Start, filled in job_exec
	::string             job_tmp_dir      ;                                    // proc == Start
	bool                 keep_tmp         = false               ;              // proc == Start
	vector<int>          kill_sigs        ;                                    // proc == Start
	bool                 live_out         = false               ;              // proc == Start
	LnkSupport           lnk_support      = LnkSupport   ::None ;              // proc == Start
	JobReason            reason           = JobReasonTag ::None ;              // proc == Start
	::string             remote_admin_dir ;                                    // proc == Start
	::string             root_dir         ;                                    // proc == Start
	::vmap_ss            rsrcs            ;                                    // proc == Start
	::string             script           ;                                    // proc == Start
	SeqId                seq_id           = 0                   ;              // proc == Start, filled in job_exec
	SmallId              small_id         = 0                   ;              // proc == Start
	::string             stdin            ;                                    // proc == Start
	::string             stdout           ;                                    // proc == Start
	::vector<TargetSpec> targets          ;                                    // proc == Start
	Time::Delay          timeout          ;                                    // proc == Start
	Bool3                ok               = No                  ;              // proc == ChkDeps || DepCrcs, if No <=> deps in error, if Maybe <=> deps not ready
	::vector<Crc>        crcs             ;                                    // proc ==            DepCrcs
} ;

struct JobInfo {
	friend ::ostream& operator<<( ::ostream& , JobInfo const& ) ;
	using Date = Time::ProcessDate ;
	template<IsStream T> void serdes(T& s) {
		::serdes(s,end_date) ;
		::serdes(s,stdout  ) ;
		::serdes(s,wstatus ) ;
	}
	// data
	Date     end_date ;
	::string stdout   ;
	int      wstatus  = 0 ;
} ;

ENUM_1( JobExecRpcProc
,	Cached = Deps                      // >=Cached means that report may be cached and in that case, proc's are ordered by importance (report is sent if more important)
,	None
,	ChkDeps
,	CriticalBarrier
,	DepCrcs
,	Heartbeat
,	Kill
,	Tmp                                // write activity in tmp has been detected (hence clean up is required)
,	Trace                              // no algorithmic info, just for tracing purpose
,	Deps
,	Updates                            // read then write
,	Unlinks
,	Targets
)

struct JobExecRpcReq {
	friend ::ostream& operator<<( ::ostream& , JobExecRpcReq const& ) ;
	// make short lines
	using P   = JobExecRpcProc    ;
	using PD  = Time::ProcessDate ;
	using DD  = Time::DiskDate    ;
	using MDD = ::vmap_s<DD>      ;
	using S   = ::string          ;
	using VS  = ::vector_s        ;
	using DAs = DepAccesses       ;
	// cxtors & casts
	JobExecRpcReq(                                            S const& c={} ) :                                                                          comment{c} {                              }
	JobExecRpcReq( P p ,                             bool s , S const& c={} ) : proc{p} , sync{s}                                                      , comment{c} { SWEAR(!has_files       ()) ; }
	JobExecRpcReq( P p ,                                      S const& c={} ) : proc{p}                                                                , comment{c} { SWEAR(!has_files       ()) ; }
	//
	JobExecRpcReq( P p , S const& f , DD d , DAs a , bool s , S const& c={} ) : proc{p} , sync{s} ,                   das{a} , files{{{       f ,d}} } , comment{c} { SWEAR( has_deps        ()) ; }
	JobExecRpcReq( P p , S     && f , DD d , DAs a , bool s , S const& c={} ) : proc{p} , sync{s} ,                   das{a} , files{{{::move(f),d}} } , comment{c} { SWEAR( has_deps        ()) ; }
	JobExecRpcReq( P p , MDD const& fs     , DAs a , bool s , S const& c={} ) : proc{p} , sync{s} ,                   das{a} , files{       fs       } , comment{c} { SWEAR( has_deps        ()) ; }
	JobExecRpcReq( P p , MDD     && fs     , DAs a , bool s , S const& c={} ) : proc{p} , sync{s} ,                   das{a} , files{::move(fs)      } , comment{c} { SWEAR( has_deps        ()) ; }
	JobExecRpcReq( P p , S const& f , DD d , DAs a ,          S const& c={} ) : proc{p} ,                             das{a} , files{{{       f ,d}} } , comment{c} { SWEAR( has_deps        ()) ; }
	JobExecRpcReq( P p , S     && f , DD d , DAs a ,          S const& c={} ) : proc{p} ,                             das{a} , files{{{::move(f),d}} } , comment{c} { SWEAR( has_deps        ()) ; }
	JobExecRpcReq( P p , MDD const& fs     , DAs a ,          S const& c={} ) : proc{p} ,                             das{a} , files{       fs       } , comment{c} { SWEAR( has_deps        ()) ; }
	JobExecRpcReq( P p , MDD     && fs     , DAs a ,          S const& c={} ) : proc{p} ,                             das{a} , files{::move(fs)      } , comment{c} { SWEAR( has_deps        ()) ; }
	//
	JobExecRpcReq( P p , S const& f        , DAs a , bool s , S const& c={} ) : proc{p} , sync{s} , auto_date{true} , das{a} , files{{{       f ,{}}}} , comment{c} { SWEAR( has_deps        ()) ; }
	JobExecRpcReq( P p , S     && f        , DAs a , bool s , S const& c={} ) : proc{p} , sync{s} , auto_date{true} , das{a} , files{{{::move(f),{}}}} , comment{c} { SWEAR( has_deps        ()) ; }
	JobExecRpcReq( P p , VS  const& fs     , DAs a , bool s , S const& c={} ) : proc{p} , sync{s} , auto_date{true} , das{a} , files{_mk_files(fs)   } , comment{c} { SWEAR( has_deps        ()) ; }
	JobExecRpcReq( P p , S const& f        , DAs a ,          S const& c={} ) : proc{p} ,           auto_date{true} , das{a} , files{{{       f ,{}}}} , comment{c} { SWEAR( has_deps        ()) ; }
	JobExecRpcReq( P p , S     && f        , DAs a ,          S const& c={} ) : proc{p} ,           auto_date{true} , das{a} , files{{{::move(f),{}}}} , comment{c} { SWEAR( has_deps        ()) ; }
	JobExecRpcReq( P p , VS  const& fs     , DAs a ,          S const& c={} ) : proc{p} ,           auto_date{true} , das{a} , files{_mk_files(fs)   } , comment{c} { SWEAR( has_deps        ()) ; }
	//
	JobExecRpcReq( P p , S const& f        ,         bool s , S const& c={} ) : proc{p} , sync{s} ,                            files{{{       f ,{}}}} , comment{c} { SWEAR( has_targets_only()) ; }
	JobExecRpcReq( P p , S     && f        ,         bool s , S const& c={} ) : proc{p} , sync{s} ,                            files{{{::move(f),{}}}} , comment{c} { SWEAR( has_targets_only()) ; }
	JobExecRpcReq( P p , VS  const& fs     ,         bool s , S const& c={} ) : proc{p} , sync{s} ,                            files{_mk_files(fs)   } , comment{c} { SWEAR( has_targets_only()) ; }
	JobExecRpcReq( P p , S const& f        ,                  S const& c={} ) : proc{p} ,                                      files{{{       f ,{}}}} , comment{c} { SWEAR( has_targets_only()) ; }
	JobExecRpcReq( P p , S     && f        ,                  S const& c={} ) : proc{p} ,                                      files{{{::move(f),{}}}} , comment{c} { SWEAR( has_targets_only()) ; }
	JobExecRpcReq( P p , VS  const& fs     ,                  S const& c={} ) : proc{p} ,                                      files{_mk_files(fs)   } , comment{c} { SWEAR( has_targets_only()) ; }
	//
	MDD _mk_files(::vector_s const& fs) {
		MDD res ;
		for( ::string const& f : fs ) res.emplace_back(f,DD()) ;
		return res ;
	}
	bool has_files       () const { return has_targets() ||  has_deps() ; }
	bool has_targets_only() const { return has_targets() && !has_deps() ; }
	bool has_deps() const {
		switch (proc) {
			case P::DepCrcs :
			case P::Deps    :
			case P::Updates : return true  ;
			default         : return false ;
		}
	}
	bool has_targets() const {
		switch (proc) {
			case P::Updates :
			case P::Targets :
			case P::Unlinks : return true  ;
			default         : return false ;
		}
	}
	// services
public :
	template<IsStream T> void serdes(T& s) {
		if (::is_base_of_v<::istream,T>) *this = JobExecRpcReq() ;
		/**/             ::serdes(s,proc     ) ;
		/**/             ::serdes(s,date     ) ;
		/**/             ::serdes(s,sync     ) ;
		if (has_files()) ::serdes(s,auto_date) ;
		if (has_files()) ::serdes(s,files    ) ;
		if (has_deps() ) ::serdes(s,das      ) ;
		/**/             ::serdes(s,comment  ) ;
	}
	// data
	P    proc      = P::None     ;
	PD   date      = PD::s_now() ;     // access date to reorder accesses during analysis
	bool sync      = false       ;
	bool auto_date = false       ;     // if true <=> files are not dated and dates must be added by probing disk (for internal job purpose, not to be sent to job_exec)
	DAs  das       ;                   // DepAccesses
	MDD  files     ;                   // file date when accessed for Deps to identify content
	S    comment   ;
} ;

struct JobExecRpcReply {
	friend ::ostream& operator<<( ::ostream& , JobExecRpcReply const& ) ;
	using Proc = JobExecRpcProc ;
	using Crc  = Hash::Crc      ;
	// cxtors & casts
	JobExecRpcReply(                                  ) = default ;
	JobExecRpcReply( Proc p                           ) : proc{p}            { SWEAR( proc!=Proc::ChkDeps && proc!=Proc::DepCrcs ) ; }
	JobExecRpcReply( Proc p , bool                 o  ) : proc{p} , ok  {o}  { SWEAR( proc==Proc::ChkDeps                        ) ; }
	JobExecRpcReply( Proc p , ::vector<Crc> const& cs ) : proc{p} , crcs{cs} { SWEAR( proc==Proc::DepCrcs                        ) ; }
	JobExecRpcReply( JobRpcReply const& jrr ) {
		switch (jrr.proc) {
			case JobProc::None    :                        proc = Proc::None    ;                      break ;
			case JobProc::ChkDeps : SWEAR(jrr.ok!=Maybe) ; proc = Proc::ChkDeps ; ok   = jrr.ok==Yes ; break ;
			case JobProc::DepCrcs :                        proc = Proc::DepCrcs ; crcs = jrr.crcs    ; break ;
			default : FAIL(jrr.proc) ;
		}
	}
	// services
	template<IsStream S> void serdes(S& s) {
		if (::is_base_of_v<::istream,S>) *this = JobExecRpcReply() ;
		::serdes(s,proc) ;
		switch (proc) {
			case Proc::ChkDeps : ::serdes(s,ok  ) ; break ;
			case Proc::DepCrcs : ::serdes(s,crcs) ; break ;
			default : ;
		}
	}
	// data
	Proc          proc = Proc::None ;
	bool          ok   = false      ;  // if proc==ChkDeps
	::vector<Crc> crcs ;               // if proc==DepCrcs
} ;