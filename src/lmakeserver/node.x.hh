// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#include "codec.hh"

#ifdef STRUCT_DECL

ENUM( Buildable
,	LongName    //                                   name is longer than allowed in config
,	DynAnti     //                                   match dependent
,	Anti        //                                   match independent
,	SrcDir      //                                   match independent (much like star targets, i.e. only existing files are deemed buildable)
,	No          // <=No means node is not buildable
,	Maybe       //                                   buildability is data dependent (maybe converted to Yes by further analysis)
,	SubSrcDir   //                                   sub-file of a SrcDir
,	Unknown
,	Yes         // >=Yes means node is buildable
,	DynSrc      //                                   match dependent
,	Src         //                                   match independent
,	Decode      //                                   file name representing a code->val association
,	Encode      //                                   file name representing a val->code association
,	SubSrc      //                                   sub-file of a src listed in manifest
,	Loop        //                                   node is being analyzed, deemed buildable so as to block further analysis
)

ENUM_1( Manual
,	Changed = Empty // >=Changed means that job is sensitive to new content
,	Ok              // file is as recorded
,	Unlnked         // file has been unlinked
,	Empty           // file is modified but is empty
,	Modif           // file is modified and may contain user sensitive info
,	Unknown
)

ENUM( NodeGoal // each action is included in the following one
,	None
,	Makable    // do whatever is necessary to assert node does/does not exist (data dependent)
,	Status     // check book-keeping, no disk access
,	Dsk        // ensure up-to-date on disk
)

ENUM( NodeMakeAction
,	Wakeup           // a job has completed
,	Makable
,	Status
,	Dsk
)

ENUM_1( NodeStatus
,	Makable = Src  // <=Makable means node can be used as dep
,	Plain          // must be first (as 0 is deemed to be a job_tgt index), node is generated by a job
,	Multi          // several jobs
,	Src            // node is a src     or a file within a src dir
,	SrcDir         // node is a src dir or a dir  within a src dir
,	None           // no job
,	Uphill         // >=Uphill means node has a buildable uphill dir, node has a regular file as uphill dir
,	Transcient     //                                                 node has a link         as uphill dir (and such a dep will certainly disappear when job is remade unless it is a static dep)
,	Unknown
)

namespace Engine {

	struct Node        ;
	struct NodeData    ;
	struct NodeReqInfo ;

	struct Target  ;
	using Targets = TargetsBase ;


	struct Dep  ;
	struct Deps ;

	static constexpr uint8_t NodeNGuardBits = 1 ; // to be able to make Target

}

#endif
#ifdef STRUCT_DEF

inline NodeGoal mk_goal(NodeMakeAction ma) {
	static constexpr NodeGoal s_tab[] {
		NodeGoal::None                  // Wakeup
	,	NodeGoal::Makable               // Makable
	,	NodeGoal::Status                // Status
	,	NodeGoal::Dsk                   // Dsk
	} ;
	static_assert(sizeof(s_tab)==N<NodeMakeAction>*sizeof(NodeGoal)) ;
	return s_tab[+ma] ;
}

inline NodeMakeAction mk_action(NodeGoal g) {
	static constexpr NodeMakeAction s_tab[] {
		{}
	,	NodeMakeAction::Makable // Makable
	,	NodeMakeAction::Status  // Status
	,	NodeMakeAction::Dsk     // Dsk
	} ;
	static_assert(sizeof(s_tab)==N<NodeGoal>*sizeof(NodeMakeAction)) ;
	SWEAR(g!=NodeGoal::None) ;
	return s_tab[+g] ;
}

namespace Engine {

	//
	// Node
	//

