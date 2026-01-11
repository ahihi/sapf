//    SAPF - Sound As Pure Form
//    Copyright (C) 2019 James McCartney
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <https://www.gnu.org/licenses/>.

#ifndef __VM_h__
#define __VM_h__

#include "Object.hpp"
#include "symbol.hpp"
#include "rgen.hpp"
#include <climits>
#include <vector>
#include <sys/time.h>
#include <atomic>

#if USE_LIBEDIT
	#include <histedit.h>
#else
	#include <readline/history.h>
	#include <readline/readline.h>
#endif

extern pthread_mutex_t gHelpMutex;

#define PROMPT_MAX 256
extern char *prompt_default;
extern char *promptstr;

class Unlocker
{
	pthread_mutex_t* mLock;
public:
	Unlocker(pthread_mutex_t* inLock)
	{
		pthread_mutex_unlock(mLock);
	}
	~Unlocker() 
	{ 
		pthread_mutex_lock(mLock); 
	}
};


class Locker
{
	pthread_mutex_t* mLock;
public:
	Locker(pthread_mutex_t* inLock) : mLock(inLock)
	{
		pthread_mutex_lock(mLock); 
	}
	~Locker() 
	{ 
		pthread_mutex_unlock(mLock);
	}
};

const double kDefaultSampleRate = 96000.;
const int kDefaultControlBlockSize = 128;
const int kDefaultVBlockSize = 1;
const int kDefaultZBlockSize = 512;

struct Rate
{
	int blockSize;
	double sampleRate;
	double nyquistRate;
	double invSampleRate;
	double invNyquistRate;
	double radiansPerSample;
	double invBlockSize;
	double freqLimit;
	
	Rate(Rate const& inParent, int inDiv)
	{
		set(inParent.sampleRate, inParent.blockSize, inDiv);
	}
		
	Rate(double inSampleRate, int inBlockSize)
	{
		set(inSampleRate, inBlockSize, 1);
	}
	
	Rate(const Rate& that)
	{
		blockSize = that.blockSize;
		sampleRate = that.sampleRate;
		nyquistRate = that.nyquistRate;
		invSampleRate = that.invSampleRate;
		invNyquistRate = that.invNyquistRate;
		radiansPerSample = that.radiansPerSample;
		invBlockSize = that.invBlockSize;
		freqLimit = that.freqLimit;
	}
	
	void set(double inSampleRate, int inBlockSize, int inDiv)
	{
		blockSize = inBlockSize / inDiv;
		sampleRate = inSampleRate / inDiv;
		nyquistRate = .5 * sampleRate;
		invSampleRate = 1. / sampleRate;
		invNyquistRate = 2. * invSampleRate;
		radiansPerSample = 2. * M_PI * invSampleRate;
		invBlockSize = 1. / blockSize;
		freqLimit = std::min(24000., nyquistRate);
	}
	
	bool operator==(Rate const& that)
	{
		return blockSize == that.blockSize && sampleRate == that.sampleRate;
	}
};

const size_t kStackSize = 16384;

class CompileScope;

const int kMaxTokenLen = 2048;

enum {
	parsingWords,
	parsingString,
	parsingParens,
	parsingLambda,
	parsingArray,
	parsingEnvir
};

class Thread
{
public:
	size_t stackBase;
	size_t localBase;
	std::vector<V> stack;
	std::vector<V> local;	
	P<Fun> fun;
	P<GForm> mWorkspace;

	P<CompileScope> mCompileScope;

	Rate rate;

	RGen rgen;
	
	// parser
	FILE* parserInputFile;
	char token[kMaxTokenLen];
	int tokenLen;
	
	int parsingWhat;
	bool fromString;
	
	// edit line
#if USE_LIBEDIT
	EditLine *el;
	History *myhistory;
	HistEvent ev;
#endif
	char historyfilename[PATH_MAX];
	const char *line;
	int linelen;
	int linepos;
	const char* logfilename;	
	time_t previousTimeStamp;
	
	Thread();
	Thread(const Thread& parent);
	Thread(const Thread& parent, P<Fun> const& fun);
	~Thread();
	
	
	const char* curline() const { return line + linepos; }
	void prevc() { 
		if (linepos) --linepos;
	}
	void unget(int n) { linepos -= n; }
	void unget(const char* s) { linepos -= curline() - s; }
	char c() { return line[linepos]; }
	char d() { return line[linepos] ? line[linepos+1] : 0; }
	char getc();
	void getLine();
	void logTimestamp(FILE* logfile);
	void toToken(const char* s, int n) {
		if (n >= kMaxTokenLen) { post("token too long.\n"); throw errSyntax; }
		tokenLen = n;
		memcpy(token, s, n);
		token[tokenLen] = 0;
	}
	char* tok() { return token; }
	void clearTok() { tokenLen = 0; token[0] = 0; }
	void setParseString(const char* inString) 
	{ 
		if (inString) { line = inString; fromString = true; linepos = 0; }
		else fromString = false;
	}
    
