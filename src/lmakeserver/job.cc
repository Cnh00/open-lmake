// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "time.hh"
#include "rpc_job.hh"

#include "core.hh"

using namespace Disk ;

namespace Engine {

	//
	// jobs thread
	//

	// we want to unlink dir knowing that :
	// - create_dirs will be created, so no need to unlink them
	// - keep_enclosing_dirs must be kept, together with all its recursive children
	// result is reported through in/out param to_del_dirs that is used to manage recursion :
	// - on the way up we notice that we hit a create_dirs to avoid unlinking a dir that will have to be recreated
	// - if we hit a keep_enclosing_dirs, we bounce down with a false return value saying that we must not unlink anything
	// - on the way down, we accumulate to to_del_dirs dirs if we did not bounce on a keep_enclosing_dirs and we are not a father of a create_dirs
	static bool/*ok*/ _acc_to_del_dirs( ::set_s& to_del_dirs , ::umap_s<NodeIdx> const& keep_enclosing_dirs , ::set_s const& create_dirs , ::string const& dir , bool keep=false ) {
		if (dir.empty()                      ) return true  ;                  // bounce at root, accumulating to to_del_dirs on the way down
		if (to_del_dirs        .contains(dir)) return true  ;                  // return true to indicate that above has already been analyzed and is ok, propagate downward
		if (keep_enclosing_dirs.contains(dir)) return false ;                  // return false to indicate that nothing must be unlinked here and below , propagate downward
		//
		keep |= create_dirs.contains(dir) ;                                    // set keep     to indicate that nothing must be unlinked here and above , propagate upward
		//
		if ( !_acc_to_del_dirs( to_del_dirs , keep_enclosing_dirs , create_dirs , dir_name(dir) , keep ) ) return false ;
		//
		if (!keep) to_del_dirs.insert(dir) ;
		return true ;
	}

	::vector<Node>/*report_unlink*/ Job::wash(Rule::SimpleMatch const& match_) const {
		Trace trace("wash") ;
		::vector<Node> report_unlink ;
		Rule           rule          = (*this)->rule        ;
		::vector_s     to_mk_dirs    = match_.target_dirs() ;
		::set_s        to_del_dirs   ;                                         // ordered to ensure to_del_dirs are removed deepest first
		::vector_s     to_wash       ;
		// compute targets to wash
		// handle static targets
		::vector_view_c_s sts = match_.static_targets() ;
		for( VarIdx t=0 ; t<sts.size() ; t++ ) {
			Node target{sts[t]} ;
			if ( target->crc==Crc::None                                              ) continue ;                        // no interest to wash file if it does not exist
			if (                                   rule->flags(t)[Flag::Incremental] ) continue ;                        // keep file for incremental targets
			if ( !target->has_actual_job(*this) && rule->flags(t)[Flag::Warning    ] ) report_unlink.push_back(target) ;
			to_wash.push_back(sts[t]) ;
		}
		// handle star targets
		Rule::Match full_match ;                                               // lazy evaluated, if we find any target to report_unlink
		for( Target target : (*this)->star_targets ) {
			if (target->crc==Crc::None) continue ;                             // no interest to wash file if it does not exist
			if (target.is_update()    ) continue ;                             // if reads were allowed, keep file
			if (!target->has_actual_job(*this)) {
				if (!full_match                                              ) full_match = match_ ;             // solve lazy evaluation
				if (rule->flags(full_match.idx(target.name()))[Flag::Warning]) report_unlink.push_back(target) ;
			}
			to_wash.push_back(target.name()) ;
		}
		// remove old_targets
		::set_s       to_mk_dir_set = mk_set(to_mk_dirs)     ;                 // uncomfortable on how a hash tab may work with repetitive calls to begin/erase, safer with a set
		::unique_lock lock          { _s_target_dirs_mutex } ;
		for( ::string const& t : to_wash ) {
			trace("unlink_target",t) ;
			//vvvvvvv
			unlink(t) ;
			//^^^^^^^
			_acc_to_del_dirs( to_del_dirs , _s_target_dirs , to_mk_dir_set , dir_name(t) ) ; // _s_target_dirs must protect all dirs beneath it
		}
		// create target dirs
		while (to_mk_dir_set.size()) {
			auto dir = to_mk_dir_set.cbegin() ;                                // process by starting top most : as to_mk_dirs is ordered, parent necessarily appears before child
			//    vvvvvvvvvvvvvvvvvvvvvvvv
			if (::mkdir(dir->c_str(),0755)==0) {
			//    ^^^^^^^^^^^^^^^^^^^^^^^^
				to_mk_dir_set.erase(dir) ;                                     // created, ok
			} else if (errno==EEXIST) {
				if      (is_dir(*dir)              ) to_mk_dir_set.erase(dir)  ;                            // already exists, ok
				else if (Node(*dir).manual_ok()==No) throw to_string("must unlink but is manual : ",*dir) ;
				else                                 ::unlink(dir->c_str()) ;                               // exists but is not a dir : unlink file and retry
			} else {
				::string parent = dir_name(*dir) ;
				swear_prod( (errno==ENOENT||errno==ENOTDIR) && !parent.empty() , "cannot create dir ",*dir ) ; // if ENOTDIR, a parent dir is not a dir, it will be fixed up
				to_mk_dir_set.insert(::move(parent)) ;                                                         // retry after parent is created
			}
		}
		// remove containing dirs accumulated in to_del_dirs
		::uset_s not_empty_dirs ;
		for( auto it=to_del_dirs.rbegin() ; it!=to_del_dirs.crend() ; it++ ) { // proceed in reverse order to guarantee subdirs are seen first
			::string const& dir = *it ;
			if (not_empty_dirs.contains(dir)) continue ;
			//         vvvvvvvvvvvvvvvvv
			if      (::rmdir(dir.c_str())==0) { trace("unlink_dir"          ,dir) ; }
			//         ^^^^^^^^^^^^^^^^^
			else if (errno==ENOENT          ) { trace("dir_already_unlinked",dir) ; }
			else                              { trace("dir_not_empty"       ,dir) ;
				for( ::string d=dir_name(dir) ; !d.empty() ; d=dir_name(d) ) { // no hope to unlink a dir if a sub-dir still exists
					if (not_empty_dirs.contains(d)) break ;                    // enclosing dirs are already recorded, no need to proceed
					not_empty_dirs.insert(d) ;
				}
			}
		}
		for( ::string const& dir : to_mk_dirs ) { trace("create_dir",dir) ; _s_target_dirs[dir]++ ; } // update _target_dirs once we are sure job will start
		return report_unlink ;
	}