	struct Node : NodeBase {
		friend ::ostream& operator<<( ::ostream& , Node const ) ;
		using MakeAction = NodeMakeAction ;
		using ReqInfo    = NodeReqInfo    ;
		//
		static constexpr RuleIdx NoIdx      = -1                 ;
		static constexpr RuleIdx MaxRuleIdx = -(N<NodeStatus>+1) ;
		// statics
		static Hash::Crc s_src_dirs_crc() ;
		// static data
	private :
		static Hash::Crc _s_src_dirs_crc ;
		// cxtors & casts
		using NodeBase::NodeBase ;
	} ;

	//
	// Target
	//

	struct Target : Node {
		static_assert(Node::NGuardBits>=1) ;
		static constexpr uint8_t NGuardBits = Node::NGuardBits-1      ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		friend ::ostream& operator<<( ::ostream& , Target const ) ;
		// cxtors & casts
		Target(                       ) = default ;
		Target( Node n , Tflags tf={} ) : Node(n) , tflags{tf} {}
		// accesses
		bool static_phony() const { return ::static_phony(tflags) ; }
		// services
		constexpr ::strong_ordering operator<=>(Node const& other) const { return Node::operator<=>(other) ; }
		// data
		Tflags tflags   ;
	} ;
	static_assert(sizeof(Target)==8) ;

	//
	// Dep
	//

	struct Dep : DepDigestBase<Node> {
		friend ::ostream& operator<<( ::ostream& , Dep const& ) ;
		using Base = DepDigestBase<Node> ;
		// cxtors & casts
		using Base::Base ;
		// accesses
		::string accesses_str() const ;
		::string dflags_str  () const ;
		// services
		bool up_to_date (bool full=false) const ;
		void acquire_crc() ;
	} ;
	static_assert(sizeof(Dep)==16) ;

	union GenericDep {
		static constexpr uint8_t NodesPerDep = sizeof(Dep)/sizeof(Node) ;
		// cxtors & casts
		GenericDep(Dep const& d={}) : hdr{d} {}
		// services
		GenericDep const* next() const { return this+1+div_up(hdr.sz,GenericDep::NodesPerDep) ; }
		GenericDep      * next()       { return this+1+div_up(hdr.sz,GenericDep::NodesPerDep) ; }
		// data
		Dep hdr                 = {} ;
		Node chunk[NodesPerDep] ;
	} ;

	//
	// Deps
	//

	struct DepsIter {
		struct Digest {
			friend ::ostream& operator<<( ::ostream& , Digest const& ) ;
			NodeIdx hdr     = 0 ;
			uint8_t i_chunk = 0 ;
		} ;
		// cxtors & casts
		DepsIter(                     ) = default ;
		DepsIter( DepsIter const& dit ) : hdr{dit.hdr} , i_chunk{dit.i_chunk} {}
		DepsIter( GenericDep const* d ) : hdr{d      }                        {}
		DepsIter( Deps , Digest       ) ;
		//
		DepsIter& operator=(DepsIter const& dit) {
			hdr     = dit.hdr     ;
			i_chunk = dit.i_chunk ;
			return *this ;
		}
		// accesses
		bool operator==(DepsIter const& dit) const { return hdr==dit.hdr && i_chunk==dit.i_chunk ; }
		Digest digest  (Deps               ) const ;
		// services
		Dep const* operator->() const { return &**this ; }
		Dep const& operator* () const {
			// Node's in chunk are semanticly located before header so :
			// - if i_chunk< hdr->sz : refer to dep with no crc, flags nor parallel
			// - if i_chunk==hdr->sz : refer to header
			if (i_chunk==hdr->hdr.sz) return hdr->hdr ;
			static_cast<Node&>(tmpl) = hdr[1].chunk[i_chunk]   ;
			tmpl.accesses            = hdr->hdr.chunk_accesses ;
			return tmpl ;
		}
		DepsIter& operator++(int) { return ++*this ; }
		DepsIter& operator++(   ) {
			if (i_chunk<hdr->hdr.sz)   i_chunk++ ;                         // go to next item in chunk
			else                     { i_chunk = 0 ; hdr = hdr->next() ; } // go to next chunk
			return *this ;
		}
		// data
		GenericDep const* hdr     = nullptr                    ;           // pointer to current chunk header
		uint8_t           i_chunk = 0                          ;           // current index in chunk
		mutable Dep       tmpl    = {{}/*accesses*/,Crc::None} ;           // template to store uncompressed Dep's
	} ;