    void ApplyIfFun(V& v);
	
	
	V& getLocal(size_t i)
	{
		return local[localBase + i];
	}
	
	
	void popLocals()
	{ 
		local.erase(local.end() - fun->NumLocals(), local.end());
	}
	
	// stack ops
	void push(Arg v) 
	{
		stack.push_back(v);
	}
	void push(V && v)
	{
		stack.push_back(std::move(v));
	}
	
	void push(O o) 
	{
		stack.push_back(o);
	}
	
	void push(double f) 
	{
		stack.push_back(f);
	}
	
	template <typename T>
	void push(P<T> const& p) 
	{
		stack.push_back(p);
	}
	
	void tuck(size_t n, Arg v);

	void pushBool(bool b) { push(b ? 1. : 0.); } 
	
	V pop() 
	{
		if (stackDepth() == 0) 
			throw errStackUnderflow;

		V v = std::move(stack.back());

		stack.pop_back();
		return v;
	}
	
	void popn(size_t n) 
	{
		if (stackDepth() < n) 
			throw errStackUnderflow;
		stack.erase(stack.end() - n, stack.end());
	}

	void clearStack()
	{
		popn(stackDepth());
	}
		
	V& top() { 
		if (stackDepth() == 0) 
			throw errStackUnderflow;
		return stack.back(); 
	}
	size_t stackDepth() const { return stack.size() - stackBase; }
	size_t numLocals() const { return local.size() - localBase; }
	
	void setStackBaseTo(size_t newStackBase) { stackBase = newStackBase; }
	void setStackBase(size_t n = 0) { stackBase = stack.size() - n; }
	void setLocalBase(size_t newLocalBase) { localBase = newLocalBase; }
	void setLocalBase() { localBase = local.size(); }

	bool compile(const char* inString, P<Fun>& fun, bool inTopLevel);
	
	V popValue();
	int64_t popInt(const char* msg);
	double popFloat(const char* msg);
	#define POPTYPEDECL(TYPE) P<TYPE> pop##TYPE(const char* msg);

	POPTYPEDECL(Ref);
	POPTYPEDECL(ZRef);
	POPTYPEDECL(String);
	POPTYPEDECL(Fun);
	POPTYPEDECL(List);
	POPTYPEDECL(Form);
	
	V popZIn(const char* msg);
	V popZInList(const char* msg);
	P<List> popVList(const char* msg);
	P<List> popZList(const char* msg);

	void printStack();
	void printLocals();

	void run(Opcode* c);
		
	void repl(FILE* infile, const char* logfilename);
};

class VM
{
public:
	const char* prelude_file;
	const char* log_file;
	
	P<GTable> builtins;
		
	int printLength;
	int printDepth;
	int printTotalItems;
	
	Rate ar;
	Rate kr;
	
	int VblockSize;

	// useful objects
	P<Form> _ee;
	
	P<List> _nilv;
	P<List> _nilz;
	P<Array> _anilv;
	P<Array> _anilz;

	P<Prim> inherit;
	P<Prim> newForm;
	P<Prim> newVList;
	P<Prim> newZList;
	
	List* getNil(int inItemType) const { return inItemType == itemTypeV ? _nilv() : _nilz(); }
	Array* getNilArray(int inItemType) const { return inItemType == itemTypeV ? _anilv() : _anilz(); }

	V plusFun;
	V mulFun;
	V minFun;
	V maxFun;
	
	bool traceon = false;

#if COLLECT_MINFO
	std::atomic<int64_t> totalRetains;
	std::atomic<int64_t> totalReleases;
	std::atomic<int64_t> totalObjectsAllocated;
	std::atomic<int64_t> totalObjectsFreed;
	std::atomic<int64_t> totalSignalGenerators;
	std::atomic<int64_t> totalStreamGenerators;
#endif

	std::vector<std::string> bifHelp;
	std::vector<std::string> udfHelp;

	void addBifHelp(std::string const& str) { Locker lock(&gHelpMutex); bifHelp.push_back(str); }
	void addUdfHelp(std::string const& str) { Locker lock(&gHelpMutex); udfHelp.push_back(str); }

	void addBifHelp(const char* name, const char* mask = nullptr, const char* help = nullptr) 
	{ 	
		std::string helpstr = name;
		if (mask) {
			helpstr += " @";
			helpstr += mask;
		}
		if (help) {
			helpstr += " ";
			helpstr += help;
		}
		addBifHelp(helpstr);
	}
	void addUdfHelp(const char* name, const char* mask = nullptr, const char* help = nullptr)
	{
		std::string helpstr = name;
		if (mask) {
			helpstr += " @";
			helpstr += mask;
		}
		if (help) {
			helpstr += " ";
			helpstr += help;
		}
		addUdfHelp(helpstr);
	}

	VM();
	~VM();
	
	void setSampleRate(double inSampleRate);
	
	V def(Arg key, Arg value);
	V def(const char* name, Arg value);
	V def(const char* name, int takes, int leaves, PrimFun pf, const char* help, Arg value = 0., bool setNoEach = false);
	V defmcx(const char* name, int numArgs, PrimFun pf, const char* help, Arg value = 0.); // multi channel expanded
	V defautomap(const char* name, const char* mask, PrimFun pf, const char* help, Arg value = 0.); // auto mapped
};

