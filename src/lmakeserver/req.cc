// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <stdexcept>

#include "core.hh"

using namespace Disk ;
using namespace Time ;

namespace Engine {

	//
	// Req
	//

	::mutex           Req::s_reqs_mutex    ;
	SmallIds<ReqIdx > Req::s_small_ids     ;
	::vector<ReqData> Req::s_store(1)      ;
	::vector<Req    > Req::s_reqs_by_start ;
	::vector<Req    > Req::_s_reqs_by_eta  ;

	::ostream& operator<<( ::ostream& os , Req const r ) {
		return os << "Rq(" << int(+r) << ')' ;
	}

	Req::Req(EngineClosureReq const& ecr) : Base{s_small_ids.acquire()} {
		SWEAR( +*this<=s_store.size() , +*this , s_store.size() ) ;
		if (s_store.size()>ReqIdx(-1)) throw to_string("too many requests : ",s_store.size()," > ",ReqIdx(-1)) ;
		if (+*this>=s_store.size()) {
			::unique_lock lock{s_reqs_mutex} ;                                 // emplace_back may reallocate
			s_store.emplace_back() ;
		}
		ReqData& data = **this ;
		//
		for( int i=0 ;; i++ ) {                                                // try increasing resolution in file name until no conflict
			::string lcl_log_file = "outputs/"+Pdate::s_now().str(i)     ;
			::string log_file     = to_string(AdminDir,'/',lcl_log_file) ;
			if (is_reg(log_file)) { SWEAR(i<=9,i) ; continue ; }               // if conflict, try higher resolution, at ns resolution, it impossible to have a conflict
			//
			::string last = AdminDir+"/last_output"s ;
			//
			data.log_stream.open(log_file) ;
			try         { unlink(last) ; lnk(last,lcl_log_file) ;                     }
			catch (...) { exit(2,"cannot create symlink ",last," to ",lcl_log_file) ; }
			break ;
		}
		//
		data.idx_by_start = s_n_reqs()           ;
		data.idx_by_eta   = s_n_reqs()           ;                             // initially, eta is far future
		data.jobs .dflt   = Job ::ReqInfo(*this) ;
		data.nodes.dflt   = Node::ReqInfo(*this) ;
		data.start        = Ddate::s_now()       ;
		data.options      = ecr.options          ;
		data.audit_fd     = ecr.out_fd           ;
		data.stats.start  = Pdate::s_now()       ;
		//
		s_reqs_by_start.push_back(*this) ;
		_adjust_eta(true/*push_self*/) ;
		//
		try {
			JobIdx n_jobs = from_chars<JobIdx>(ecr.options.flag_args[+ReqFlag::Jobs],true/*empty_ok*/) ;
			if (ecr.as_job()) data.job = ecr.job()                                                                             ;
			else              data.job = Job( Special::Req , Deps(ecr.targets(),Accesses::All,StaticDflags,true/*parallel*/) ) ;
			Backend::s_open_req( +*this , n_jobs ) ;
		} catch (::string const& e) {
			close() ;
			throw ;
		}
		//
		Trace trace("Req",*this,s_n_reqs(),data.start) ;
	}

	void Req::make() {
		Job job = (*this)->job ;
		Trace trace("make",*this,job) ;
		//
		Job::ReqInfo& jri = job->req_info(*this) ;
		jri.live_out = (*this)->options.flags[ReqFlag::LiveOut] ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		job->make(jri,RunAction::Status) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		for( Node d : job->deps ) if (d->done(*this)) (*this)->up_to_dates.push_back(d) ;
		chk_end() ;
	}

	void Req::kill() {
		Trace trace("kill",*this) ;
		(*this)->zombie = true ;
		Backend::s_kill_req(+*this) ;
	}