	struct Deps : DepsBase {
		// cxtors & casts
		using DepsBase::DepsBase ;
		Deps( ::vmap  <Node,Dflags> const& , Accesses ,          bool parallel ) ;
		Deps( ::vector<Node       > const& , Accesses , Dflags , bool parallel ) ;
		// accesses
		NodeIdx size() const = delete ; // deps are compressed
		// services
		DepsIter begin() const {
			GenericDep const* first = items() ;
			return {first} ;
		}
		DepsIter end() const {
			GenericDep const* last1 = items()+DepsBase::size() ;
			return {last1} ;
		}
		void assign      (            ::vector<Dep> const& ) ;
		void replace_tail( DepsIter , ::vector<Dep> const& ) ;
	} ;

}

#endif
#ifdef INFO_DEF

namespace Engine {

	struct NodeReqInfo : ReqInfo {                                              // watchers of Node's are Job's
		friend ::ostream& operator<<( ::ostream& os , NodeReqInfo const& ri ) ;
		//
		using MakeAction = NodeMakeAction ;
		//
		static constexpr RuleIdx NoIdx = Node::NoIdx ;
		static const     ReqInfo Src   ;
		// cxtors & casts
		using ReqInfo::ReqInfo ;
		// accesses
		bool done(NodeGoal ng) const { return done_>=ng   ; }
		bool done(           ) const { return done_>=goal ; }
		// services
		void reset(NodeGoal ng=NodeGoal::None) { done_ &= ng ; }
		// data
	public :
		RuleIdx  prio_idx    = NoIdx           ;                                //    16 bits, index to the first job of the current prio being or having been analyzed
		bool     single      = false           ;                                // 1<= 8 bits, if true <=> consider only job indexed by prio_idx, not all jobs at this priority
		Accesses overwritten ;                                                  // 3<= 8 bits, accesses for which overwritten file can be perceived (None if file has not been overwritten)
		Manual   manual      = Manual::Unknown ;                                // 3<= 8 bits
		Bool3    speculate   = Yes             ;                                // 2<= 8 bits, Yes : prev dep not ready, Maybe : prev dep in error
		NodeGoal goal        = NodeGoal::None  ;                                // 2<= 8 bits, asked level
		NodeGoal done_       = NodeGoal::None  ;                                // 2<= 8 bits, done level
	} ;
	static_assert(sizeof(NodeReqInfo)==24) ;                                    // check expected size

}

#endif
#ifdef DATA_DEF

namespace Engine {

