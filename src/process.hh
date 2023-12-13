// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/signalfd.h>
#include <sys/wait.h>
#include <wait.h>

#include <csignal>

#include "fd.hh"

struct Pipe {
	// cxtors & casts
	Pipe(       ) = default ;
	Pipe(NewType) { open() ; }
	void open() {
		int fds[2] ;
		swear_prod( ::pipe(fds)==0 , "cannot create pipes" ) ;
		read  = fds[0] ;
		write = fds[1] ;
	}
	void close() {
		read .close() ;
		write.close() ;
	}
	// data
	Fd read  ;     // read  side of the pipe
	Fd write ;     // write side of the pipe
} ;

static inline bool/*done*/ set_sig( int sig , bool block ) {
	sigset_t new_mask ;
	sigset_t old_mask ;
	sigemptyset(&new_mask    ) ;
	sigaddset  (&new_mask,sig) ;
	//
	swear( ::pthread_sigmask( block?SIG_BLOCK:SIG_UNBLOCK , &new_mask , &old_mask )==0 , "cannot ",block?"block":"unblock"," sig ",sig ) ;
	//
	return sigismember(&old_mask,sig)!=block ;
}
static inline Fd open_sig_fd(int sig) {
	swear_prod(set_sig(sig,true/*block*/),"signal ",::strsignal(sig)," is already blocked") ;
	//
	sigset_t mask ;
	sigemptyset(&mask    ) ;
	sigaddset  (&mask,sig) ;
	//
	return ::signalfd( -1 , &mask , SFD_CLOEXEC ) ;
}
static inline void close_sig_fd( Fd fd , int sig ) {
	fd.close() ;
	set_sig(sig,false/*block*/) ;
}

static inline bool is_sig_sync(int sig) {
	switch (sig) {
		case SIGILL  :
		case SIGTRAP :
		case SIGABRT :
		case SIGBUS  :
		case SIGFPE  :
		case SIGSEGV : return true  ;
		default      : return false ;
	}
}

static inline ::string wstatus_str(int wstatus) {
	if (WIFEXITED  (wstatus)) return WEXITSTATUS(wstatus) ? to_string("exit ",WEXITSTATUS(wstatus))  : "ok"s   ;
	if (WIFSIGNALED(wstatus)) return to_string("signal ",WTERMSIG(wstatus),'-',::strsignal(WTERMSIG(wstatus))) ;
	else                      return "??"                                                                      ;
}

struct Child {
	static constexpr Fd None{-1} ;
	static constexpr Fd Pipe{-2} ;
	// cxtors & casts
	Child() = default ;
	Child(
		bool            as_group_          , ::vector_s const& args
	,	Fd              stdin_fd=Fd::Stdin , Fd                stdout_fd=Fd::Stdout , Fd stderr_fd=Fd::Stderr
	,	::map_ss const* env     =nullptr   , ::map_ss   const* add_env  =nullptr
	,	::string const& chroot  ={}
	,	::string const& cwd     ={}
	,	void (*pre_exec)()      =nullptr
	) {
		spawn(as_group_,args,stdin_fd,stdout_fd,stderr_fd,env,add_env,chroot,cwd,pre_exec) ;
	}
	~Child() {
		swear_prod(pid==-1,"bad pid ",pid) ;
	}
	// accesses
	bool operator +() const { return pid!=-1 ; }
	bool operator !() const { return !+*this ; }
	// services
	bool/*parent*/ spawn(
		bool            as_group_          , ::vector_s const& args
	,	Fd              stdin_fd=Fd::Stdin , Fd                stdout_fd=Fd::Stdout , Fd stderr_fd=Fd::Stderr
	,	::map_ss const* env     =nullptr   , ::map_ss   const* add_env  =nullptr
	,	::string const& chroot  ={}
	,	::string const& cwd     ={}
	,	void (*pre_exec)()      =nullptr
	) ;
	void mk_daemon() {
		pid = -1 ;
		stdin .detach() ;
		stdout.detach() ;
		stderr.detach() ;
	}
	void waited() {
		pid = -1 ;
	}
	int/*wstatus*/ wait() {
		SWEAR(pid!=-1) ;
		int wstatus ;
		int rc = ::waitpid(pid,&wstatus,0) ;
		swear_prod(rc==pid,"cannot wait for pid ",pid) ;
		waited() ;
		return wstatus ;
	}
	bool wait_ok() {
		int wstatus = wait() ;
		return WIFEXITED(wstatus) && WEXITSTATUS(wstatus)==0 ;
	}
	bool/*done*/ kill(int sig) {
		if (!sig    ) return true                  ;
		if (as_group) return kill_group  (pid,sig) ;
		else          return kill_process(pid,sig) ;
	}
	bool is_alive() const {
		return kill_process(pid,0) ;
	}
	//data
	pid_t       pid      = -1    ;
	AutoCloseFd stdin    ;
	AutoCloseFd stdout   ;
	AutoCloseFd stderr   ;
	bool        as_group = false ;
} ;