	void Req::close() {
		Trace trace("close",*this) ;
		SWEAR((*this)->is_open()) ;
		kill() ;                                                               // in case req is closed before being done
		Backend::s_close_req(+*this) ;
		// erase req from sorted vectors by physically shifting reqs that are after
		Idx n_reqs = s_n_reqs() ;
		for( Idx i=(*this)->idx_by_start ; i<n_reqs-1 ; i++ ) {
			s_reqs_by_start[i]               = s_reqs_by_start[i+1] ;
			s_reqs_by_start[i]->idx_by_start = i                    ;
		}
		s_reqs_by_start.pop_back() ;
		{	::unique_lock lock{s_reqs_mutex} ;
			for( Idx i=(*this)->idx_by_eta ; i<n_reqs-1 ; i++ ) {
				_s_reqs_by_eta[i]             = _s_reqs_by_eta[i+1] ;
				_s_reqs_by_eta[i]->idx_by_eta = i                   ;
			}
			_s_reqs_by_eta.pop_back() ;
		}
		(*this)->clear() ;
		s_small_ids.release(+*this) ;
	}

	void Req::inc_rule_exec_time( Rule rule , Delay delta , Tokens1 tokens1 ) {
			auto it = (*this)->ete_n_rules.find(rule) ;
			if (it==(*this)->ete_n_rules.end()) return ;
			(*this)->ete += delta * it->second * (tokens1+1) / rule->n_tokens ;    // adjust req ete's that are computed after this exec_time, accounting for parallel execution
			_adjust_eta() ;
	}
	void Req::new_exec_time( JobData const& job , bool remove_old , bool add_new , Delay old_exec_time ) {
		SWEAR( !job.rule->is_special() , job.rule->special ) ;
		if ( !remove_old && !add_new ) return ;                                // nothing to do
		Delay delta ;
		Rule  rule  = job.rule ;
		if (remove_old) {                                                      // use old info
			if (+old_exec_time) { delta -= old_exec_time   ;                                                                         }
			else                { delta -= rule->exec_time ; SWEAR((*this)->ete_n_rules[rule]>0) ; (*this)->ete_n_rules[rule] -= 1 ; }
		}
		if (add_new) {                                                         // use new info
			if (+job.exec_time) { delta += job.exec_time ;                                     }
			else                { delta += rule->exec_time ; (*this)->ete_n_rules[rule] += 1 ; }
		}
		(*this)->ete += delta * (job.tokens1+1) / rule->n_tokens ;             // account for parallel execution when computing ete
		_adjust_eta() ;
	}
	void Req::_adjust_eta(bool push_self) {
			Pdate now = Pdate::s_now() ;
			Trace trace("_adjust_eta",now,(*this)->ete) ;
			// reorder _s_reqs_by_eta and adjust idx_by_eta to reflect new order
			bool changed = false ;
			{	::unique_lock lock       { s_reqs_mutex }      ;
				Idx           idx_by_eta = (*this)->idx_by_eta ;
				(*this)->eta = now + (*this)->ete ;
				if (push_self) _s_reqs_by_eta.push_back(*this) ;
				while ( idx_by_eta>0 && _s_reqs_by_eta[idx_by_eta-1]->eta>(*this)->eta ) {                       // if eta is decreased
					( _s_reqs_by_eta[idx_by_eta  ] = _s_reqs_by_eta[idx_by_eta-1] )->idx_by_eta = idx_by_eta   ; // swap w/ prev entry
					( _s_reqs_by_eta[idx_by_eta-1] = *this                        )->idx_by_eta = idx_by_eta-1 ; // .
					changed = true ;
				}
				if (!changed)
					while ( idx_by_eta+1<s_n_reqs() && _s_reqs_by_eta[idx_by_eta+1]->eta<(*this)->eta ) {            // if eta is increased
						( _s_reqs_by_eta[idx_by_eta  ] = _s_reqs_by_eta[idx_by_eta+1] )->idx_by_eta = idx_by_eta   ; // swap w/ next entry
						( _s_reqs_by_eta[idx_by_eta+1] = *this                        )->idx_by_eta = idx_by_eta+1 ; // .
						changed = true ;
					}
			}
			if (changed) Backend::s_new_req_eta(+*this) ;                      // tell backends that req priority order has changed
	}