	struct NodeData : DataBase {
		using Idx        = NodeIdx        ;
		using ReqInfo    = NodeReqInfo    ;
		using MakeAction = NodeMakeAction ;
		using LvlIdx     = RuleIdx        ;                                                                           // lvl may indicate the number of rules tried
		//
		static constexpr RuleIdx MaxRuleIdx = Node::MaxRuleIdx ;
		static constexpr RuleIdx NoIdx      = Node::NoIdx      ;
		// static data
		static Mutex<MutexLvl::NodeCrcDate> s_crc_date_mutex ;
		// cxtors & casts
		NodeData(                                          ) = delete ;                                               // if necessary, we must take care of the union
		NodeData( Name n , bool no_dir , bool locked=false ) : DataBase{n} {
			if (!no_dir) dir() = Node(_dir_name(),false/*no_dir*/,locked) ;
		}
		~NodeData() {
			job_tgts().pop() ;
		}
		// accesses
		Node     idx    () const { return Node::s_idx(*this) ; }
		::string name   () const { return full_name()        ; }
		size_t   name_sz() const { return full_name_sz()     ; }
		//
		bool is_decode() const { return buildable==Buildable::Decode ; }
		bool is_encode() const { return buildable==Buildable::Encode ; }
		bool is_plain () const { return !is_decode() && !is_encode() ; }
		//
		Node             & dir          ()       { SWEAR(  is_plain () , buildable ) ; return _if_plain .dir                                      ; }
		Node        const& dir          () const { SWEAR(  is_plain () , buildable ) ; return _if_plain .dir                                      ; }
		JobTgts          & job_tgts     ()       { SWEAR(  is_plain () , buildable ) ; return _if_plain .job_tgts                                 ; }
		JobTgts     const& job_tgts     () const { SWEAR(  is_plain () , buildable ) ; return _if_plain .job_tgts                                 ; }
		RuleTgts         & rule_tgts    ()       { SWEAR(  is_plain () , buildable ) ; return _if_plain .rule_tgts                                ; }
		RuleTgts    const& rule_tgts    () const { SWEAR(  is_plain () , buildable ) ; return _if_plain .rule_tgts                                ; }
		Job              & actual_job   ()       { SWEAR(  is_plain () , buildable ) ; return _if_plain .actual_job                               ; }
		Job         const& actual_job   () const { SWEAR(  is_plain () , buildable ) ; return _if_plain .actual_job                               ; }
		Tflags           & actual_tflags()       { SWEAR(  is_plain () , buildable ) ; return _actual_tflags                                      ; }
		Tflags      const& actual_tflags() const { SWEAR(  is_plain () , buildable ) ; return _actual_tflags                                      ; }
		SigDate          & date         ()       { SWEAR(  is_plain () , buildable ) ; return _if_plain .date                                     ; }
		SigDate     const& date         () const { SWEAR(  is_plain () , buildable ) ; return _if_plain .date                                     ; }
		Codec::Val       & codec_val    ()       { SWEAR(  is_decode() , buildable ) ; return _if_decode.val                                      ; }
		Codec::Val  const& codec_val    () const { SWEAR(  is_decode() , buildable ) ; return _if_decode.val                                      ; }
		Codec::Code      & codec_code   ()       { SWEAR(  is_encode() , buildable ) ; return _if_encode.code                                     ; }
		Codec::Code const& codec_code   () const { SWEAR(  is_encode() , buildable ) ; return _if_encode.code                                     ; }
		Ddate            & log_date     ()       { SWEAR( !is_plain () , buildable ) ; return is_decode()?_if_decode.log_date:_if_encode.log_date ; }
		Ddate       const& log_date     () const { SWEAR( !is_plain () , buildable ) ; return is_decode()?_if_decode.log_date:_if_encode.log_date ; }
		//
		void crc_date( Crc crc_ , SigDate const& sd ) {
			date() = sd   ;
			crc    = crc_ ;
		}
		//
		bool           has_req   ( Req                       ) const ;
		ReqInfo const& c_req_info( Req                       ) const ;
		ReqInfo      & req_info  ( Req                       ) const ;
		ReqInfo      & req_info  ( ReqInfo const&            ) const ;                                                // make R/W while avoiding look up (unless allocation)
		::vector<Req>  reqs      (                           ) const ;
		bool           waiting   (                           ) const ;
		bool           done      ( ReqInfo const& , NodeGoal ) const ;
		bool           done      ( ReqInfo const&            ) const ;
		bool           done      ( Req            , NodeGoal ) const ;
		bool           done      ( Req                       ) const ;
		//
		bool match_ok          (         ) const {                          return match_gen>=Rule::s_match_gen                             ; }
		bool has_actual_job    (         ) const {                          return is_plain() && +actual_job() && !actual_job()->rule.old() ; }
		bool has_actual_job    (Job    j ) const { SWEAR(!j ->rule.old()) ; return is_plain() && actual_job()==j                            ; }
		//
		Manual manual        (                  FileSig const& ) const ;
		Manual manual        (                                 ) const { return manual(FileSig(name())) ; }
		Manual manual_refresh( Req            , FileSig const& ) ;                                                    // refresh date if file was updated but steady
		Manual manual_refresh( JobData const& , FileSig const& ) ;                                                    // .
		Manual manual_refresh( Req            r                )       { return manual_refresh(r,FileSig(name())) ; }
		Manual manual_refresh( JobData const& j                )       { return manual_refresh(j,FileSig(name())) ; }
		//
		bool/*modified*/ refresh_src_anti( bool report_no_file , ::vector<Req> const& , ::string const& name ) ;      // Req's are for reporting only
		//
		void full_refresh( bool report_no_file , ::vector<Req> const& reqs , ::string const& name ) {
			set_buildable() ;
			if (is_src_anti()) refresh_src_anti(report_no_file,reqs,name) ;
			else               manual_refresh  (Req()                   ) ;                    // no manual_steady diagnostic as this may be because of another job
		}
		//
		RuleIdx    conform_idx(              ) const { if   (_conform_idx<=MaxRuleIdx)   return _conform_idx              ; else return NoIdx             ; }
		void       conform_idx(RuleIdx    idx)       { SWEAR(idx         <=MaxRuleIdx) ; _conform_idx = idx               ;                                 }
		NodeStatus status     (              ) const { if   (_conform_idx> MaxRuleIdx)   return NodeStatus(-_conform_idx) ; else return NodeStatus::Plain ; }
		void       status     (NodeStatus s  )       { SWEAR(+s                      ) ; _conform_idx = -+s               ;                                 }
		//
		JobTgt conform_job_tgt() const {
			if (status()==NodeStatus::Plain) return job_tgts()[conform_idx()] ;
			else                             return {}                        ;
		}
		bool conform() const {
			Job cj = conform_job_tgt() ;
			return +cj && ( cj->is_special() || has_actual_job(cj) ) ;
		}
		Bool3 ok(bool force_err=false) const {                                                 // if Maybe <=> not built
			switch (status()) {
				case NodeStatus::Plain : return No | !( force_err || conform_job_tgt()->err() ) ;
				case NodeStatus::Multi : return No                                              ;
				case NodeStatus::Src   : return No | !( force_err || crc==Crc::None           ) ;
				default                : return Maybe                                           ;
			}
		}
		Bool3 ok( ReqInfo const& cri , Accesses a=~Accesses() ) const {
			SWEAR(cri.done(NodeGoal::Status)) ;
			return ok(+(cri.overwritten&a)) ;
		}
		bool running(ReqInfo const& cri) const {
			for( Job j : conform_job_tgts(cri) )
				for( Req r : j->running_reqs() )
					if (j->c_req_info(r).step()==JobStep::Exec) return true ;
			return false ;
		}
		//
		bool is_src_anti() const {
			SWEAR(match_ok()) ;
			switch (buildable) {
				case Buildable::LongName  :
				case Buildable::DynAnti   :
				case Buildable::Anti      :
				case Buildable::SrcDir    :
				case Buildable::SubSrcDir :
				case Buildable::DynSrc    :
				case Buildable::Src       :
				case Buildable::Decode    :
				case Buildable::Encode    :
				case Buildable::SubSrc    : return true  ;
				default                   : return false ;
			}
		}
		//
		// services
		bool read(Accesses a) const {                                                          // return true <= file was perceived different from non-existent, assuming access provided in a
			if (crc==Crc::None ) return false          ;                                       // file does not exist, cannot perceive difference
			if (a[Access::Stat]) return true           ;                                       // if file exists, stat is different
			if (crc.is_lnk()   ) return a[Access::Lnk] ;
			if (crc.is_reg()   ) return a[Access::Reg] ;
			else                 return +a             ;                                       // dont know if file is a link, any access may have perceived a difference
		}
		bool up_to_date( DepDigest const& dd , bool full=false ) const {                       // only manage crc, not dates
			return crc.match( dd.crc() , full?~Accesses():dd.accesses ) ;
		}
		//
		Manual manual_wash( ReqInfo& ri , bool lazy=false ) ;
		//
		void mk_old   (                        ) ;
		void mk_src   (Buildable=Buildable::Src) ;
		void mk_src   (FileTag                 ) ;
		void mk_no_src(                        ) ;
		//
		::c_vector_view<JobTgt> prio_job_tgts     (RuleIdx prio_idx) const ;
		::c_vector_view<JobTgt> conform_job_tgts  (ReqInfo const&  ) const ;
		::c_vector_view<JobTgt> conform_job_tgts  (                ) const ;
		::c_vector_view<JobTgt> candidate_job_tgts(                ) const ;                   // all jobs above prio provided in conform_idx
		//
		void set_buildable( Req={}   , DepDepth lvl=0       ) ;                                // data independent, may be pessimistic (Maybe instead of Yes), req is for error reporing only
		void set_pressure ( ReqInfo& , CoarseDelay pressure ) const ;
		//
		void propag_speculate( Req req , Bool3 speculate ) const {
			/**/                          if (speculate==Yes         ) return ;                // fast path : nothing to propagate
			ReqInfo& ri = req_info(req) ; if (speculate>=ri.speculate) return ;
			ri.speculate = speculate ;
			_propag_speculate(ri) ;
		}
		//
		void set_infinite(::vector<Node> const& deps) ;
		//
		void make  ( ReqInfo& , MakeAction , Bool3 speculate=Yes ) ;
		void wakeup( ReqInfo& ri                                 ) { return make(ri,MakeAction::Wakeup) ; }
		//
		bool/*ok*/ forget( bool targets , bool deps ) ;
		//
		template<class RI> void add_watcher( ReqInfo& ri , Watcher watcher , RI& wri , CoarseDelay pressure ) ;
		//
		bool/*modified*/ refresh( Crc , SigDate const& ={} ) ;
		void             refresh(                          ) ;
	private :
		void         _set_buildable_raw( Req      , DepDepth                         ) ;       // req is for error reporting only
		bool/*done*/ _make_pre         ( ReqInfo&                                    ) ;
		void         _make_raw         ( ReqInfo& , MakeAction , Bool3 speculate=Yes ) ;
		void         _set_pressure_raw ( ReqInfo&                                    ) const ;
		void         _propag_speculate ( ReqInfo const&                              ) const ;
		//
		Buildable _gather_special_rule_tgts( ::string const& name                          ) ;
		Buildable _gather_prio_job_tgts    ( ::string const& name , Req   , DepDepth lvl=0 ) ;
		Buildable _gather_prio_job_tgts    (                        Req r , DepDepth lvl=0 ) {
			if (!rule_tgts()) return Buildable::No                             ;               // fast path : avoid computing name()
			else              return _gather_prio_job_tgts( name() , r , lvl ) ;
		}
		//
		void _set_match_gen(bool ok) ;
		// data
		// START_OF_VERSIONING
	public :
		struct IfPlain {
			SigDate  date       ;                                // ~40+40<128 bits,         p : production date, d : if file mtime is d, crc is valid, 40 bits : 30 years @ms resolution
			Node     dir        ;                                //  31   < 32 bits, shared
			JobTgts  job_tgts   ;                                //         32 bits, owned , ordered by prio, valid if match_ok
			RuleTgts rule_tgts  ;                                // ~20   < 32 bits, shared, matching rule_tgts issued from suffix on top of job_tgts, valid if match_ok
			Job      actual_job ;                                //  31   < 32 bits, shared, job that generated node
		} ;
		struct IfDecode {
			Ddate      log_date ;                                // ~40   < 64 bits,         logical date to detect overwritten
			Codec::Val val      ;                                //         32 bits,         offset in association file where the association line can be found
		} ;
		struct IfEncode {
			Ddate       log_date ;                               // ~40   < 64 bits,         logical date to detect overwritten
			Codec::Code code     ;                               //         32 bits,         offset in association file where the association line can be found
		} ;
		//Name   name   ;                                        //         32 bits, inherited
		Watcher  asking ;                                        //         32 bits,         last watcher needing this node
		Crc      crc    = Crc::None ;                            // ~45   < 64 bits,         disk file CRC when file's mtime was date.p. 45 bits : MTBF=1000 years @ 1000 files generated per second.
	private :
		union {
			IfPlain  _if_plain  = {} ;                           //        256 bits
			IfDecode _if_decode ;                                //         32 bits
			IfEncode _if_encode ;                                //         32 bits
		} ;
	public :
		MatchGen  match_gen:NMatchGenBits = 0                  ; //          8 bits,          if <Rule::s_match_gen => deem !job_tgts.size() && !rule_tgts && !sure
		Buildable buildable:4             = Buildable::Unknown ; //          4 bits,          data independent, if Maybe => buildability is data dependent, if Plain => not yet computed
		bool      polluted :1             = false              ; //          1 bit ,          if true <=  node was polluted produced by a non-official job or badly produced by official job
	private :
		RuleIdx _conform_idx   = -+NodeStatus::Unknown ;         //         16 bits,          index to job_tgts to first job with execut.ing.ed prio level, if NoIdx <=> uphill or no job found
		Tflags  _actual_tflags ;                                 //          8 bits,          tflags associated with actual_job
		// END_OF_VERSIONING
	} ;
	static_assert(sizeof(NodeData)==56) ;                        // check expected size

}