	void Job::fill_rpc_reply( JobRpcReply& jrr , Rule::SimpleMatch const& match_ , ::vmap_ss const& rsrcs ) const {
		Rule                 r       = (*this)->rule          ;
		::vector_s const&    targets = match_.targets()       ;
		::vector_view_c<Dep> deps    = (*this)->static_deps() ;
		//
		for( auto const& [k,ef] : r->env ) jrr.env.emplace_back(k,ef.val) ;
		for( auto const& [k,i] : r->cmd_ctx ) {
			::string  var ;
			::string  str ;
			::vmap_ss dct ;
			switch (k) {
				case CmdVar::Stem    : var = r->stems    [i].first ; str        = match_.stems[i]        ;                                                                goto Str ;
				case CmdVar::Target  : var = r->targets  [i].first ; str        = targets     [i]        ;                                                                goto Str ;
				case CmdVar::Dep     : var = r->deps .dct[i].first ; str        = deps        [i].name() ;                                                                goto Str ;
				case CmdVar::Rsrc    : var = r->rsrcs.dct[i].first ; str        = rsrcs       [i].second ;                                                                goto Str ;
				case CmdVar::Stdout  :                               jrr.stdout = targets     [i]        ; if (jrr.stdout.empty()) jrr.stdout = "/dev/null" ;             continue ;
				case CmdVar::Stdin   :                               jrr.stdin  = deps        [i].name() ;                                                                continue ;
				case CmdVar::Tokens  : var = "job_tokens"          ; str        = to_string((*this)->tokens) ;                                                            goto Str ;
				case CmdVar::Stems   : var = "stems"     ; for( VarIdx j=0 ; j<r->n_static_stems ; j++ ) dct.emplace_back(r->stems    [j].first,match_.stems[j]       ) ; goto Dct ;
				case CmdVar::Targets : var = "targets"   ; for( VarIdx j=0 ; j<r->targets.size() ; j++ ) dct.emplace_back(r->targets  [j].first,targets     [j]       ) ; goto Dct ;
				case CmdVar::Deps    : var = "deps"      ; for( VarIdx j=0 ; j<r->n_deps ()      ; j++ ) dct.emplace_back(r->deps .dct[j].first,deps        [j].name()) ; goto Dct ;
				case CmdVar::Rsrcs   : var = "resources" ; for( VarIdx j=0 ; j<r->n_rsrcs()      ; j++ ) dct.emplace_back(r->rsrcs.dct[j].first,rsrcs       [j].second) ; goto Dct ;
				default : FAIL(k) ;
			}
		Str :
			if (r->is_python) append_to_string( jrr.script , var ," = ", mk_py_str(str) ,'\n') ;
			else              jrr.env.emplace_back(var,str) ;
			continue ;
		Dct :
			// XXX : dont know how to pass a dict in environment
			if (r->is_python) {
				const char* sep = "" ;
				/**/                             append_to_string( jrr.script , var ," = {"                                      ) ;
				for( auto const& [k,v] : dct ) { append_to_string( jrr.script , '\n',sep,'\t', mk_py_str(k) ," : ", mk_py_str(v) ) ; sep = "," ; }
				/**/                             append_to_string( jrr.script ,"\n}\n"                                           ) ;
			}
			continue ;
		}
		jrr.script += r->script ;
		if (r->is_python) {
			if (r->script.back()!='\n') jrr.script += '\n'                                                                ;
			/**/                        jrr.script += "rc = cmd()\nif rc : raise RuntimeError(f'cmd() return rc={rc}')\n" ;
		}
		//
		jrr.targets.reserve(targets.size()) ;
		for( VarIdx t=0 ; t<targets.size() ; t++ ) if (!targets[t].empty()) jrr.targets.emplace_back( targets[t] , r->flags(t) ) ;
		if (r->has_stars) for( Dep d : deps ) jrr.force_deps.push_back(d.name()) ;                                                 // to ensure static deps will not match star targets
	}

	void Job::end_exec() const {
		::unique_lock lock(_s_target_dirs_mutex) ;
		for( ::string const& d : match().target_dirs() ) {
			auto it = _s_target_dirs.find(d) ;
			SWEAR(it!=_s_target_dirs.end()) ;
			if (it->second==1) _s_target_dirs.erase(it) ;
			else               it->second--             ;
		}
	}

	//
	// main thread
	//

	//
	// JobTgts
	//

	::ostream& operator<<( ::ostream& os , JobTgts const jts ) {
		return os<<jts.view() ;
	}

	//
	// JobReqInfo
	//

	::ostream& operator<<( ::ostream& os , JobReqInfo const& ri ) {
		return os<<"JRI(" << ri.req <<','<< ri.action <<','<< ri.lvl <<','<< ri.n_wait <<')' ;
	}

	//
	// Job
	//

	::ostream& operator<<( ::ostream& os , Job const j ) {
		os << "J(" ;
		if (+j) os << +j ;
		return os << ')' ;
	}
	::ostream& operator<<( ::ostream& os , JobTgt const jt ) {
		if (!jt) return os << "JT()" ;
		os << "JobTgt(" << Job(jt) ;
		if (jt.is_sure()) os << ",sure" ;
		return os << ')' ;
	}

	::shared_mutex    Job::_s_target_dirs_mutex ;
	::umap_s<NodeIdx> Job::_s_target_dirs       ;

	Job::Job( RuleTgt rule_tgt , ::string const& target , DepDepth lvl ) {
		Trace trace("Job",rule_tgt,target,lvl) ;
		Rule::Match match_{rule_tgt,target} ;
		if (!match_) { trace("no_match") ; return ; }
		vector<Node> deps ;
		try                     { deps = mk_vector<Node>(match_.deps()) ; }
		catch (::string const&) { trace("no_dep_subst") ; return ;        }
		for( Node d : deps ) {
			//vvvvvvvvvvvvvvvvvv
			d.set_buildable(lvl) ;
			//^^^^^^^^^^^^^^^^^^
			if (d->buildable==No) { trace("no_dep",d) ; return ; }
		}
		//      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		*this = Job( match_.name(),Dflt , rule_tgt,deps,match_.tokens() ) ;
		//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		trace("found",*this) ;
	}

	::string Job::ancillary_file(::string const& pfx) const {
		::string str        = to_string('0',+*this) ;                                                             // ensure size is even as we group by 100
		bool     skip_first = str.size()&0x1        ;                                                             // need initial 0 if required to have an even size
		::string res        = pfx                   ; res.reserve( res.size() + str.size() + str.size()/2 + 1 ) ; // 1.5*str.size() as there is a / for 2 digits + final _
		size_t   i          ;
		for( i=skip_first ; i<str.size()-1 ; i+=2 ) { res.push_back('/') ; res.append(str,i,2) ; } // create a dir hierarchy with 100 files at each level
		res.push_back('_') ;                                                                       // avoid name clashes with directories
		return res ;
	}

	::vector<Req> Job::running_reqs() const {                                               // sorted by start
		::vector<Req> res ; res.reserve(Req::s_reqs_by_start.size()) ;                      // pessimistic, so no realloc
		for( Req r : Req::s_reqs_by_start ) if (c_req_info(r).running()) res.push_back(r) ;
		return res ;
	}

	::vector<Req> Job::old_done_reqs() const {                                 // sorted by start
		::vector<Req> res ; res.reserve(Req::s_reqs_by_start.size()) ;         // pessimistic, so no realloc
		for( Req r : Req::s_reqs_by_start ) {
			if (c_req_info(r).running()) break ;
			if (c_req_info(r).done()   ) res.push_back(r) ;
		}
		return res ;
	}

	void Job::report_start( ReqInfo& ri , bool force ) const {
		if ( !force && ri.start_reported ) return ;
		ri.req->audit_job(Color::HiddenNote,"start",*this) ;
		ri.start_reported = true ;
	}
	void Job::report_start() const {
		Trace trace("report_start",*this) ;
		for( Req req : running_reqs() ) report_start(req_info(req)) ;
	}

	void Job::started( bool report , ::vector<Node> const& report_unlink ) {
		Trace trace("started",*this) ;
		for( Req req : running_reqs() ) {
			ReqInfo& ri = req_info(req) ;
			if (report) report_start(ri,true/*force*/) ;
			else        ri.start_reported = false ;
			size_t w = 0 ;
			for( Node t : report_unlink ) if (t->has_actual_job()) w = ::max( w , t->actual_job_tgt->rule->user_name().size() ) ;
			for( Node t : report_unlink ) {
				if (t->has_actual_job()) req->audit_node( Color::Warning , to_string("unlinked target (generated by ",::setw(w),t->actual_job_tgt->rule->user_name(),')') , t , 1 ) ;
				else                     req->audit_node( Color::Note    , to_string("unlinked target (obsolete)"    ,::setw(w?4+w+1:0),""                              ) , t , 1 ) ;
			}
			if (ri.lvl==ReqInfo::Lvl::Queued) {
				req->stats.cur(ReqInfo::Lvl::Queued)-- ;
				req->stats.cur(ReqInfo::Lvl::Exec  )++ ;
				ri.lvl = ReqInfo::Lvl::Exec ;
			}
		}
	}