	void Req::_report_no_rule( Node node , DepDepth lvl ) {
		::string                        name      = node.name()           ;
		::vector<RuleTgt>               rrts      = node->raw_rule_tgts() ;
		::vmap<RuleTgt,Rule::FullMatch> mrts      ;                            // matching rules
		RuleTgt                         art       ;                            // set if an anti-rule matches
		RuleIdx                         n_missing = 0                    ;     // number of rules missing deps
		//
		Node dir = node ; while (dir->uphill) dir = Node(dir_name(dir.name())) ;
		if ( dir!=node && dir->makable() ) {
			/**/                                            (*this)->audit_node( Color::Err     , "no rule for"            , name , lvl   ) ;
			if (dir->conform_job_tgt().produces(dir,true) ) (*this)->audit_node( Color::Warning , "dir is buildable :"     , dir  , lvl+1 ) ;
			else                                            (*this)->audit_node( Color::Warning , "dir may be buildable :" , dir  , lvl+1 ) ;
			return ;
		}
		//
		for( RuleTgt rt : rrts ) {                                             // first pass to gather info : matching rules in mrts and number of them missing deps in n_missing
			Rule::FullMatch match{rt,name} ;
			if (!match       ) {            continue ; }
			if (rt->is_anti()) { art = rt ; break    ; }
			mrts.emplace_back(rt,match) ;
			if ( JobTgt jt{rt,name} ; +jt ) {                                                                        // do not pass *this as req to avoid generating error message at cxtor time
				swear_prod(!jt.produces(node),"no rule for ",node.name()," but ",jt->rule->name," may produce it") ;
				if (jt->run_status!=RunStatus::NoDep) continue ;
			}
			try                     { rt->deps_attrs.eval(match) ; }
			catch (::string const&) { continue ;                   }           // do not consider rule if deps cannot be computed
			n_missing++ ;
		}
		//
		if      (+art           ) (*this)->audit_node(Color::Err ,to_string("anti-rule ",art->name," matches"),name,lvl  ) ;
		else if (mrts.empty()   ) (*this)->audit_node(Color::Err ,"no rule match"                             ,name,lvl  ) ;
		else                      (*this)->audit_node(Color::Err ,"no rule for"                               ,name,lvl  ) ;
		if      (is_target(name)) (*this)->audit_node(Color::Note,"consider : git add"                        ,name,lvl+1) ;
		//
		for( auto const& [rt,m] : mrts ) {                          // second pass to do report
			JobTgt                      jt          { rt , name } ; // do not pass *this as req to avoid generating error message at cxtor time
			::string                    reason      ;
			Node                        missing_dep ;
			::vmap_s<pair_s<AccDflags>> static_deps ;
			if ( +jt && jt->run_status!=RunStatus::NoDep ) { reason      = "does not produce it"                      ; goto Report ; }
			try                                            { static_deps = rt->deps_attrs.eval(m)                     ;               }
			catch (::string const& e)                      { reason      = to_string("cannot compute its deps :\n",e) ; goto Report ; }
			{	::string missing_key ;
				// first search a non-buildable, if not found, deps have been made and we search for non makable
				for( bool search_non_buildable : {true,false} )
					for( auto const& [k,daf] : static_deps ) {
						Node d{daf.first} ;
						if ( search_non_buildable ? d->buildable!=No : d->makable() ) continue ;
						missing_key = k ;
						missing_dep = d ;
						goto Found ;
					}
			Found :
				SWEAR(+missing_dep) ;                                          // else why wouldn't it apply ?!?
				FileInfo fi{missing_dep.name()} ;
				reason = to_string( "misses static dep ", missing_key , (+fi?" (existing)":fi.tag==FileTag::Dir?" (dir)":"") ) ;
			}
		Report :
			if (!missing_dep) (*this)->audit_info( Color::Note , to_string("rule ",rt->name,' ',reason     ) ,               lvl+1 ) ;
			else              (*this)->audit_node( Color::Note , to_string("rule ",rt->name,' ',reason," :") , missing_dep , lvl+1 ) ;
			//
			if ( +missing_dep && n_missing==1 && (!g_config.max_err_lines||lvl<g_config.max_err_lines) ) _report_no_rule( missing_dep , lvl+2 ) ;
		}
		//
		if (+art) (*this)->audit_info( Color::Note , to_string("anti-rule ",art->name," matches") , lvl+1 ) ;
	}