#endif
#ifdef IMPL

namespace Engine {

	//
	// NodeData
	//

	inline bool NodeData::has_req(Req r) const {
		return Req::s_store[+r].nodes.contains(idx()) ;
	}
	inline NodeReqInfo const& NodeData::c_req_info(Req r) const {
		::umap<Node,ReqInfo> const& req_infos = Req::s_store[+r].nodes ;
		auto                        it        = req_infos.find(idx())  ;                 // avoid double look up
		if (it==req_infos.end()) return Req::s_store[+r].nodes.dflt ;
		else                     return it->second                  ;
	}
	inline NodeReqInfo& NodeData::req_info(Req r) const {
		return Req::s_store[+r].nodes.try_emplace(idx(),NodeReqInfo(r)).first->second ;
	}
	inline NodeReqInfo& NodeData::req_info(ReqInfo const& cri) const {
		if (&cri==&Req::s_store[+cri.req].nodes.dflt) return req_info(cri.req)         ; // allocate
		else                                          return const_cast<ReqInfo&>(cri) ; // already allocated, no look up
	}
	inline ::vector<Req> NodeData::reqs() const { return Req::s_reqs(*this) ; }

	inline bool NodeData::waiting() const {
		for( Req r : reqs() ) if (c_req_info(r).waiting()) return true ;
		return false ;
	}