	void Job::premature_end( Req req , bool report ) {
		Trace trace("premature_end",*this,req,STR(report)) ;
		ReqInfo& ri = req_info(req) ;
		ri.missing_audit = MissingAudit::No ;
		make( ri , RunAction::None , {}/*reason*/ , MakeAction::PrematureEnd ) ;
		if (report) req->audit_job(Color::Note,"continue",*this) ;
		req.chk_end() ;
	}

	void Job::not_started() {
		Trace trace("not_started",*this) ;
		for( Req req : running_reqs() ) premature_end(req,false/*report*/) ;
	}

	::string Job::s_reason_str(JobReason reason) {
		if (reason.tag<JobReasonTag::HasNode) return           JobReasonTagStrs[+reason.tag]                                 ;
		else                                  return to_string(JobReasonTagStrs[+reason.tag]," : ",Node(reason.node).name()) ;
	}

	// answer to job execution requests
	JobRpcReply Job::job_info( JobProc proc , ::vmap_s<DepDigest> const& deps ) const {
		::vector<Req> reqs = running_reqs() ;
		Trace trace("job_info",proc,deps.size()) ;
		if (reqs.empty()) return proc ;                                        // if job is not running, it is too late
		//
		switch (proc) {
			case JobProc::DepCrcs : {
				::vector<Crc> res ; res.reserve(deps.size()) ;
				for( auto [dn,_] : deps ) res.push_back(Node(dn)->crc) ;
				return {proc,res} ;
			}
			case JobProc::ChkDeps : {
				for( auto [dn,_] : deps ) {
					Node dep { dn }  ;
					bool err = false ;
					for( Req req : reqs ) {
						// we do not need dep for our purpose, but it will soon be necessary, it is simpler just to call plain make()
						// use Dsk because file must be present for this job
						NodeReqInfo const& cdri = dep.make( dep.c_req_info(req) , RunAction::Dsk ) ;
						// if dep is waiting for any req, stop analysis as it is complicated what we want to rebuild after
						// and there is no loss of parallelism as we do not wait for completion before doing a full analysis in make()
						if (cdri.waiting()) {
							trace("dep",dep,"waiting",dn) ;
							return {proc,Maybe} ;
						}
						err |= dep.err(cdri) ;
					}
					trace("dep",dep,STR(err),dn) ;
					if (err) return {proc,No} ;
				}
				return {proc,Yes} ;
			}
			default : FAIL(proc) ;
		}
	}