	void Req::_report_cycle(Node node) {
		::uset  <Node> seen      ;
		::vector<Node> cycle     ;
		::uset  <Rule> to_raise  ;
		::vector<Node> to_forget ;
		for( Node d=node ; !seen.contains(d) ;) {
			seen.insert(d) ;
			for( Job j : d->conform_job_tgts(d->c_req_info(*this)) )           // 1st pass to find done rules which we suggest to raise the prio of to avoid the loop
				if (j->c_req_info(*this).done()) to_raise.insert(j->rule) ;
			for( Job j : d->conform_job_tgts(d->c_req_info(*this)) ) {         // 2nd pass to find the loop
				Job::ReqInfo const& cjri = j->c_req_info(*this) ;
				if (cjri.done()     ) continue ;
				if (cjri.speculative) to_forget.push_back(d) ;
				for( Node dd : j->deps ) {
					if (dd->done(*this)) continue ;
					d = dd ;
					goto Next ;
				}
				fail_prod("not done but all deps are done : ",j.name()) ;
			}
			fail_prod("not done but all possible jobs are done : ",d.name()) ;
		Next :
			cycle.push_back(d) ;
		}
		(*this)->audit_node( Color::Err , "cycle detected for",node ) ;
		Node deepest   = cycle.back()  ;
		bool seen_loop = deepest==node ;
		for( size_t i=0 ; i<cycle.size() ; i++ ) {
			const char* prefix ;
			/**/ if ( seen_loop && i==0 && i==cycle.size()-1 ) { prefix = "^-- " ;                    }
			else if ( seen_loop && i==0                      ) { prefix = "^   " ;                    }
			else if (                      i==cycle.size()-1 ) { prefix = "+-- " ;                    }
			else if ( seen_loop && i!=0                      ) { prefix = "|   " ;                    }
			else if ( cycle[i]==deepest                      ) { prefix = "+-> " ; seen_loop = true ; }
			else                                               { prefix = "    " ;                    }
			(*this)->audit_node( Color::Note , prefix,cycle[i] , 1 ) ;
		}
		if (!to_forget.empty() || !to_raise.empty() ) {
			(*this)->audit_info( Color::Note , "consider :\n" ) ;
			for( Node n : to_forget ) (*this)->audit_node( Color::Note , "lforget -d ",n                            , 1 ) ;
			for( Rule r : to_raise  ) (*this)->audit_info( Color::Note , to_string(r->name,".prio = ",r->prio,"+1") , 1 ) ;
		}
	}

	bool/*overflow*/ Req::_report_err( Dep const& dep , size_t& n_err , bool& seen_stderr , ::uset<Job>& seen_jobs , ::uset<Node>& seen_nodes , DepDepth lvl ) {
		if (seen_nodes.contains(dep)) return false ;
		seen_nodes.insert(dep) ;
		Node::ReqInfo const& cri = dep->c_req_info(*this) ;
		if (!dep->makable(true/*uphill_ok*/)) {
			if      (dep->err(cri)              ) return (*this)->_send_err( false/*intermediate*/ , "dangling"  , dep.name() , n_err , lvl ) ;
			else if (dep.dflags[Dflag::Required]) return (*this)->_send_err( false/*intermediate*/ , "not built" , dep.name() , n_err , lvl ) ;
		} else if (dep->multi) {
			/**/                                  return (*this)->_send_err( false/*intermediate*/ , "multi"     , dep.name() , n_err , lvl ) ;
		}
		for( Job job : dep->conform_job_tgts(cri) )
			if (_report_err( job , dep , n_err , seen_stderr , seen_jobs , seen_nodes , lvl )) return true ;
		return false ;
	}