	inline bool NodeData::done( ReqInfo const& cri , NodeGoal na ) const {
		if (cri.done(na)) return true ;
		switch (na) {                                                                // if not actually done, report obvious cases
			case NodeGoal::None    : return true                                   ;
			case NodeGoal::Makable : return match_ok() && is_src_anti()            ;
			case NodeGoal::Status  : return match_ok() && buildable<=Buildable::No ;
			case NodeGoal::Dsk     : return false                                  ;
		DF}
	}
	inline bool NodeData::done( ReqInfo const& cri               ) const { return done(cri          ,cri.goal) ; }
	inline bool NodeData::done( Req            r   , NodeGoal ng ) const { return done(c_req_info(r),ng      ) ; }
	inline bool NodeData::done( Req            r                 ) const { return done(c_req_info(r)         ) ; }

	inline Manual NodeData::manual(FileSig const& sig) const {
		if (sig==date().sig) return Manual::Ok ;               // None and Dir are deemed identical
		Manual res = Manual::Modif ;
		if      (!sig                     ) res = Manual::Unlnked ;
		else if (sig.tag()==FileTag::Empty) res = Manual::Empty   ;
		Trace("manual",res,idx(),sig,crc,date()) ;
		return res ;
	}

	inline ::c_vector_view<JobTgt> NodeData::conform_job_tgts(ReqInfo const& cri) const { return prio_job_tgts(cri.prio_idx) ; }
	inline ::c_vector_view<JobTgt> NodeData::conform_job_tgts(                  ) const {
		// conform_idx is (one of) the producing job, not necessarily the first of the job_tgt's at same prio level
		if (status()!=NodeStatus::Plain) return {} ;
		RuleIdx prio_idx = conform_idx() ;
		Prio prio = job_tgts()[prio_idx]->rule->prio ;
		while ( prio_idx && job_tgts()[prio_idx-1]->rule->prio==prio ) prio_idx-- ; // rewind to first job within prio level
		return prio_job_tgts(prio_idx) ;
	}

