// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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

	SmallIds<ReqIdx > Req::s_small_ids     ;
	::vector<Req    > Req::s_reqs_by_start ;
	::vector<Req    > Req::s_reqs_by_eta   ;
	::vector<ReqData> Req::s_store(1)      ;

	::ostream& operator<<( ::ostream& os , Req const r ) {
		return os << "Rq(" << int(+r) << ')' ;
	}

	Req::Req( Fd fd , ::vector<Node> const& targets , ReqOptions const& options ) : Base{s_small_ids.acquire()} {
		SWEAR(+*this<=s_store.size()) ;
		if (s_store.size()>ReqIdx(-1)) throw to_string("too many requests : ",s_store.size()," > ",ReqIdx(-1)) ;
		if (+*this>=s_store.size()) s_store.emplace_back() ;
		ReqData& data = **this ;
		//
		for( int i=0 ;; i++ ) {
			::string trace_file      = "outputs/"+ProcessDate::s_now().str(i)       ;
			::string fast_trace_file = to_string(*g_local_admin_dir,'/',trace_file) ;
			if (is_reg(fast_trace_file)) { SWEAR(i<=9) ; continue ; }                 // at ns resolution, it impossible to have a conflict
			//
			::string last = AdminDir+"/last_output"s ;
			//
			data.trace_stream.open(fast_trace_file) ;
			try {
				unlink(           last) ;
				lnk   (trace_file,last) ;
			} catch (...) {
				exit(2,"cannot create symlink ",last," to ",trace_file) ;
			}
			break ;
		}
		//
		data.idx_by_start = s_n_reqs()                ;
		data.idx_by_eta   = s_n_reqs()                ;                        // initially, eta is far future
		data.jobs .dflt   = Job ::ReqInfo(*this)      ;
		data.nodes.dflt   = Node::ReqInfo(*this)      ;
		data.start        = DiskDate   ::s_now()      ;
		data.job          = Job(Special::Req,targets) ;
		data.options      = options                   ;
		data.audit_fd     = fd                        ;
		data.stats.start  = ProcessDate::s_now()      ;
		//
		s_reqs_by_start.push_back(*this) ;
		s_reqs_by_eta  .push_back(*this) ;
		_adjust_eta() ;
		Backend::s_open_req(+*this) ;
		//
		Trace trace("Req",*this,s_n_reqs(),data.start) ;
	}

	void Req::make() {
		Trace trace("make",*this,(*this)->job->deps) ;
		//
		Job job = (*this)->job ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		job.make(job.req_info(*this),RunAction::Status) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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
		for( Idx i=(*this)->idx_by_start ; i<n_reqs-1 ; i++ ) { s_reqs_by_start[i] = s_reqs_by_start[i+1] ; s_reqs_by_start[i]->idx_by_start = i ; }
		for( Idx i=(*this)->idx_by_eta   ; i<n_reqs-1 ; i++ ) { s_reqs_by_eta  [i] = s_reqs_by_eta  [i+1] ; s_reqs_by_eta  [i]->idx_by_eta   = i ; }
		s_reqs_by_start.pop_back() ;
		s_reqs_by_eta  .pop_back() ;
		(*this)->clear() ;
		s_small_ids.release(+*this) ;
	}

	void Req::inc_rule_exec_time( Rule rule , Delay delta , Tokens tokens ) {
			auto it = (*this)->ete_n_rules.find(rule) ;
			if (it==(*this)->ete_n_rules.end()) return ;
			(*this)->ete += delta * it->second * tokens / rule->n_tokens ;     // adjust req ete's that are computed after this exec_time, accounting for parallel execution
			_adjust_eta() ;
	}
	void Req::new_exec_time( Job job , bool remove_old , bool add_new , Delay old_exec_time ) {
		SWEAR(!job->rule.is_special()) ;
		if ( !remove_old && !add_new ) return ;                                // nothing to do
		Delay delta ;
		Rule  rule  = job->rule ;
		if (remove_old) {                                                      // use old info
			if (+old_exec_time) { delta -= old_exec_time   ;                                                                         }
			else                { delta -= rule->exec_time ; SWEAR((*this)->ete_n_rules[rule]>0) ; (*this)->ete_n_rules[rule] -= 1 ; }
		}
		if (add_new) {                                                         // use new info
			if (+job->exec_time) { delta += job ->exec_time ;                                   }
			else                 { delta += rule->exec_time ; (*this)->ete_n_rules[rule] += 1 ; }
		}
		(*this)->ete += delta * job->tokens / rule->n_tokens ;                 // account for parallel execution when computing ete
		_adjust_eta() ;
	}
	void Req::_adjust_eta() {
			ProcessDate now = ProcessDate::s_now() ;
			(*this)->stats.eta = now + (*this)->ete ;
			Trace trace("_adjust_eta",now,(*this)->ete,(*this)->stats.eta) ;
			// reorder s_reqs_by_eta and adjust idx_by_eta to reflect new order
			Idx idx_by_eta = (*this)->idx_by_eta ;
			bool changed = false ;
			if (!changed)
				while ( idx_by_eta>0 && s_reqs_by_eta[idx_by_eta-1]->stats.eta>(*this)->stats.eta ) {
					( s_reqs_by_eta[idx_by_eta  ] = s_reqs_by_eta[idx_by_eta-1] )->idx_by_eta = idx_by_eta   ; // swap w/ prev entry
					( s_reqs_by_eta[idx_by_eta-1] = *this                       )->idx_by_eta = idx_by_eta-1 ; // .
					changed = true ;
				}
			if (!changed)
				while ( idx_by_eta+1<s_n_reqs() && s_reqs_by_eta[idx_by_eta+1]->stats.eta<(*this)->stats.eta ) {
					( s_reqs_by_eta[idx_by_eta  ] = s_reqs_by_eta[idx_by_eta+1] )->idx_by_eta = idx_by_eta   ; // swap w/ next entry
					( s_reqs_by_eta[idx_by_eta+1] = *this                       )->idx_by_eta = idx_by_eta+1 ; // .
					changed = true ;
				}
			if (changed) Backend::s_new_req_eta(+*this) ;                      // tell backends that req priority order has changed
	}

	static void _report_no_rule( Req req , Node node , DepDepth lvl=0 ) {
		::string          name      = node.name()          ;
		::vector<RuleTgt> rrts      = node.raw_rule_tgts() ;
		::vector<RuleTgt> mrts      ;                                        // matching rules
		RuleTgt           art       ;                                        // set if an anti-rule matches
		RuleIdx           n_missing = 0                    ;                 // number of rules missing deps
		//
		Node dir = node ; while (dir->uphill) dir = Node(dir_name(dir.name())) ;
		if ( dir!=node && dir->makable() ) {
			req->audit_node(Color::Err    ,"no rule for"       ,name,lvl  ) ;
			req->audit_node(Color::Warning,"dir is buildable :",dir ,lvl+1) ;
			return ;
		}
		//
		for( RuleTgt rt : rrts ) {                                             // first pass to gather info : matching rules in mrts and number of them missing deps in n_missing
			if (!Rule::Match(rt,name)) {            continue ; }
			if (rt->anti             ) { art = rt ; break    ; }
			mrts.push_back(rt) ;
			if ( JobTgt jt{rt,name} ; +jt ) {
				swear_prod(!jt.produces(node),"no rule for ",node.name()," but ",jt->rule->user_name()," produces it") ;
				if (jt->run_status!=RunStatus::NoDep) continue ;
			}
			try                     { mk_vector<Node>(Rule::Match(rt,name).deps()) ; }
			catch (::string const&) { continue ;                                     }
			n_missing++ ;
		}
		//
		if (mrts.empty()   ) req->audit_node(Color::Err ,"no rule match"     ,name,lvl  ) ;
		else                 req->audit_node(Color::Err ,"no rule for"       ,name,lvl  ) ;
		if (is_target(name)) req->audit_node(Color::Note,"consider : git add",name,lvl+1) ;
		//
		for( RuleTgt rt : mrts ) {                                             // second pass to do report
			JobTgt         jt          { rt , name } ;
			::string       reason      ;
			::vector<Node> static_deps ;
			Node           missing_dep ;
			::string       missing_key ;
			if ( +jt && jt->run_status!=RunStatus::NoDep ) { reason      = "does not produce it"                        ;  goto Report ; }
			try                                            { static_deps = mk_vector<Node>(Rule::Match(rt,name).deps()) ;                }
			catch (::string const&)                        { reason      = "cannot compute its deps"                    ;  goto Report ; }
			// first search a non-buildable, if not found, deps have been made and we search for non makable
			for( VarIdx d=0 ; d<rt->n_deps() ; d++ ) if ( static_deps[d]->buildable==No) { missing_dep = static_deps[d] ; missing_key = rt->deps.dct[d].first ; goto Missing ; }
			for( VarIdx d=0 ; d<rt->n_deps() ; d++ ) if (!static_deps[d]->makable()    ) { missing_dep = static_deps[d] ; missing_key = rt->deps.dct[d].first ; goto Missing ; }
		Missing :
			SWEAR(+missing_dep) ;                                                                                    // else why wouldn't it apply ?!?
			{	FileInfo fi{missing_dep.name()} ;
				reason = to_string( "misses dep ", missing_key , (+fi?" (existing)":fi.tag==FileTag::Dir?" (dir)":"") ) ;
			}
		Report :
			if ( !missing_dep                             ) req->audit_info( Color::Note , to_string("rule ",rt->user_name(),' ',reason     ) ,               lvl+1 ) ;
			else                                            req->audit_node( Color::Note , to_string("rule ",rt->user_name(),' ',reason," :") , missing_dep , lvl+1 ) ;
			if ( +missing_dep && n_missing==1 && lvl<NErr ) _report_no_rule( req , missing_dep , lvl+2 ) ;
		}
		//
		if (+art) req->audit_info( Color::Note , to_string("anti-rule ",art->user_name()," matches") , lvl+1 ) ;
	}

	static void _report_cycle( Req req , Node node ) {
		::uset  <Node> seen  ;
		::vector<Node> cycle ;
		for( Node d=node ; !seen.contains(d) ;) {
			seen.insert(d) ;
			for( Job j : d.conform_job_tgts(d.c_req_info(req)) ) {
				if (j.c_req_info(req).done()) continue ;
				for( Node dd : j->deps ) {
					if (dd.done(dd.c_req_info(req))) continue ;
					d = dd ;
					goto Next ;
				}
				fail_prod("not done but all deps are done : ",j) ;
			}
			fail_prod("not done but all possible jobs are done : ",d.name()) ;
		Next :
			cycle.push_back(d) ;
		}
		req->audit_node( Color::Err , "cycle detected for",node ) ;
		Node deepest = cycle.back() ;
		bool seen_loop = deepest==node ;
		for( size_t i=0 ; i<cycle.size() ; i++ ) {
			const char* prefix ;
			/**/ if ( seen_loop && i==0 && i==cycle.size()-1 ) { prefix = "^-- " ;                    }
			else if ( seen_loop && i==0                      ) { prefix = "^   " ;                    }
			else if (                      i==cycle.size()-1 ) { prefix = "+-- " ;                    }
			else if ( seen_loop && i!=0                      ) { prefix = "|   " ;                    }
			else if ( cycle[i]==deepest                      ) { prefix = "+-> " ; seen_loop = true ; }
			else                                               { prefix = "    " ;                    }
			req->audit_node( Color::Note , prefix,cycle[i] , 1 ) ;
		}
	}

	static bool/*overflow*/ _send_err( Req req , bool intermediate , ::string const& pfx , Node node , JobNodeIdx& n_err , DepDepth lvl ) {
		if (!n_err) return true/*overflow*/ ;
		n_err-- ;
		if (n_err) req->audit_node( intermediate?Color::HiddenNote:Color::Err , to_string(::setw(::max(size_t(8)/*dangling*/,RuleData::s_name_sz)),pfx) , node , lvl ) ;
		else       req->audit_info( Color::Warning                            , "..."                                                                                ) ;
		return !n_err/*overflow*/ ;
	}
	static bool/*overflow*/ _report_err( Req req , Node node , JobNodeIdx& n_err , bool& seen_stderr , ::uset<Job>& seen_jobs , ::uset<Node>& seen_nodes , DepDepth lvl=0 ) {
		if (seen_nodes.contains(node)) return false ;
		seen_nodes.insert(node) ;
		Node::ReqInfo const& cri = node.c_req_info(req) ;
		if ( !node->makable() && node.err(cri) ) {
			return _send_err( req , false/*intermediate*/ , "dangling" , node , n_err , lvl ) ;
		}
		for( Job job : node.conform_job_tgts(cri) ) {
			if (seen_jobs.contains(job)) return false ;
			seen_jobs.insert(job) ;
			Job::ReqInfo const& jri = job.c_req_info(req) ;
			if (!jri.done()) return false ;
			if (!job->err()) return false ;
			bool intermediate = job->run_status==RunStatus::DepErr ;
			bool overflow = _send_err( req , intermediate , job->rule->name , node , n_err , lvl ) ;
			if (overflow) {
				return true ;
			} else if ( !seen_stderr && job->run_status==RunStatus::Complete && !job->rule.is_special() ) {
				try {
					// show first stderr
					IFStream    job_stream   { job.ancillary_file(AdminDir+"/job_data"s) } ;
					JobRpcReply report_start = deserialize<JobRpcReply>(job_stream) ;
					JobRpcReq   report_end   = deserialize<JobRpcReq  >(job_stream) ;
					if (!report_end.digest.stderr.empty()) {
						req->audit_stderr( report_end.digest.stderr , job->rule->stderr_len , lvl ) ;
						seen_stderr = true ;
					}
				} catch(...) {
					req->audit_info( Color::Note , "no stderr available" , lvl+1 ) ;
				}
			}
			if (intermediate)
				for( Node d : job->deps )
					if ( _report_err(req,d,n_err,seen_stderr,seen_jobs,seen_nodes,lvl+1) ) return true ;
		}
		return false ;
	}
	void Req::chk_end() {
		if ((*this)->n_running()) return ;
		Job::ReqInfo const& cri     = (*this)->job.c_req_info(*this) ;
		Job                 job     = (*this)->job                   ;
		bool                job_err = job->status!=Status::Ok        ;
		Trace trace("chk_end",*this,cri,cri.done_,job,job->status) ;
		if (!(*this)->zombie) {
			SWEAR(!job->frozen()) ;                                            // what does it mean for job of a Req to be frozen ?
			bool job_warning = !(*this)->frozens.empty() ;
			(*this)->audit_info( job_err ? Color::Err : job_warning? Color::Warning : Color::Note ,
				"+---------+\n"
				"| SUMMARY |\n"
				"+---------+\n"
			) ;
			(*this)->audit_info( Color::Note , to_string( "useful  jobs : " , (*this)->stats.ended()-(*this)->stats.ended(JobReport::Rerun) ) ) ;
			(*this)->audit_info( Color::Note , to_string( "rerun   jobs : " , (*this)->stats.ended(JobReport::Rerun)                        ) ) ;
			(*this)->audit_info( Color::Note , to_string( "useful  time : " , (*this)->stats.jobs_time[true /*useful*/].short_str()         ) ) ;
			(*this)->audit_info( Color::Note , to_string( "rerun   time : " , (*this)->stats.jobs_time[false/*useful*/].short_str()         ) ) ;
			(*this)->audit_info( Color::Note , to_string( "elapsed time : " , (ProcessDate::s_now()-(*this)->stats.start)   .short_str()    ) ) ;
			for( Job j : (*this)->frozens ) (*this)->audit_job( j->err()?Color::Err:Color::Warning , "frozen" , j ) ;
			if (!(*this)->clash_nodes.empty()) {
				(*this)->audit_info( Color::Warning , "These files have been written by several simultaneous jobs" ) ;
				(*this)->audit_info( Color::Warning , "Re-executing all lmake commands that were running in parallel is strongly recommanded" ) ;
				for( Node n : (*this)->clash_nodes ) (*this)->audit_node(Color::Warning,{},n,1) ;
			}
			if (job_err) {
				JobNodeIdx   n_err       = NErr ;
				bool         seen_stderr = false ;
				::uset<Job > seen_jobs   ;
				::uset<Node> seen_nodes  ;
				for( Node d : job->deps ) {
					Node::ReqInfo const& cdri = d.c_req_info(*this) ;
					if      (!d.done(cdri)) _report_cycle  ( *this , d                                                ) ;
					else if (d->makable() ) _report_err    ( *this , d , n_err , seen_stderr , seen_jobs , seen_nodes ) ;
					else                    _report_no_rule( *this , d                                                ) ;
				}
			}
		}
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
		SWEAR(!n_running()) ;
		*this = ReqData() ;
	}

}