	bool/*overflow*/ Req::_report_err( Job job , Node target , size_t& n_err , bool& seen_stderr , ::uset<Job>& seen_jobs , ::uset<Node>& seen_nodes , DepDepth lvl ) {
		if (seen_jobs.contains(job)) return false ;
		seen_jobs.insert(job) ;
		Job::ReqInfo const& jri = job->c_req_info(*this) ;
		if (!jri.done()) return false ;
		if (!job->err()) return false ;
		//
		bool intermediate = job->run_status==RunStatus::DepErr                                                                    ;
		bool overflow     = (*this)->_send_err( intermediate , job->rule->name , +target?target.name():job.name() , n_err , lvl ) ;
		if (overflow) return true ;
		//
		if ( !seen_stderr && job->run_status==RunStatus::Complete && !job->rule->is_special() ) {
			try {
				// show first stderr
				Rule::SimpleMatch match          ;
				IFStream          job_stream     { job->ancillary_file() }                                      ;
				auto              report_start   = deserialize<JobInfoStart>(job_stream)                        ;
				auto              report_end     = deserialize<JobInfoEnd  >(job_stream)                        ;
				EndNoneAttrs      end_none_attrs = job->rule->end_none_attrs.eval(job,match,report_start.rsrcs) ;
				AnalysisErr       ae             ;
				for( auto const& [t,n] : report_end.end.digest.analysis_err ) ae.emplace_back(t,Node(n)) ;
				seen_stderr |= (*this)->audit_stderr( ae , report_end.end.digest.stderr , end_none_attrs.stderr_len , lvl+1 ) ;
			} catch(...) {
				(*this)->audit_info( Color::Note , "no stderr available" , lvl+1 ) ;
			}
		}
		if (intermediate)
			for( Dep const& d : job->deps )
				if ( _report_err( d , n_err , seen_stderr , seen_jobs , seen_nodes , lvl+1 ) ) return true ;
		return false ;
	}

	void Req::chk_end() {
		if ((*this)->n_running()) return ;
		Job::ReqInfo const& cri     = (*this)->job->c_req_info(*this) ;
		Job                 job     = (*this)->job                    ;
		bool                job_err = job->status!=Status::Ok         ;
		Trace trace("chk_end",*this,cri,cri.done_,job,job->status) ;
		(*this)->audit_stats() ;
		if ((*this)->zombie) goto Done ;
		(*this)->audit_summary(job_err) ;
		if (!job_err) goto Done ;
		if (!job->c_req_info(*this).done()) {
			for( Dep const& d : job->deps )
				if (!d->done(*this)) { _report_cycle(d) ; goto Done ; }
			fail_prod("job not done but all deps are done : ",job.name()) ;
		} else {
			size_t       n_err       = g_config.max_err_lines ? g_config.max_err_lines : size_t(-1) ;
			bool         seen_stderr = false                                                        ;
			::uset<Job > seen_jobs   ;
			::uset<Node> seen_nodes  ;
			if (job->rule->special==Special::Req) {
				for( Dep const& d : job->deps ) if ( d->makable()) _report_err    (d     ,n_err,seen_stderr,seen_jobs,seen_nodes) ;
				for( Dep const& d : job->deps ) if (!d->makable()) _report_no_rule(d                                            ) ;
			} else {
				/**/                                               _report_err    (job,{},n_err,seen_stderr,seen_jobs,seen_nodes) ;
			}
		}
	Done :
		(*this)->audit_status(!job_err) ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		g_engine_queue.emplace(ReqProc::Close,*this) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	} ;

	//
	// ReqData
	//

	::mutex ReqData::_s_audit_mutex ;

	void ReqData::clear() {
		SWEAR( !n_running() , n_running() ) ;
		if (job->rule->special==Special::Req) job.pop();
		*this = {} ;
	}