extern VM vm;

struct WorkspaceDef
{
	P<String> mName;
};

struct LocalDef
{
	P<String> mName;
	size_t mIndex;
	int mTakes;
	int mLeaves;
};

struct VarDef
{
	P<String> mName;
	size_t mIndex;
	// this info used to generate push opcodes for creating the closure.
	int mFromScope;
	size_t mFromIndex;
};

enum {
	scopeUndefined,
	scopeBuiltIn,
	scopeWorkspace,
	scopeLocal,
	scopeFunVar
};

class CompileScope : public Object
{
public :
	P<CompileScope> mNext;
	std::vector<LocalDef> mLocals;
	std::vector<VarDef> mVars;

	CompileScope() : mNext(nullptr) {}
	CompileScope(P<CompileScope> const& inNext) : mNext(inNext) {}

	virtual size_t numLocals() { return mLocals.size(); }
	virtual size_t numVars() { return mVars.size(); }
	
	virtual bool isParen() const { return false; }
	
	virtual int directLookup(Thread& th, P<String> const& inName, size_t& outIndex, V& outBuiltIn);
	virtual int indirectLookup(Thread& th, P<String> const& inName, size_t& outIndex, V& outGlobal) = 0;
	virtual int bindVar(Thread& th, P<String> const& inName, size_t& outIndex) = 0;
	virtual int innerBindVar(Thread& th, P<String> const& inName, size_t& outIndex);
};

class TopCompileScope : public CompileScope
{
public :
	std::vector<WorkspaceDef> mWorkspaceVars;

	TopCompileScope() {}
	
	virtual const char* TypeName() const override { return "TopCompileScope"; }

	virtual int directLookup(Thread& th, P<String> const& inName, size_t& outIndex, V& outBuiltIn) override;
	virtual int indirectLookup(Thread& th, P<String> const& inName, size_t& outIndex, V& outGlobal) override;
	virtual int bindVar(Thread& th, P<String> const& inName, size_t& outIndex) override;
};

class InnerCompileScope : public CompileScope
{
public :
	InnerCompileScope(P<CompileScope> const& inNext) : CompileScope(inNext) {}
	
	virtual const char* TypeName() const override { return "InnerCompileScope"; }

	virtual int indirectLookup(Thread& th, P<String> const& inName, size_t& outIndex, V& outGlobal) override;
	virtual int bindVar(Thread& th, P<String> const& inName, size_t& outIndex) override;
};

class ParenCompileScope : public CompileScope
{
public :
	ParenCompileScope(P<CompileScope> const& inNext) : CompileScope(inNext) {}
	
	virtual const char* TypeName() const override { return "ParenCompileScope"; }

	virtual bool isParen() const override { return true; }
	
	virtual CompileScope* nextNonParen() const;

	virtual int directLookup(Thread& th, P<String> const& inName, size_t& outIndex, V& outBuiltIn) override;
	virtual int indirectLookup(Thread& th, P<String> const& inName, size_t& outIndex, V& outGlobal) override;
	virtual int bindVar(Thread& th, P<String> const& inName, size_t& outIndex) override;
	virtual int innerBindVar(Thread& th, P<String> const& inName, size_t& outIndex) override;
};

inline void RCObj::retain() const
{ 
#if COLLECT_MINFO
	++vm.totalRetains;
#endif
	++refcount;
}

inline void RCObj::release()
{
#if COLLECT_MINFO
	++vm.totalReleases;
#endif
	int32_t newRefCount = --refcount;
	if (newRefCount == 0) 
		norefs();
	if (newRefCount < 0)
		negrefcount();
}


class SaveStack
{
	Thread& th;
	size_t saveBase;
public:
	SaveStack(Thread& _th, int n = 0) : th(_th), saveBase(th.stackBase) { th.setStackBase(n); }
	~SaveStack() { 
		th.clearStack();
		th.setStackBaseTo(saveBase);
	}
};

class ParenStack
{
	Thread& th;
	size_t saveBase;
public:
	ParenStack(Thread& _th) : th(_th), saveBase(th.stackBase) { th.setStackBase(); }
	~ParenStack() { 
		th.setStackBaseTo(saveBase);
	}
};

class SaveCompileScope
{
	Thread& th;
	P<CompileScope> cs;
public:
	SaveCompileScope(Thread& _th) : th(_th), cs(th.mCompileScope) {}
	~SaveCompileScope() { th.mCompileScope = cs; }
	
};


uint64_t timeseed();
void loadFile(Thread& th, const char* filename);


class UseRate
{
	Thread& th;
	Rate prevRate;
public:
	UseRate(Thread& _th, Rate const& inRate) : th(_th), prevRate(th.rate) { th.rate = inRate; }
	~UseRate() { th.rate = prevRate; }
};

inline void Gen::setDone()
{
	mDone = true;
}

P<Form> extendFormByOne(Thread& th, P<Form> const& parent, P<TableMap> const& tmap, Arg value);

#endif