	void Job::live_out(::string const& txt) const {
		for( Req r : running_reqs() ) {
			ReqInfo& ri = req_info(r) ;
			if (!ri.live_out) continue ;
			report_start(ri) ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			r->audit_info(Color::None,txt,1) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
	}

	bool/*modified*/ Job::end( ProcessDate start , JobDigest const& digest ) {
		Status            status       = digest.status                                      ; // status will be modified, need to make a copy
		bool              err          = status>=Status::Err                                ;
		bool              killed       = status<=Status::Killed                             ;
		JobReason         local_reason = killed ? JobReasonTag::Killed : JobReasonTag::None ;
		bool              any_modified = false                                              ;
		Rule              rule         = (*this)->rule                                      ;
		::vector<pair_ss> analysis_err ;
		//
		SWEAR( status!=Status::New && !JobData::s_frozen(status) ) ;           // we just executed the job, it can be neither new nor frozen
		SWEAR(!rule.is_special()) ;
		//
		switch (status) {
			case Status::Lost    : local_reason = JobReasonTag::Lost    ; break ;
			case Status::Killed  : local_reason = JobReasonTag::Killed  ; break ;
			case Status::ChkDeps : local_reason = JobReasonTag::ChkDeps ; break ;
			case Status::Garbage :                                        break ; // Garbage is caught as a default message is none else is available
			default              : SWEAR(status>Status::Garbage) ;                // ensure we have not forgotten a case
		}
		//
		(*this)->end_date = ProcessDate::s_now()                            ;
		(*this)->status   = status<=Status::Garbage ? status : Status::Lost ;  // ensure we cannot appear up to date while working on data
		fence() ;
		//
		Trace trace("end",*this,status) ;
		//
		// handle targets
		//
		auto report_missing_target = [&](::string const& tn)->void {
			FileInfo fi{tn} ;
			analysis_err.emplace_back( to_string("missing target",(+fi?" (existing)":fi.tag==FileTag::Dir?" (dir)":"")," :") , tn ) ;
		} ;
		::uset<VarIdx> seen_static_targets ;

		for( UNode t : (*this)->star_targets ) if (t->has_actual_job(*this)) t->actual_job_tgt.clear() ; // ensure targets we no more generate do not keep pointing to us

		::vector<Target> star_targets ; if (rule->has_stars) star_targets.reserve(digest.targets.size()) ; // typically, there is either no star targets or most of them are stars
		for( auto const& [tn,td] : digest.targets ) {
			Flags flags  = td.tgt_idx!=Rule::NoVar ? rule->flags(td.tgt_idx) : UnexpectedFlags ;
			UNode target { tn }                                                                ;
			Crc   crc    = td.write ? td.crc : target->crc                                     ;
			//
			if ( !flags[Flag::ManualOk] && td.write && target->is_src() ) {
				err = true ;
				if (td.crc==Crc::None) analysis_err.emplace_back("unexpected unlink of source",tn) ;
				else                   analysis_err.emplace_back("unexpected write to source" ,tn) ;
			}
			if (
				td.write                                                       // we actually wrote
			&&	target->has_actual_job() && !target->has_actual_job(*this)     // there is another job
			&&	target->actual_job_tgt->end_date>start                         // dates overlap, which means both jobs were running concurrently (we are the second to end)
			) {
				Job    aj       = target->actual_job_tgt                                          ;
				VarIdx aj_idx   = aj.match().idx(target.name())                                   ; // this is expensive, but pretty exceptional
				Flags  aj_flags = aj_idx==Rule::NoVar ? UnexpectedFlags : aj->rule->flags(aj_idx) ;
				trace("clash",*this,flags,aj,aj_idx,aj_flags,target) ;
				// /!\ This may be very annoying !
				//     Even completed Req's may have been poluted as at the time t->actual_job_tgt completed, it was not aware of the clash.
				//     Putting target in clash_nodes will generate a frightening message to user asking to relaunch all concurrent commands, even past ones.
				//     Note that once we have detected the frightening situation and warned the user, we do not care masking further clashes by overwriting actual_job_tgt.
				if (flags   [Flag::Crc]) local_reason |= {JobReasonTag::ClashTarget,+target} ; // if we care about content, we must rerun
				if (aj_flags[Flag::Crc]) for( Req r : reqs() ) r->clash_nodes.insert(target) ; // if actual job cares about content, we have the annoying case mentioned above
			}
			if ( !flags[Flag::Incremental] && target->read(td.das) && target->crc!=Crc::None ) {
				local_reason |= {JobReasonTag::PrevTarget,+target} ;
			}
			if (crc==Crc::None) {
				if (!RuleData::s_sure(flags)) continue ;                       // if we are not sure, a target is not generated if it does not exist
				if ( !flags[Flag::Star] && !flags[Flag::Optional] ) {
					err = true ;
					report_missing_target(tn) ;
				}
			}
			if ( td.write && crc!=Crc::None && !flags[Flag::Write] ) {
				err = true ;
				analysis_err.emplace_back("unexpected write to",tn) ;
			}
			//
			if (flags[Flag::Star]) star_targets.emplace_back( target , flags[Flag::Incremental] ) ;
			else                   seen_static_targets.insert(td.tgt_idx)                         ;
			//
			bool modified = false ;
			FileInfoDate fid{tn} ;
			if (!td.write) {
				if ( flags[Flag::ManualOk] && flags[Flag::Incremental] && target.manual_ok(fid)!=Yes ) crc = {tn,g_config.hash_algo} ;
				else                                                                                   goto NoRefresh ;
			}
			//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			modified = target.refresh( fid.tag==FileTag::Lnk , crc , fid.date_or_now() ) ;
			//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		NoRefresh :
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			target->actual_job_tgt = { *this , RuleData::s_sure(flags) } ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			any_modified |= modified && flags[Flag::Match] ;
			trace("target",tn,flags,td,STR(modified),status) ;
		}
		if (seen_static_targets.size()<rule->n_static_targets) {               // some static targets have not been seen
			Rule::Match       match_         = simple_match()          ;       // match_ must stay alive as long as we use static_targets
			::vector_view_c_s static_targets = match_.static_targets() ;
			for( VarIdx t=0 ; t<rule->n_static_targets ; t++ ) {
				if (seen_static_targets.contains(t)) continue ;
				Flags flags = rule->flags(t) ;
				UNode tu    { static_targets[t] }   ;
				tu->actual_job_tgt = { *this , true/*is_sure*/ } ;
				//                             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				if (!flags[Flag::Incremental]) tu.refresh( false/*is_lnk*/ , Crc::None , DiskDate::s_now() ) ; // if incremental, target is preserved, else it has been washed at start time
				//                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				if (!flags[Flag::Optional]) {
					err = true ;
					if (status==Status::Ok) report_missing_target(static_targets[t]) ; // only report if job was ok, else it is quite normal
				}
			}
		}
		::sort(star_targets) ;                                                 // ease search in targets
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		(*this)->star_targets.assign(star_targets) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		//
		// handle deps
		//
		if (!killed) {                                                         // if killed, old deps are better than new ones, keep them
			DiskDate             db_date        ;
			::vector_view<Dep>   static_dep_vec = (*this)->static_deps()       ;
			::umap<Node,NodeIdx> static_deps_   ;
			::vector<Dep>        dep_vector     ;                                dep_vector.reserve(digest.deps.size()) ; // typically, static deps are all accessed
			::uset<Node>         old_deps       = mk_uset<Node>((*this)->deps) ;
			//

			auto update_dep = [&]( const char* tag , Dep& dep , DepDigest const& dd ) -> void {
				//                vvvvvvvvvvvvvvvvv
				if (dd.garbage) { dep.crc ({}     ) ; local_reason |= {JobReasonTag::DepNotReady,+dep} ; } // garbage : force unknown crc
				else            { dep.date(dd.date) ;                                                    } // date will be transformed into crc in make if possible
				//                ^^^^^^^^^^^^^^^^^
				trace(tag,dep,dd,dep->db_date()) ;
			} ;
			auto process_dep = [&]( const char* tag , bool access , Node d , DepDigest const& dd , DepInfo skipped=DepInfo::Parallel ) -> void {
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				dep_vector.emplace_back( d , dd.info|skipped , old_deps.contains(d) ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				if (access) update_dep(tag,dep_vector.back(),dd) ;
				db_date = ::max(db_date,d->db_date()) ;
			} ;
			// generate deps by putting static deps first, but actual access info is from digest.deps
			for( NodeIdx d=0 ; d<static_dep_vec.size() ; d++ ) {
				process_dep( "static_dep" , false/*access*/ , static_dep_vec[d] , DepInfo::Parallel ) ; // do as if no access were performed (and fix during deps analysis right below)
				static_deps_[static_dep_vec[d]] = d ;
			}
			DepInfo skipped_dep_info = DepInfo::Parallel ;
			for( auto const& [dn,dd] : digest.deps ) {
				Node d{dn} ;
				auto it = static_deps_.find(d) ;
				if (it!=static_deps_.end()) { update_dep ( "static_dep_update" , dep_vector[it->second] , dd                    ) ; skipped_dep_info |= dd.info           ; }
				else                        { process_dep( "hidden_dep"        , true/*access*/ , d     , dd , skipped_dep_info ) ; skipped_dep_info  = DepInfo::Parallel ; }
			}
			(*this)->deps.assign(dep_vector) ;
			if (any_modified) (*this)->db_date = db_date ;
		}
		//
		// wrap up
		//
		switch (status) {
			case Status::Ok      : if ( !digest.stderr.empty() && !rule->allow_stderr ) { analysis_err.emplace_back("non-empty stderr",::string()) ; err = true ; } break ;
			case Status::Timeout :                                                      { analysis_err.emplace_back("timeout"         ,::string()) ;              } break ;
			default : ;
		}
		//
		(*this)->exec_ok(true) ;                                               // effect of old cmd has gone away with job execution
		fence() ;
		//                      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if      (+local_reason) (*this)->status = ::min(status,Status::Garbage) ; // only update status once every other info is set in case of crash and avoid transforming garbage into Err
		else if (err          ) (*this)->status = ::max(status,Status::Err    ) ; // .
		else                    (*this)->status =       status                  ; // .
		//                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		bool          report_stats  = status==Status::Ok              ;
		::vector<Req> running_reqs_ = running_reqs()                  ;
		CoarseDelay   old_exec_time = (*this)->best_exec_time().first ;
		if (report_stats) {
			SWEAR(+digest.stats.total) ;
			(*this)->exec_time = digest.stats.total ;
			rule.new_job_exec_time( digest.stats.total , (*this)->tokens ) ;
		}
		for( Req req : running_reqs_ ) req_info(req).lvl = JobLvl::End ;       // we must not appear as Exec while other reqs are analysing or we will wrongly think job is on going
		for( Req req : running_reqs_ ) {
			ReqInfo& ri = req_info(req) ;
			trace("req_before",local_reason,status,ri) ;
			ri.missing_audit = MissingAudit::No ;
			// we call wakeup_watchers ourselves once reports are done to avoid anti-intuitive report order
			//                 vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			JobReason reason = make( ri , RunAction::Status , local_reason , MakeAction::End , &old_exec_time , false/*wakeup_watchers*/ ) ;
			//                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (status<=Status::Garbage) reason |= JobReasonTag::Garbage ;                                                                 // default message
			if (ri.done()) {
				if (reason.has_err()) audit_end( {} , ri , {}            , {{s_reason_str(reason),{}}} , any_modified , digest.stats.total ) ; // report exec time even if not recording it
				else                  audit_end( {} , ri , digest.stderr , analysis_err                , any_modified , digest.stats.total ) ; // .
				trace("wakeup_watchers",ri) ;
				ri.wakeup_watchers() ;
			} else {
				audit_end( +local_reason?"":"may_" , ri , {} , {{s_reason_str(reason),{}}} , any_modified , digest.stats.total ) ; // report 'rerun' rather than status
				ri.missing_audit = any_modified?MissingAudit::Modified:MissingAudit::Steady ;                                      // report status as soon as avail
			}
			trace("req_after",ri) ;
			req.chk_end() ;
		}
		trace("summary",*this) ;
		return true ;
	}

	void Job::audit_end( ::string const& pfx , ReqInfo const& cri , ::string const& stderr , ::vector<pair_ss> const& analysis_err , bool modified , Delay exec_time ) const {
		Req       req    = cri.req            ;
		::string  step   ;
		Color     color  = Color::Ok          ;
		JobReport jr     = JobReport::Unknown ;
		if      ((*this)->status==Status::Killed) { step = mk_snake((*this)->status) ; color = Color::Err  ; }
		else if (req->zombie                    ) { step = "completed"               ; color = Color::Note ; }
		else {
			if      (!cri.done()                             ) { jr = JobReport::Rerun  ; step = mk_snake(jr                 ) ;                      color = Color::Note    ; }
			else if ((*this)->run_status!=RunStatus::Complete) { jr = JobReport::Failed ; step = mk_snake((*this)->run_status) ;                      color = Color::Err     ; }
			else if ((*this)->status    ==Status   ::Timeout ) { jr = JobReport::Failed ; step = mk_snake((*this)->status    ) ;                      color = Color::Err     ; }
			else if ((*this)->err()                          ) { jr = JobReport::Failed ; step = mk_snake(jr                 ) ;                      color = Color::Err     ; }
			else if (modified                                ) { jr = JobReport::Done   ; step = mk_snake(jr                 ) ; if (!stderr.empty()) color = Color::Warning ; }
			else                                               { jr = JobReport::Steady ; step = mk_snake(jr                 ) ;                                               }
			req->stats.ended(jr)++ ;
			req->stats.jobs_time[cri.done()/*useful*/] += exec_time ;
		}
		if (!pfx.empty()) step = pfx+step ;
		Trace trace("audit_end",color,step,*this,cri,STR(modified)) ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		req->audit_job(color,step,*this,exec_time) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (jr==JobReport::Unknown) return ;
		::string err ;
		for( auto const& [pfx,file] : analysis_err ) {
			//                vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			if (file.empty()) req->audit_info(Color::Note,pfx,     1) ;
			else              req->audit_node(Color::Note,pfx,file,1) ;
			//                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
		req->audit_stderr(stderr,(*this)->rule->stderr_len) ;
	}

	void Job::_set_pressure_raw(ReqInfo& ri , CoarseDelay pressure ) const {
		using Lvl = ReqInfo::Lvl ;
		Trace("set_pressure",*this,ri,pressure) ;
		Req         req          = ri.req ;
		CoarseDelay dep_pressure = ri.pressure + (*this)->best_exec_time().first ;
		switch (ri.lvl) {
			//                                                                                                                 vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			case Lvl::Dep    : for( Dep const& d : (*this)->deps.subvec(ri.dep_lvl) ) { if (d.info==DepInfo::Critical) break ; d.set_pressure(d.req_info(req),dep_pressure) ; } break ;
			case Lvl::Queued :                                                          Backend::s_set_pressure( (*this)->rule->backend , +*this , +req , dep_pressure ) ;      break ;
			//                                                                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			default : ;
		}
	}

	ENUM(DepState
	,	Ok
	,	DanglingModif                                                          // modified dep has been seen but still processing parallel deps
	,	Modif
	,	Err
	,	MissingStatic
	)

	static inline bool _inc_cur( Req req , JobLvl jl , int inc) {
		if (jl==JobLvl::None) return false ;
		JobIdx& stat = req->stats.cur(jl==JobLvl::End?JobLvl::Exec:jl) ;
		if (inc<0) SWEAR(stat>=JobIdx(-inc)) ;
		stat += inc ;
		return jl!=JobLvl::Done ;
	}
	JobReason Job::_make_raw( ReqInfo& ri , RunAction run_action , JobReason reason , MakeAction make_action , CoarseDelay const* old_exec_time , bool wakeup_watchers ) {
		using Lvl = ReqInfo::Lvl ;
		Lvl       before_lvl = ri.lvl ;                                        // capture previous state before any update
		//
		ri.update( run_action , make_action , *this ) ;
		if (ri.waiting()) return reason ;                                      // we may have looped in which case stats update is meaningless and may fail()
		//
		Req     req     = ri.req         ;
		Rule    rule    = (*this)->rule  ;
		Special special = rule.special() ;
		//
		Trace trace("Jmake",*this,ri,before_lvl,run_action,reason,make_action,old_exec_time,STR(wakeup_watchers)) ;
		if (ri.done(ri.action)) goto Wakeup ;
		for (;;) {                                                                      // loop in case analysis must be restarted (only in case of flash execution)
			DepState    dep_state     = DepState::Ok                                  ;
			bool        sure          = true                                          ;
			CoarseDelay dep_pressure  = ri.pressure + (*this)->best_exec_time().first ;
			Idx         n_deps        = rule->no_deps ? 0 : (*this)->deps.size()      ;
			Idx         n_static_deps = (*this)->n_static_deps()                      ;
			//
			RunAction dep_action = req->options.flags[ReqFlag::Archive] ? RunAction::Dsk : RunAction::Status ;
			//
			Status status = (*this)->status ;
			if (status<=Status::Garbage) ri.action = RunAction::Run ;
			//
			if (make_action==MakeAction::End) { dep_action = RunAction::Dsk ; ri.dep_lvl  = 0 ; } // if analysing end of job, we need to be certain of presence of all deps on disk
			if (ri.action  ==RunAction::Run ) { dep_action = RunAction::Dsk ;                   } // if we must run the job , .
			//
			switch (ri.lvl) {
				case Lvl::None :
					if ( rule->force && ri.action>=RunAction::Status ) {       // only once, not in case of analysis restart
						reason     |= JobReasonTag::Force ;
						ri.action   = RunAction::Run      ;
						dep_action  = RunAction::Dsk      ;
					}
					// process command like a dep in parallel with static_deps
					if ( !(*this)->exec_ok() || (req->options.flags[ReqFlag::ForgetOldErrors]&&(*this)->status==Status::Err) ) {
						dep_state   = DepState::DanglingModif                                     ; // a new command is like a modif, new rsrcs may transform error into ok
						reason     |=
							!(*this)->cmd_ok () ? JobReasonTag::Cmd
						:	!(*this)->exec_ok() ? JobReasonTag::Rsrcs
						:	                      JobReasonTag::OldError
						;
						ri.action   = RunAction::Run                                              ; // ensure analysis is done at the right level
						dep_action  = RunAction::Dsk                                              ;
					}
					ri.lvl = Lvl::Dep ;
					if (JobData::s_frozen(status)) {
						ri.action = RunAction::Run ;                           // ensure crc are updated, like for sources
						break ;
					}
				/*fall through*/
				case Lvl::Dep : {
					bool restarted = false ;
					Dep  sentinel  { Node() , DepInfo::Critical } ;
					for ( NodeIdx i_dep=ri.dep_lvl ;;) {
						SWEAR(i_dep<=n_deps) ;
						bool is_static = i_dep<n_static_deps ;
						Dep& d         = i_dep<n_deps        ? (*this)->deps[i_dep] : sentinel ;
						//
						if ( DepInfo di=d.info ; di!=DepInfo::Parallel ) {
							if (dep_state==DepState::DanglingModif) dep_state = DepState::Modif ;
							if (di==DepInfo::Critical) {
								// if we restarted, deps before current criticity level will just be regenerated if necessary
								// so they will not change and in particular will not change the list of deps at lower criticity level, so we can analyse them all at once
								if ( !restarted && ri.waiting() ) break ;      // unless restarted, stop analysis if something *may* change at a given criticity level
								if (dep_state!=DepState::Ok) {
									if (dep_state==DepState::Modif) {
										(*this)->deps.shorten_by(n_deps-i_dep) ; // critical modif : ignore non-critical
										n_deps = i_dep ;
										// we may have to restart dep analysis as we now have to ensure dep presence on disk
										// check before checking for waiting() as if we are waiting a dep, this does not prevent to regenerate another one if necessary
										// and waiting the same dep twice is harmless
										SWEAR(ri.action>RunAction::Makable) ;  // there is no critical sections in static deps and Makable only looks at static deps
										ri.action = RunAction::Run ;
										if (dep_action<RunAction::Dsk) {
											dep_action = RunAction::Dsk ;
											dep_state  = DepState::Ok   ;
											i_dep      = 0              ;
											restarted  = true           ;
											continue ;
										}
									}
									break ;                                    // in all cases, stop analysis if something *did* change at a fiven criticity level
								}
								if (!ri.waiting()) ri.dep_lvl = i_dep ;        // all is ok at this criticity level, next time, restart analysis after this
								if (i_dep==n_deps) break              ;        // we are done
							}
							if ( !is_static && ri.action==RunAction::Makable ) break ; // whether we are makable only depends on static deps
						}
						i_dep++ ;
						//
						Node::ReqInfo const* cdri = &d.c_req_info(req) ;       // avoid allocating req_info as long as not necessary
						//
						if (cdri->waiting()) {
							reason |= {JobReasonTag::DepNotReady,+d} ;
						}
						d.acquire_crc() ;                                                      // 1st chance : before calling make as it can be destroyed in case of flash execution
						ri.n_wait++ ;                                                          // in case of recursion loop, we want to appear waiting (loop will be caught because of no job on going)
						if ( special==Special::Req && req->options.flags[ReqFlag::LiveOut] ) { // ask live output for last level if user asked it
							Node::ReqInfo& dri = d.req_info(*cdri) ; cdri = &dri ;             // refresh cdri in case dri allocated a new one
							dri.live_out = true ;
						}
						//      vvvvvvvvvvvvvvvvvvvvvvvvvvvv
						cdri = &d.make( *cdri , dep_action ) ;                 // refresh cdri if make changed it
						//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						ri.n_wait-- ;                                          // restore
						if ( is_static && sure ) {
							if      (d->buildable!=Yes   ) sure = false ;       // buildable is better after make()
							else if (i_dep==n_static_deps) (*this)->mk_sure() ; // improve sure on last static dep (sure is pessimistic)
						}
						//
						Bool3 mark    = No                                   ; // XXX : transform into bool overwritten
						bool  makable = d->makable(special==Special::Uphill) ; // sub-files of makable dir are not buildable, except for Uphill so that sub-sub-files are not buildable
						//
						if (cdri->waiting()) {
							reason |= {JobReasonTag::DepOutOfDate,+d} ;
							Node::ReqInfo& dri = d.req_info(*cdri) ; cdri = &dri ; // refresh cdri in case dri allocated a new one
							d.add_watcher(dri,*this,ri,dep_pressure) ;
							goto Continue ;
						}
						SWEAR(d.done(*cdri)) ;                                 // after having called make, d must be either waiting or done
						d.acquire_crc() ;                                      // 2nd chance : after having called make as if dep is steady (typically a source), crc may have been computed
						if (dep_state>=DepState::Modif) goto Continue ;
						if ( is_static && !makable ) {                         // uphill rule is the only rule to accept uphill dep
							dep_state  = DepState::MissingStatic             ;
							reason    |= {JobReasonTag::StaticDepMissing,+d} ;
							trace("missing_static",d) ;
							goto Continue ;
						}
						if (cdri->err!=No) {                                   // dep is already in error and has been reported to user
							dep_state = DepState::Err ;
							if (cdri->err==Yes) reason |= {JobReasonTag::DepErr        ,+d} ;
							else                reason |= {JobReasonTag::DepOverwritten,+d} ;
							goto Continue ;
						}
						if (!makable) {
							bool seen_existing ;
							switch (d.is_date) {
								case No    : seen_existing = d.crc()!=Crc::None ; break ;
								case Maybe : seen_existing = false              ; break ;
								case Yes   : seen_existing = +d.date()          ; break ;
								default : FAIL(d.is_date) ;
							}
							if (seen_existing) {
								if (is_target(d.name())) {                                                      // file still exists, still dangling
									req->audit_node(Color::Err ,"dangling"          ,d  ) ;
									req->audit_node(Color::Note,"consider : git add",d,1) ;
									trace("dangling",d) ;
									mark = Yes ; goto MarkDep ;
								} else {
									d.crc({}) ;                                // file does not exist any more, it has been removed
								}
							}
						}
						if (d->err()) {
							trace("dep_err",d) ;
							mark = Yes ; goto Err ;
						}
						if (
							( d.is_date==Yes                                          ) // if still waiting for a crc here, it will never come
						||	( d.known && make_action==MakeAction::End && !d.has_crc() ) // when ending a job, known deps should have a crc
						) {
							if (is_target(d.name())) {                         // file still exists, still manual
								if (d->is_src()) goto Overwriting ;
								for( Job j : d.conform_job_tgts(*cdri) )
									for( Req r : j.running_reqs() )
										if (j.c_req_info(r).lvl==Lvl::Exec) goto Overwriting ;
								req->audit_node(Color::Err,"manual",d) ;                       // well, maybe a job is writing to d as an unknown target, but we then cant distinguish
								trace("manual",d) ;
								mark = Yes ; goto MarkDep ;
							Overwriting :
								req->audit_node(Color::Err,"overwriting",d) ;
								mark = Maybe ; goto MarkDep ;
							} else {
								d.crc({}) ;                                    // file does not exist any more, no more manual
							}
						}
						if (d->db_date()>req->start) {
							req->audit_node(Color::Err,"overwritten",d) ;
							trace("overwritten",d,d->db_date(),req->start) ;
							mark = Maybe ; goto MarkDep ;
						}
						if (!d.crc_ok()) { dep_state = DepState::DanglingModif ; reason |= {JobReasonTag::DepChanged,+d} ; }
						goto Continue ;
					MarkDep :
						{	Node::ReqInfo& dri = d.req_info(*cdri) ; cdri = &dri ; // refresh cdri in case dri allocated a new one
							dri.err = mark ;
						}
					Err :
						dep_state = DepState::Err ;
						switch (mark) {
							case Maybe : reason |= {JobReasonTag::DepOverwritten,+d} ; break ;
							case Yes   : reason |= {JobReasonTag::DepErr        ,+d} ; break ;
							default : FAIL(mark) ;
						}
					Continue :
						trace("dep",d,STR(is_static),STR(d.done(*cdri)),STR(makable),STR(d.err(*cdri)),ri,d.is_date,d.is_date==No?d.crc():Crc(),"<=>",d->crc,dep_state,reason) ;
					}
					if (ri.waiting()) goto Wait ;
				} break ;
				default : FAIL(ri.lvl) ;
			}
			switch (dep_state) {
				case DepState::Ok            :
				case DepState::DanglingModif :                                                     // if last dep is parallel, we have not transformed DanglingModif into Modif
				case DepState::Modif         : (*this)->run_status = RunStatus::Complete ; break ;
				case DepState::Err           : (*this)->run_status = RunStatus::DepErr   ; break ;
				case DepState::MissingStatic : (*this)->run_status = RunStatus::NoDep    ; break ;
				default : fail(dep_state) ;
			}
			trace("run",ri,(*this)->run_status,dep_state) ;
			//
			if (ri.action          !=RunAction::Run     ) break ;              // we are done with the analysis and we do not need to run : we're done
			if ((*this)->run_status!=RunStatus::Complete) break ;              // we cant run the job, error is set and we're done
			//                    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			bool maybe_new_deps = submit(ri,reason,dep_pressure) ;
			//                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (ri.waiting()) goto Wait ;
			if (!maybe_new_deps) break ;                                       // if no new deps, we are done
			make_action = MakeAction::End    ;                                 // restart analysis as if called by end() as in case of flash execution, submit has called end()
			ri.action   = RunAction ::Status ;                                 // .
			ri.lvl      = Lvl       ::Dep    ;                                 // .
			trace("restart_analysis",ri) ;
		}
		ri.lvl    = Lvl::Done ;
		ri.done_ |= ri.action ;
	Wakeup :
		if ( +ri.missing_audit && !req->zombie ) {
			trace("report_missing") ;
			IFStream    job_stream   { ancillary_file(AdminDir+"/job_data"s) } ;
			JobRpcReply report_start ; deserialize(job_stream,report_start) ;
			JobRpcReq   report_end   ; deserialize(job_stream,report_end  ) ;
			SWEAR(req->stats.ended(JobReport::Rerun)>0) ;
			req->stats.ended(JobReport::Rerun)-- ;                                                                // we tranform a rerun into a completed job, subtract what was accumulated as rerun
			req->stats.jobs_time[false/*useful*/] -= (*this)->exec_time ;                                         // .
			req->stats.jobs_time[true /*useful*/] += (*this)->exec_time ;                                         // exec time is not added to useful as it is not provided to audit_end
			audit_end( "was_" , ri , report_end.digest.stderr , {} , ri.missing_audit==MissingAudit::Modified ) ;
			ri.missing_audit = MissingAudit::No ;
		}
		trace("wakeup",ri) ;
		//                                           vvvvvvvvvvvvvvvvvvvv
		if ( wakeup_watchers && ri.done(ri.action) ) ri.wakeup_watchers() ;
		//                                           ^^^^^^^^^^^^^^^^^^^^
	Wait :
		if (!rule.is_special()) {
			bool remove_old = _inc_cur(req,before_lvl,-1) ;
			bool add_new    = _inc_cur(req,ri.lvl    ,+1) ;
			req.new_exec_time(*this,remove_old,add_new,old_exec_time?*old_exec_time:(*this)->exec_time) ;
		}
		return reason ;
	}

	::string Job::special_stderr(Node node) const {
		OStringStream res ;
		switch ((*this)->rule.special()) {
			case Special::Plain :
				SWEAR((*this)->frozen()) ;
				if ((*this)->run_status>=RunStatus::Err) {
					if (+node) res << to_string("frozen file does not exist while not optional : ",node.name(),'\n') ;
					else       res <<           "frozen file does not exist while not optional\n"                    ;
				}
			break ;
			case Special::Infinite : {
				Deps const& deps        = (*this)->deps ;
				size_t      n_all_deps  = deps.size()   ;
				size_t      n_show_deps = n_all_deps    ; if (n_show_deps>NErr) n_show_deps = NErr-1 ; // NErr lines, including ...
				for( size_t i=1 ; i<=n_show_deps ; i++ ) res << deps[n_all_deps-i].name() << '\n' ;
				if (deps.size()>NErr) res << "...\n" ;
			} break ;
			case Special::Src :
				if ((*this)->status>=Status::Err) {
					if ((*this)->frozen()) res << "frozen file does not exist\n" ;
					else                   res << "file does not exist\n"        ;
				}
			break ;
			default : ;
		}
		return res.str() ;
	}

	static SpecialStep _update_frozen_target( bool is_src , Job j , UNode t , ::string const& tn , VarIdx ti=-1/*star*/ ) {
		Rule         r   = j->rule ;
		FileInfoDate fid { tn }    ;
		if (!fid) {
			if ( is_src                        ) {                             return j->frozen() ? SpecialStep::NoFile : SpecialStep::ErrNoFile ; }
			if ( ti==VarIdx(-1)                ) { t->actual_job_tgt.clear() ; return SpecialStep::Idle                                          ; } // unlink of a star target is nothing
			if ( !r->flags(ti)[Flag::Optional] ) {                             return SpecialStep::ErrNoFile                                     ; }
			else                                 {                             return SpecialStep::NoFile                                        ; }
		}
		Trace trace("src",fid.date,t->date) ;
		if ( fid.date==t->date && +t->crc ) return SpecialStep::Idle ;
		//
		Crc  crc      { tn , g_config.hash_algo } ;
		bool modified = !crc.match(t->crc)        ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		t.refresh( fid.tag==FileTag::Lnk , crc , fid.date_or_now() ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (modified) { j->db_date = fid.date ; return SpecialStep::Modified ; }
		else          {                         return SpecialStep::Steady   ; }
	}
	bool/*may_new_dep*/ Job::_submit_special(Req& req) {
		Trace trace("submit_special",*this,req) ;
		//
		if ((*this)->frozen()) req->frozens.push_back(*this) ;
		//
		switch ((*this)->rule.special()) {
			case Special::Plain : {
				SWEAR((*this)->frozen()) ;                                     // only case where we are here without being special
				Rule::SimpleMatch match_         = simple_match()          ;   // match_ lifetime must be at least as long as static_targets lifetime
				::vector_view_c_s static_targets = match_.static_targets() ;
				SpecialStep       special_step   = SpecialStep::Idle       ;
				Node              worst_target   ;
				for( VarIdx ti=0 ; ti<static_targets.size() ; ti++ ) {
					::string const& tn = static_targets[ti]                                             ;
					UNode           t  { tn }                                                           ;
					SpecialStep     ss = _update_frozen_target( false/*is_src*/ , *this , t , tn , ti ) ;
					if (ss>special_step) { special_step = ss ; worst_target = t ; }
				}
				for( UNode t : (*this)->star_targets ) {
					SpecialStep ss = _update_frozen_target( false/*is_src*/ , *this , t , t.name() ) ;
					if (ss>special_step) { special_step = ss ; worst_target = t ; }
				}
				(*this)->status = special_step<SpecialStep::Err ? Status::Frozen : Status::ErrFrozen ;
				audit_end_special( req , special_step , worst_target ) ;
			} break ;
			case Special::Src : {
				SWEAR((*this)->rule->n_static_targets==0) ;
				//
				::string    tn = name()                                                                   ;
				UNode       t  { tn }                                                                     ;
				SpecialStep ss = _update_frozen_target( true/*is_src*/ , *this , t , tn , 0/*not_star*/ ) ;
				if ((*this)->frozen()) (*this)->status = ss<SpecialStep::Err ? Status::Frozen : Status::ErrFrozen ;
				else                   (*this)->status = ss<SpecialStep::Err ? Status::Ok     : Status::Err       ;
				audit_end_special(req,ss) ;
			} break ;
			case Special::Req :
				(*this)->status = Status::Ok ;
			break ;
			case Special::Infinite :
				(*this)->status = Status::Err ;
				audit_end_special( req , SpecialStep::Err ) ;
			break ;
			case Special::Uphill :
				for( Dep const& d : (*this)->deps ) {
					// if we see a link uphill, then our crc is unknown to trigger rebuild of dependents
					// there is no such stable situation as link will be resolved when dep is acquired, only when link appeared, until next rebuild
					UNode un{name()} ;
					un->actual_job_tgt = {*this,true/*is_sure*/} ;
					if ( d->is_lnk || !d->crc ) un.refresh( false/*is_lnk*/ , {}        , {}                ) ;
					else                        un.refresh( false/*is_lnk*/ , Crc::None , DiskDate::s_now() ) ;
				}
				(*this)->status = Status::Ok ;
			break ;
			default : fail() ;
		}
		return false/*may_new_dep*/ ;
	}

	bool/*maybe_new_deps*/ Job::_submit_plain( ReqInfo& ri , JobReason reason , CoarseDelay pressure ) {
		using Lvl = ReqInfo::Lvl ;
		Req  req  = ri.req        ;
		Rule rule = (*this)->rule ;
		Trace trace("submit_plain",*this,ri,STR((*this)->rule.is_special()),reason,pressure) ;
		SWEAR(!ri.waiting()) ;
		for( Req r : running_reqs() ) if (r!=req) {
			ri.n_wait++ ;
			ri.lvl = c_req_info(r).lvl ;                                         // Exec or Queued, same as other reqs
			if (ri.lvl==Lvl::Exec) req->audit_job(Color::Note,"started",*this) ;
			Backend::s_add_pressure( rule->backend , +*this , +req , pressure ) ; // tell backend of new Req, even if job is started and pressure has become meaningless
			trace("other_req",r,ri) ;
			return false/*may_new_deps*/ ;
		}
		Rule::Match         match_            = match()                 ;
		::vector_view_c_s   static_targets    = match_.static_targets() ;
		::umap<Node,VarIdx> static_target_map ; for( VarIdx ti=0 ; ti<static_targets.size() ; ti++ ) static_target_map[static_targets[ti]] = ti ;
		// check clashes
		NodeIdx d = 0 ;
		for( Dep const& dep : (*this)->static_deps() ) {
			if (!static_target_map.contains(dep)) { d++ ; continue ; }
			::string err_msg = to_string("simultaneously static target ",rule->targets[static_target_map[dep]].first," and static dep ",rule->deps.dct[d].first," : ") ;
			req->audit_job ( Color::Err  , "clash" , *this     ) ;
			req->audit_node( Color::Note , err_msg , dep   , 1 ) ;
			(*this)->run_status = RunStatus::DepErr ;
			trace("clash",ri) ;
			return false/*may_new_deps*/ ;
		}
		// check targets
		::vmap<Node,bool/*ok*/> manual_targets ;
		::vmap<Node,bool/*ok*/> src_targets    ;
		auto chk_target = [&]( Node t , VarIdx ti , ::string const& tn )->void {
			Flags flags = rule->flags(ti) ;
			if (t.manual_ok(FileInfoDate(tn))==No) manual_targets.emplace_back(t,flags[Flag::ManualOk]) ;
			if (t->is_src()                      ) src_targets   .emplace_back(t,flags[Flag::SourceOk]) ;
		} ;
		for( auto const& [t,ti] : static_target_map     ) {                          chk_target( t , ti             , static_targets[ti] ) ; }
		for( Node         t     : (*this)->star_targets ) { ::string tn = t.name() ; chk_target( t , match_.idx(tn) , tn                 ) ; }
		//
		for( bool chk_src : { true , false } ) {
			bool                           job_ok      = true                                   ;
			::vmap<Node,bool/*ok*/> const& chk_targets = chk_src ? src_targets : manual_targets ;
			for( auto const& [t,ok] : chk_targets ) {
				trace((chk_src?"source":"manual"),t,STR(ok)) ;
				bool target_ok = ok || req->options.flags[ chk_src ? ReqFlag::SourceOk : ReqFlag::ManualOk ] ;
				req->audit_job( target_ok?Color::Note:Color::Err , (chk_src?"source":"manual") , rule , t.name() ) ;
				job_ok &= target_ok ;
			}
			if (job_ok) continue ;
			// generate a message that is simultaneously consise, informative and executable (with a copy/paste) with sh & csh syntaxes
			req->audit_info( Color::Note , "consider :" , 1 ) ;
			if (chk_src) {
				// to reach this job, we must make a target which is *not* a source
				for( auto const& [t,ti] : static_target_map     ) if (!t->is_src()) { req->audit_node( Color::Note , "lmake -s " , t , 2 ) ; goto Advised ; }
				for( Node         t     : (*this)->star_targets ) if (!t->is_src()) { req->audit_node( Color::Note , "lmake -s " , t , 2 ) ; goto Advised ; }
			Advised : ;
			} else {
				for( auto const& [t,ok] : chk_targets ) {
					if (ok) continue ;
					DiskDate td    = file_date(t.name()) ;
					uint8_t  n_dec = (td-t->date)>Delay(2.) ? 0 : 9 ;          // if dates are far apart, probably a human action and short date is more comfortable, else be precise
					req->audit_node(
						Color::Note
					,	t->crc==Crc::None ?
							to_string( "rm `: touched " , td.str(0    ) , " not generated"                   , " `" )
						:	to_string( "rm `: touched " , td.str(n_dec) , " generated " , t->date.str(n_dec) , " `" )
					,	t
					,	2
					) ;
				}
			}
			(*this)->run_status = RunStatus::TargetErr ;
			trace(chk_src?"target_is_src":"target_is_manual",ri) ;
			return false/*may_new_deps*/ ;
		}
		::vmap_ss rsrc_map ;
		try {
			::vector_s rsrcs = match_.rsrcs() ;
			rsrc_map.reserve(rsrcs.size()) ;
			for( VarIdx r=0 ; r<rsrcs.size() ; r++ ) rsrc_map.emplace_back(rule->rsrcs.dct[r].first,rsrcs[r]) ;
		} catch (::string const& e) {
			req->audit_job ( Color::Err  , "resources" , *this     ) ;
			req->audit_info( Color::Note , e           ,         1 ) ;
			(*this)->run_status = RunStatus::RsrcsErr ;
			trace("no_rsrcs",ri) ;
			return false/*may_new_deps*/ ;
		}
		ri.n_wait++ ;                                                          // set before calling submit call back as in case of flash execution, we must be clean
		ri.lvl = Lvl::Queued ;
		try {
			//       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Backend::s_submit( rule->backend , +*this , +req , pressure , rsrc_map , reason ) ;
			//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} catch (::string const& e) {
			ri.n_wait-- ;                                                      // restore n_wait as we prepared to wait
			(*this)->status = Status::Err ;
			req->audit_job ( Color::Err  , "failed" , *this     ) ;
			req->audit_info( Color::Note , e                , 1 ) ;
			trace("submit_err",ri) ;
			return false/*may_new_deps*/ ;
		} ;
		trace("submitted",ri) ;
		return true/*maybe_new_deps*/ ;
	}

	void Job::audit_end_special( Req req , SpecialStep step , Node node ) const {
		Status status = (*this)->status                                                                          ;
		Color  color  = status==Status::Ok ? Color::HiddenOk : status>=Status::Err ? Color::Err : Color::Warning ;
		bool   frozen = JobData::s_frozen(status)                                                                ;
		//
		SWEAR(status>Status::Garbage) ;
		Trace trace("audit_end_special",*this,req,step,color,status) ;
		//
		::string    stderr   = special_stderr(node) ;
		const char* step_str = nullptr              ;
		switch (step) {
			case SpecialStep::Idle      : step_str = frozen ? "frozen"         : nullptr     ; break ;
			case SpecialStep::NoFile    : step_str = frozen ? "no_file_frozen" : "no_file"   ; break ;
			case SpecialStep::Steady    : step_str = frozen ? "steady_frozen"  : "steady"    ; break ;
			case SpecialStep::Modified  : step_str = frozen ? "new_frozen"     : "new"       ; break ;
			case SpecialStep::ErrNoFile : step_str = frozen ? "err_frozen"     : "failed"    ; break ;
			default : FAIL(step) ;
		}
		if (step_str) {
			/**/                 req->audit_job (color      ,step_str,*this  ) ;
			if (!stderr.empty()) req->audit_info(Color::None,stderr        ,1) ;
		}
	}

	bool/*ok*/ Job::forget() {
		Trace trace("Jforget",*this,(*this)->deps,(*this)->deps.size(),(*this)->rule->n_deps()) ;
		for( Req r : running_reqs() ) { (void)r ; return false ; }                                // ensure job is not running
		(*this)->status = Status::New ;
		fence() ;                                                              // once status is New, we are sure target is not up to date, we can safely modify it
		(*this)->run_status = RunStatus::Complete ;
		(*this)->deps.shorten_by( (*this)->deps.size() - (*this)->rule->n_deps() ) ; // forget hidden deps
		if (!(*this)->rule.is_special()) {
			(*this)->exec_gen = 0 ;
			(*this)->star_targets.clear() ;
		}
		trace("summary",(*this)->deps) ;
		return true ;
	}

}