	void ReqData::audit_summary(bool err) const {
		bool warning =
			!frozens    .empty()
		||	!no_triggers.empty()
		||	!clash_nodes.empty()
		;
		audit_info( err ? Color::Err : warning ? Color::Warning : Color::Note ,
			"+---------+\n"
			"| SUMMARY |\n"
			"+---------+\n"
		) ;
		::string const& startup_dir_s = options.startup_dir_s ;
		if (!startup_dir_s.empty()           ) audit_info( Color::Note , to_string( "startup dir  : " , startup_dir_s.substr(0,startup_dir_s.size()-1) ) ) ;
		if (!up_to_dates.empty()) {
			bool seen_up_to_dates = false ;
			for( Node n : up_to_dates ) if (!n->is_src()) { seen_up_to_dates = true ; break ; }
			for( Node n : up_to_dates )
				if      (!n->is_src()    ) audit_node( Color::Note    , "was already up to date :" , n ) ;
				else if (seen_up_to_dates) audit_node( Color::Warning , "source                 :" , n ) ; // align if necessary
				else                       audit_node( Color::Warning , "file is a source :"       , n ) ;
		}
		if (stats.ended(JobReport::Failed)   ) audit_info( Color::Note , to_string( "failed  jobs : " , stats.ended(JobReport::Failed)                 ) ) ;
		/**/                                   audit_info( Color::Note , to_string( "done    jobs : " , stats.ended(JobReport::Done  )                 ) ) ;
		if (stats.ended(JobReport::Steady)   ) audit_info( Color::Note , to_string( "steady  jobs : " , stats.ended(JobReport::Steady)                 ) ) ;
		if (stats.ended(JobReport::Hit   )   ) audit_info( Color::Note , to_string( "hit     jobs : " , stats.ended(JobReport::Hit   )                 ) ) ;
		if (stats.ended(JobReport::Rerun )   ) audit_info( Color::Note , to_string( "rerun   jobs : " , stats.ended(JobReport::Rerun )                 ) ) ;
		/**/                                   audit_info( Color::Note , to_string( "useful  time : " , stats.jobs_time[true /*useful*/].short_str()   ) ) ;
		if (+stats.jobs_time[false/*useful*/]) audit_info( Color::Note , to_string( "rerun   time : " , stats.jobs_time[false/*useful*/].short_str()   ) ) ;
		/**/                                   audit_info( Color::Note , to_string( "elapsed time : " , (Pdate::s_now()-stats.start)    .short_str()   ) ) ;
		{	::vmap<Job,JobIdx> frozens_ = mk_vmap(frozens) ;
			::sort( frozens_ , []( ::pair<Job,JobIdx> const& a , ::pair<Job,JobIdx> b ) { return a.second<b.second ; } ) ; // sort in discovery order
			size_t w = 0 ;
			for( auto [j,_] : frozens_ ) w = ::max( w , j->rule->name.size() ) ;
			for( auto [j,_] : frozens_ ) audit_job( j->err()?Color::Err:Color::Warning , to_string("frozen ",::setw(w),j->rule->name) , j ) ;
		}
		{	::vmap<Node,NodeIdx> no_triggers_ = mk_vmap(no_triggers) ;
			::sort( no_triggers_ , []( ::pair<Node,NodeIdx> const& a , ::pair<Node,NodeIdx> b ) { return a.second<b.second ; } ) ; // sort in discovery order
			for( auto [n,_] : no_triggers_ ) audit_node( Color::Warning , "no-trigger" , n ) ;
		}
		if (!clash_nodes.empty()) {
			::vmap<Node,NodeIdx> clash_nodes_ = mk_vmap(clash_nodes) ;
			::sort( clash_nodes_ , []( ::pair<Node,NodeIdx> const& a , ::pair<Node,NodeIdx> b ) { return a.second<b.second ; } ) ; // sort in discovery order
			audit_info( Color::Warning , "These files have been written by several simultaneous jobs and lmake was unable to reliably recover" ) ;
			audit_info( Color::Warning , "Re-executing all lmake commands that were running in parallel is strongly recommanded"               ) ;
			for( auto [n,_] : clash_nodes_ ) audit_node(Color::Warning,{},n,1) ;
		}
	}