	template<class RI> void NodeData::add_watcher( ReqInfo& ri , Watcher watcher , RI& wri , CoarseDelay pressure ) {
		ri.add_watcher(watcher,wri) ;
		set_pressure(ri,pressure) ;
	}

	inline void NodeData::_set_match_gen(bool ok) {
		if      (!ok                        ) { SWEAR(is_plain()                   ) ; match_gen = 0                 ; buildable = Buildable::Unknown ; }
		else if (match_gen<Rule::s_match_gen) { SWEAR(buildable!=Buildable::Unknown) ; match_gen = Rule::s_match_gen ;                                  }
	}

	inline void NodeData::set_buildable( Req req , DepDepth lvl ) { // req is for error reporting only
		if (!match_ok()) _set_buildable_raw(req,lvl) ;              // if not already set
		SWEAR(buildable!=Buildable::Unknown) ;
	}

	inline void NodeData::set_pressure( ReqInfo& ri , CoarseDelay pressure ) const {
		if (!ri.set_pressure(pressure)) return ;                                     // pressure is not significantly higher than already existing, nothing to propagate
		if (!ri.waiting()             ) return ;
		_set_pressure_raw(ri) ;
	}

	inline void NodeData::make( ReqInfo& ri , MakeAction ma , Bool3 s ) {
		if ( ma!=MakeAction::Wakeup && s>=ri.speculate && ri.done(mk_goal(ma)) ) return ; // fast path
		_make_raw(ri,ma,s) ;
	}

	inline void NodeData::refresh() {
		FileSig sig { name() } ;
		switch (manual(sig)) {
			case Manual::Ok      :                                      break ;
			case Manual::Unlnked : refresh( Crc::None  , Pdate(New) ) ; break ;
			case Manual::Empty   : refresh( Crc::Empty , sig        ) ; break ;
			case Manual::Modif   : refresh( {}         , sig        ) ; break ;
		DF}
	}

	//
	// Dep
	//

	inline bool Dep::up_to_date(bool full) const {
		return is_crc && crc().match( (*this)->crc , full?~Accesses():accesses ) ;
	}

	inline void Dep::acquire_crc() {
		if ( !is_crc && (*this)->crc.valid() && (*this)->crc!=Crc::None && sig()==(*this)->date().sig ) crc((*this)->crc) ;
	}

	//
	// Deps
	//

	inline DepsIter::DepsIter( Deps ds , Digest d ) : hdr{+ds?ds.items()+d.hdr:nullptr} , i_chunk{d.i_chunk} {}

	inline DepsIter::Digest DepsIter::digest(Deps ds) const {
		return { hdr?NodeIdx(hdr-ds.items()):0 , i_chunk } ;
	}
}

#endif