	void ReqData::audit_job( Color c , Pdate date , ::string const& step , Rule rule , ::string const& job_name , in_addr_t host , Delay exec_time ) const {
		::OStringStream msg ;
		if (g_config.console.date_prec!=uint8_t(-1)) msg <<      date.str(g_config.console.date_prec,true/*in_day*/)                      <<' ' ;
		if (g_config.console.host_len !=uint8_t(-1)) msg <<      ::setw(g_config.console.host_len)<<SockFd::s_host(host)                  <<' ' ;
		/**/                                         msg <<      ::setw(StepSz                   )<<step                                        ;
		/**/                                         msg <<' '<< ::setw(RuleData::s_name_sz      )<<rule->name                                  ;
		if (g_config.console.has_exec_time         ) msg <<' '<< ::setw(6                        )<<(+exec_time?exec_time.short_str():"")       ;
		audit( audit_fd , log_stream , options , c , 0 , msg.str() , job_name ) ;
		last_info = {} ;
	}

	void ReqData::audit_status(bool ok) const {
		try                     { OMsgBuf().send( audit_fd , ReqRpcReply(ok) ) ; }
		catch (::string const&) {                                                } // if client has disappeared, well, we cannot do much
		log_stream << "status : " << (ok?"ok":"failed") << '\n' ;
	}

	bool/*seen*/ ReqData::audit_stderr( ::string const& backend_msg , AnalysisErr const& analysis_err , ::string const& stderr , size_t max_stderr_lines , DepDepth lvl ) const {
		if (!backend_msg.empty())                 audit_info( Color::Note , backend_msg , lvl ) ;
		for( auto const& [pfx,n] : analysis_err ) audit_node( Color::Note , pfx , n     , lvl ) ;
		if (stderr.empty()) return !analysis_err.empty() ;
		if (max_stderr_lines!=size_t(-1)) {
			::string_view shorten = first_lines(stderr,max_stderr_lines) ;
			if (shorten.size()<stderr.size()) {
				audit_info( Color::None , ::string(shorten) , lvl ) ;
				audit_info( Color::Note , "..."             , lvl ) ;
				return true ;
			}
		}
		audit_info( Color::None , stderr , lvl ) ;
		return true ;
	}

	void ReqData::audit_stats() const {
		try {
			OMsgBuf().send( audit_fd , ReqRpcReply( title(
				options
			,	stats.ended(JobReport::Failed)==0 ? ""s : to_string( "failed:"  , stats.ended(JobReport::Failed),' ')
			,	                                                     "done:"    , stats.ended(JobReport::Done  )+stats.ended(JobReport::Steady)
			,	g_config.caches.empty()           ? ""s : to_string(" hit:"     , stats.ended(JobReport::Hit   ))
			,	stats.ended(JobReport::Rerun )==0 ? ""s : to_string(" rerun:"   , stats.ended(JobReport::Rerun ))
			,	                                                    " running:" , stats.cur  (JobLvl   ::Exec  )
			,	stats.cur  (JobLvl   ::Queued)==0 ? ""s : to_string(" queued:"  , stats.cur  (JobLvl   ::Queued))
			,	stats.cur  (JobLvl   ::Dep   )==0 ? ""s : to_string(" waiting:" , stats.cur  (JobLvl   ::Dep   ))
			) ) ) ;
		} catch (::string const&) {}                                       // if client has disappeared, well, we cannot do much
	}

	bool/*overflow*/ ReqData::_send_err( bool intermediate , ::string const& pfx , ::string const& target , size_t& n_err , DepDepth lvl ) {
		if (!n_err) return true/*overflow*/ ;
		n_err-- ;
		if (n_err) audit_info( intermediate?Color::HiddenNote:Color::Err , to_string(::setw(::max(size_t(8)/*dangling*/,RuleData::s_name_sz)),pfx) , target , lvl ) ;
		else       audit_info( Color::Warning                            , "..."                                                                                  ) ;
		return !n_err/*overflow*/ ;
	}

	//
	// JobAudit
	//

	::ostream& operator<<( ::ostream& os , JobAudit const& ja ) {
		os << "JobAudit(" ;
		/**/                          os << (ja.hit?"hit":"rerun") ;
		if (ja.modified             ) os << ",modified"            ;
		if (!ja.analysis_err.empty()) os <<','<< ja.analysis_err   ;
		return os <<')' ;

	}

}
