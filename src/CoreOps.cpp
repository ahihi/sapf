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

#include "VM.hpp"
#include "Parser.hpp"
#include "clz.hpp"
#include <string>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////

// stack shufflers
#pragma mark STACK OPS

static void clear_(Thread& th, Prim* prim)
{
	th.clearStack();
}

static void cleard_(Thread& th, Prim* prim)
{
	V v = th.top();
	th.clearStack();
	th.push(v);
}

static void stackDepth_(Thread& th, Prim* prim)
{
	th.push(th.stackDepth());
}

static void ba_(Thread& th, Prim* prim)
{
	if (th.stackDepth() < 2)
		throw errStackUnderflow;
	V* sp = &th.top();
	V b = sp[0];
	sp[0] = sp[-1];
	sp[-1] = b;
}

static void bac_(Thread& th, Prim* prim) 
{
	// swapd
	if (th.stackDepth() < 3)
		throw errStackUnderflow;
	V* sp = &th.top();
	V a = sp[-2];
	V b = sp[-1];
	V c = sp[0];
	sp[-2] = b;
	sp[-1] = a;
	sp[0] = c;
}

static void cab_(Thread& th, Prim* prim)
{
	// rrot
	if (th.stackDepth() < 3)
		throw errStackUnderflow;
	V* sp = &th.top();
	V a = sp[-2];
	V b = sp[-1];
	V c = sp[0];
	sp[-2] = c;
	sp[-1] = a;
	sp[0] = b;
}

static void bca_(Thread& th, Prim* prim)
{
	// rot
	if (th.stackDepth() < 3)
		throw errStackUnderflow;
	V* sp = &th.top();
	V a = sp[-2];
	V b = sp[-1];
	V c = sp[0];
	sp[-2] = b;
	sp[-1] = c;
	sp[0] = a;
}

static void cba_(Thread& th, Prim* prim)
{
	// reverse top 3
	if (th.stackDepth() < 3)
		throw errStackUnderflow;
	V* sp = &th.top();
	V a = sp[-2];
	V b = sp[-1];
	V c = sp[0];
	sp[-2] = c;
	sp[-1] = b;
	sp[0] = a;
}

static void aa_(Thread& th, Prim* prim)
{
	// dup
	V v = th.top();
	th.push(v);
}

static void aaa_(Thread& th, Prim* prim)
{
	// dup
	V v = th.top();
	th.push(v);
	th.push(v);
}

static void aba_(Thread& th, Prim* prim)
{
	// over
	if (th.stackDepth() < 2)
		throw errStackUnderflow;

	V* sp = &th.top();
	th.push(sp[-1]);
}

static void bab_(Thread& th, Prim* prim)
{
	// tuck
	if (th.stackDepth() < 2)
		throw errStackUnderflow;

	V* sp = &th.top();
	V a = sp[-1];
	V b = sp[0];
	th.push(b);

	sp[-1] = b;
	sp[0] = a;
}

static void aab_(Thread& th, Prim* prim)
{
	// tuck
	if (th.stackDepth() < 2)
		throw errStackUnderflow;
    
	V* sp = &th.top();
	V a = sp[-1];
	V b = sp[0];
	th.push(b);	
	sp[0] = a;
}

static void aabb_(Thread& th, Prim* prim)
{
	// tuck
	if (th.stackDepth() < 2)
		throw errStackUnderflow;
    
	V* sp = &th.top();
	V a = sp[-1];
	V b = sp[0];
	th.push(b);	
	sp[0] = a;
	th.push(b);	
}

static void abab_(Thread& th, Prim* prim)
{
	if (th.stackDepth() < 2)
		throw errStackUnderflow;

	V* sp = &th.top();
	V a = sp[-1];
	V b = sp[0];
	th.push(a);	
	th.push(b);	
}

static void nip_(Thread& th, Prim* prim)
{
	if (th.stackDepth() < 2)
		throw errStackUnderflow;

	V* sp = &th.top();
	V b = sp[0];
	sp[-1] = b;
	th.pop();
}


static void pop_(Thread& th, Prim* prim)
{
	th.pop();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma mark INHERIT

static bool hasItem(int64_t size, Table** a, Table* item)
{
	for (int64_t i = 0; i<size; ++i) {
		if (a[i]->Identical(item))
			return true;
	}
	return false;
}

static void Envir_merge2(Thread& th, int64_t asize, Table** a, int64_t bsize, Table** b, int64_t& csize, Table** c0)
{

	Table** c = c0;
	Table** aend = a + asize;
	Table** bend = b + bsize;
	while (a < aend && b < bend) {	
		if ((*a)->Identical(*b)) {
			*c++ = *a++;
			b++;
		} else if (!hasItem(bend-b-1, b+1, *a)) {
			*c++ = *a++;
		} else if (!hasItem(aend-a-1, a+1, *b)) {
			*c++ = *b++;
		} else {
			throw errInconsistentInheritance;
		}
	}
	while (a < aend) { *c++ = *a++; }
	while (b < bend) { *c++ = *b++; }
	csize = c - c0;
}
	
static int64_t Envir_toVec(O list, int64_t maxSize, Table** vec)
{
	int64_t i = 0;
	for (; list && i < maxSize-1;) {
		vec[i++] = ((Form*)list)->mTable();
		list = ((Form*)list)->mNextForm();
	}
	return i;
}

static P<Form> Envir_fromVec(int64_t size, Table** a)
{
	if (size == 0) return vm._ee;
	
	P<Form> list;
	for (int64_t i = size-1; i >= 0; --i) {
		list = consForm(a[i], list);
	}
	return list;
}


P<Form> linearizeInheritance(Thread& th, size_t numArgs, V* args)
{
	if (numArgs == 0) return vm._ee;
	if (numArgs == 1) {
		if (args[0].isForm()) {
			return (Form*)args[0].asObj();
		} else {
			return vm._ee;
		}
	}
	
	const size_t maxSize = 1024;
	Table* t[3][maxSize];
	
	int ai = 0;
	int bi = 1;
	int ci = 2;

	int64_t asize = Envir_toVec(args[0].asObj(), maxSize, t[ai]);
	for (size_t i = 1; i < numArgs; ++i) {
		int64_t bsize = Envir_toVec(args[i].asObj(), maxSize, t[bi]);
		int64_t csize;
		Envir_merge2(th, asize, t[ai], bsize, t[bi], csize, t[ci]);
		int temp = ci;
		ci = ai;
		ai = temp;
		asize = csize;
	}
	return Envir_fromVec(asize, t[ai]);
}

P<Form> asParent(Thread& th, V& v)
{
	P<Form> parent;
	if (v.isReal()) {
		parent = nullptr;
	} else if (v.isForm()) {
		if (v.o() == vm._ee()) parent = nullptr;
		else parent = (Form*)v.o();
	} else if (v.isFunOrPrim()) {
		SaveStack save(th);
		v.apply(th);
		
		size_t n = th.stackDepth();
		V* args = &th.top() - (n - 1);

		parent = linearizeInheritance(th, n, args);
	
		th.popn(n);
	} else if (v.isVList()) {
		if (!v.isFinite())
			indefiniteOp("", "{} : parent");
			
		P<Array> const& a = ((List*)v.o())->mArray;
		size_t n = a->size();
		parent = linearizeInheritance(th, n, a->v());
	} else {
		wrongType("new : parent", "Form, Fun or VList", v);
		return NULL; // never gets here, but otherwise gcc warns about parent uninitialized.
	}
	return parent;
}

struct Binding
{
	V key;
	BothIn value;
};

struct Bind : public Gen
{
	P<TableMap> mMap;
	P<Form> mParent;
	std::vector<Binding> _bindings;
	
	Bind(Thread& th, P<Form>& parent, P<List> const& bindings, bool inIsFinite) 
		: Gen(th, itemTypeV, inIsFinite), mParent(parent)
	{
		int64_t m = bindings->length(th);
		for (int64_t i = 0; i+1 < m; i += 2) {
			Binding b;
			b.key = bindings->at(i);
			b.value.set(bindings->at(i+1));
			_bindings.push_back(b);
		}
	}
	
	const char* TypeName() const override { return "Bind"; }
	
	virtual void pull(Thread& th) override 
	{
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		int n = framesToFill;
		for (int i = 0; i < n; ++i) {
			P<Form> e = consForm(new Table(mMap), mParent);
			
			int64_t m = _bindings.size();
			for (int64_t j = 0; j < m; ++j) {
				Binding& b = _bindings[j];
				V val;
				if (b.value.one(th, val)) {
					setDone();
					goto leave;
				}
				e->put(j, val); // ok, single threaded mutation
			}
			
			out[i] = e;
			--framesToFill;
		}
leave:
		produce(framesToFill);
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma mark REF OPS


static void ref_(Thread& th, Prim* prim)
{
	V value = th.pop();
	V ref = new Ref(value);
	th.push(ref);
}

static void zref_(Thread& th, Prim* prim)
{
	Z z = th.popFloat("zref : value");
	th.push(new ZRef(z));
}


static void set_(Thread& th, Prim* prim)
{
	V ref = th.pop();
	if (ref.isRef()) {
		V value = th.pop();
		((Ref*)ref.o())->set(value);
	} else if (ref.isZRef()) {
		Z value = th.popFloat("set : value");
		((ZRef*)ref.o())->set(value);
	} else if (ref.isPlug()) {
		V value = th.pop();
		((Plug*)ref.o())->setPlug(value);
	} else if (ref.isZPlug()) {
		V value = th.popZIn("set : value");
		((ZPlug*)ref.o())->setPlug(value);
	} else if (ref.isVList() && ref.isFinite()) {
		V value = th.pop();
		P<List> refList = ((List*)ref.o())->pack(th);
		P<Array> refArray = refList->mArray;
		V* refs = refArray->v();
		if (value.isVList() && value.isFinite()) {
			P<List> valueList = ((List*)value.o())->pack(th);
			P<Array> valueArray = valueList->mArray;
			V* vals = valueArray->v();
			size_t n = std::min(refArray->size(), valueArray->size());
			for (size_t i = 0; i < n; ++i) {
				SaveStack ss(th);
				th.push(vals[i]);
				th.push(refs[i]);
				set_(th, prim);
			}
		} else {
			size_t n = refArray->size();
			for (size_t i = 0; i < n; ++i) {
				SaveStack ss(th);
				th.push(value);
				th.push(refs[i]);
				set_(th, prim);
			}
		}
	} else {
		wrongType("set : ref", "Ref, ZRef, Plug or ZPlug", ref);
	}
}

static void get_(Thread& th, Prim* prim)
{
	V ref = th.pop();
	th.push(ref.deref());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

// printing ops
#pragma mark PRINTING

static void pr_(Thread& th, Prim* prim)
{
	std::string s;
	th.pop().print(th, s);
	post("%s", s.c_str());
}

static void prdebug_(Thread& th, Prim* prim)
{
	std::string s;
	th.pop().printDebug(th, s);
	post("%s", s.c_str());
}

static void cr_(Thread& th, Prim* prim)
{
	post("\n");
}

static void tab_(Thread& th, Prim* prim)
{
	post("\t");
}

static void sp_(Thread& th, Prim* prim)
{
	post(" ");
}

static void prstk_(Thread& th, Prim* prim)
{
	post("stack : "); th.printStack(); post("\n");
}


static void printLength_(Thread& th, Prim* prim)
{
	th.push(vm.printLength);
}

static void printDepth_(Thread& th, Prim* prim)
{
	th.push(vm.printDepth);
}

static void printTotalItems_(Thread& th, Prim* prim)
{
	th.push(vm.printTotalItems);
}

static void setPrintLength_(Thread& th, Prim* prim)
{
	vm.printLength = (int)th.popInt("setPrintLength : length");
}

static void setPrintDepth_(Thread& th, Prim* prim)
{
	vm.printDepth = (int)th.popInt("setPrintDepth : depth");
}

static void setPrintTotalItems_(Thread& th, Prim* prim)
{
	vm.printTotalItems = (int)th.popInt("setPrintTotalItems : numItems");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

// string ops
#pragma mark STRINGS

static void str_(Thread& th, Prim* prim)
{
	V v = th.pop();
	std::string s;
	v.print(th, s);
	th.push(new String(s.c_str()));
}

static void debugstr_(Thread& th, Prim* prim)
{
	V v = th.pop();
	std::string s;
	v.printDebug(th, s);
	th.push(new String(s.c_str()));
}

static void strcat_(Thread& th, Prim* prim)
{
	P<String> sep = th.popString("strcat : separator");
	P<List> list = th.popVList("strcat : list");
	if (!list->isFinite())
		indefiniteOp("strcat : list", "");
	
	std::string s;
	
	list = list->pack(th);
	P<Array> array = list->mArray;
	
	for (int i = 0; i < array->size(); ++i) {
		if (i != 0) s += sep->s;
		V v = array->at(i);
		v.print(th, s);
	}
	
	th.push(new String(s.c_str()));	
}

static void strlines_(Thread& th, Prim* prim)
{
	P<List> list = th.popVList("strlines : list");
	if (!list->isFinite())
		indefiniteOp("strlines : list", "");
	
	std::string s;
	
	list = list->pack(th);
	P<Array> array = list->mArray;
	
	for (int i = 0; i < array->size(); ++i) {
		V v = array->at(i);
		v.print(th, s);
		s += "\n";
	}
	
	th.push(new String(s.c_str()));	
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

// loops
#pragma mark LOOPS

static void while_(Thread& th, Prim* prim)
{
	V body = th.pop();
	V test = th.pop();
	while (1) {
		{
			SaveStack ss(th);
			test.apply(th);
			if (th.pop().isTrue()) break;
		}
		{
			SaveStack ss(th);
			body.apply(th);
		}
	}
}


static void eachDoer(Thread& th, int level, uint32_t mask, BothIn& in, V& fun)
{
	int nextLevel = level - 1;
	if (level == 0) {
		while (1) {
			SaveStack ss(th);
			V v;
			if (in.one(th, v)) 
				return;

			th.push(v);
			fun.apply(th);
		}
	} else {
		int bit = 1 << level;

		while (1) {
			V argv;
			if (in.one(th, argv))
				return;

			bool isConstant =  !(argv.isList() && (mask & bit));
			
			if (isConstant) {
				SaveStack ss(th);
				th.push(argv);
				fun.apply(th);
			} else {
				BothIn subin;
				V v = argv;
				if (mask & bit) {
					if (v.isList() && !v.isFinite())
						indefiniteOp("do : list", "");
						
					subin.set(v);
				} else {
					subin.setConstant(v);
				}
			
				eachDoer(th, nextLevel, mask, subin, fun);
			}
		}
	}
};

static void do_(Thread& th, Prim* prim)
{
	V f = th.pop();
	V item = th.pop();
	
	if (item.isEachOp()) {
		P<EachOp> p = (EachOp*)item.o();
		if (!p->v.isFinite())
			indefiniteOp("do : list", "");
			
		BothIn in(p->v);
		int numLevels = p->mask <= 1 ? 0 : LOG2CEIL(p->mask) - 1;
		eachDoer(th, numLevels, p->mask, in, f);
	} else if (item.isList()) {
	
		P<List> s = (List*)item.o();
		if (!s->isFinite())
			indefiniteOp("do", "");

		if (s->isVList()) {
			VIn _a(s());
			while (1) {
				int n = kDefaultVBlockSize;
				int astride;
				V *a;
				if (_a(th, n,astride, a)) {
					break;
				} else {
					for (int i = 0; i < n; ++i) {
						SaveStack save(th);
						th.push(*a);
						a += astride;
						f.apply(th);
					}
					_a.advance(n);
				}
			}
		} else {
			ZIn _a(s());
			while (1) {
				int n = th.rate.blockSize;
				int astride;
				Z *a;
				if (_a(th, n,astride, a)) {
					break;
				} else {
					for (int i = 0; i < n; ++i) {
						SaveStack save(th);
						th.push(*a);
						a += astride;
						f.apply(th);
					}
					_a.advance(n);
				}
			}
		}
	} else {
		wrongType("do : list", "List", item);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma mark CONDITIONALS

static void equals_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.pop();
	th.pushBool(a.Equals(th, b));
}

static void less_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.pop();
	th.pushBool(Compare(th, a, b) < 0);
}

static void greater_(Thread& th, Prim* prim)
{
	V b = th.pop();
	V a = th.pop();
	th.pushBool(Compare(th, a, b) > 0);
}

static void if_(Thread& th, Prim* prim)
{
	V elseCode = th.pop();
	V thenCode = th.pop();
	V test = th.pop();
	if (test.isTrue()) {
		thenCode.apply(th);
	} else {
		elseCode.apply(th);
	}
}

static void dip_(Thread& th, Prim* prim)
{
    V temp = th.pop();
    V fun = th.pop();
    fun.apply(th);
    th.push(temp);
}

static void not_(Thread& th, Prim* prim)
{
	V p = th.pop();
	th.pushBool(p.isFalse());
}

static void protect_(Thread& th, Prim* prim)
{
	V protectCode = th.pop();
	V tryCode = th.pop();

	
	try {
		tryCode.apply(th);
	} catch (...) {
		protectCode.apply(th);
		throw;
	}

	protectCode.apply(th);
}

static void try_(Thread& th, Prim* prim)
{
	V catchCode = th.pop();
	V tryCode = th.pop();
	try {
		tryCode.apply(th);
	} catch (...) {
		catchCode.apply(th);
		throw;
	}
}

static void throw_(Thread& th, Prim* prim)
{
	throw -1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma mark ENVIR OPS

static void inherit_(Thread& th, Prim* prim)
{
	V v = th.pop();
	th.push(asParent(th, v));
}

static void pushWorkspace_(Thread& th, Prim* prim)
{
	th.mWorkspace = consForm(new GTable(), th.mWorkspace);
}

static void popWorkspace_(Thread& th, Prim* prim)
{
    if (!th.mWorkspace->mNextForm()) {
        post("Must not pop top level workspace!\n");
        return;
    }
	th.mWorkspace = th.mWorkspace->mNextForm;
}


static void has_(Thread& th, Prim* prim)
{
	V key = th.pop();
	V list = th.pop();
	
	V value;
	bool has = list.get(th, key, value);
	th.pushBool(has);
}

static void keys_(Thread& th, Prim* prim)
{
	P<Table> t = th.popForm("keys : e")->mTable;
	
	P<Array> a = new Array(itemTypeV, t->mMap->mSize);
	
	V* keys = t->mMap->mKeys;
	for (size_t i = 0; i < t->mMap->mSize; ++i) {
		a->add(keys[i]);
	}
	
	th.push(new List(a));
}

static void values_(Thread& th, Prim* prim)
{
	P<Table> t = th.popForm("keys : e")->mTable;
	
	P<Array> a = new Array(itemTypeV, t->mMap->mSize);
	
	V* vals = t->mValues;
	for (size_t i = 0; i < t->mMap->mSize; ++i) {
		a->add(vals[i]);
	}
	
	th.push(new List(a));
}


static void kv_(Thread& th, Prim* prim)
{
	P<Table> t = th.popForm("values : e")->mTable;
	
	P<Array> ka = new Array(itemTypeV, t->mMap->mSize);
	P<Array> va = new Array(itemTypeV, t->mMap->mSize);
	
	V* keys = t->mMap->mKeys;
	V* vals = t->mValues;
	for (size_t i = 0; i < t->mMap->mSize; ++i) {
		ka->add(keys[i]);
		va->add(vals[i]);
	}

	th.push(new List(ka));
	th.push(new List(va));
}

static void local_(Thread& th, Prim* prim)
{
	P<Table> t = th.popForm("local : e")->mTable;
	
	th.push(new Form(t));
}

static void parent_(Thread& th, Prim* prim)
{
	P<Form> form = th.popForm("values : e");
	
	th.push(form->mNextForm ? form->mNextForm : vm._ee);
}

static void dot_(Thread& th, Prim* prim)
{
	V key = th.pop();
	V e = th.pop();
		
	if (!key.isVList()) {
		V v;
		e.dot(th, key, v);
		th.push(v);
	} else {
		if (!key.isFinite())
			indefiniteOp("dot : key", "");
		List* ks = (List*)key.o();
		ks = ks->pack(th);

		P<Array> ka = ks->mArray;
		int64_t size = ka->size();
		P<Array> va = new Array(itemTypeV, size);
		va->setSize(size);
		for (int64_t i = 0; i < size; ++i) {
			V v;
			e.dot(th, key, v);
			th.push(v);
		}
		th.push(new List(va));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma mark APPLY

static void noeach_(Thread& th, Prim* prim)
{
	V fun = th.top();
	
	fun.SetNoEachOps();
}

static void apply_(Thread& th, Prim* prim)
{
	V v = th.pop();
	v.apply(th);
}

static void applyEvent_(Thread& th, Prim* prim)
{
	P<Fun> fun = th.popFun("!e : fun");
	P<Form> form = th.popForm("!e : form");
	
	for (auto const& name : fun->mDef->mArgNames) {
		V argValue;
		if (!form->dot(th, name, argValue)) {
			notFound(name);
		}
		th.push(argValue);
	}
	
	fun->apply(th);
}

static void type_(Thread& th, Prim* prim)
{
	th.push(getsym(th.pop().TypeName()));
}

static void load_(Thread& th, Prim* prim)
{
	P<String> filename = th.popString("load : filename");
	loadFile(th, filename->s);
}

static void compile_(Thread& th, Prim* prim)
{
	P<String> s = th.popString("compile : string");
	const char* ss = s->s;
	
	P<Fun> fun;
	if (!th.compile(ss, fun, false)) {
		th.push(0.);
	} else {
		th.push(fun);
	}
}

static void y_combinator_call_(Thread& th, Prim* prim)
{
	th.push(prim);
	prim->v.apply(th);
}

static void Y_(Thread& th, Prim* prim)
{
	V f = th.pop();
	if (f.takes() < 1) {
		post("Y : fun. function must take at least one argument.\n");
		throw errFailed;
	}
	th.push(new Prim(y_combinator_call_, f, f.takes()-1, f.leaves(), NULL, NULL));
}


static void* gofun(void* ptr)
{
    Thread* th = (Thread*)ptr;
    th->fun->run(*th);
    delete th;
    return NULL;
}

static void go_(Thread& th, Prim* prim)
{
    P<Fun> fun = th.popFun("go : fun");
    
    Thread* newThread = new Thread (th, fun); 
   
    pthread_t pt;
    pthread_create(&pt, NULL, gofun, newThread);
}

static void sleep_(Thread& th, Prim* prim)
{
    Z t = th.popFloat("sleep : secs");
    
    usleep((useconds_t)floor(1e6 * t + .5));
}

#if COLLECT_MINFO
static void minfo_(Thread& th, Prim* prim)
{
	post("signal generators %qd\n", vm.totalSignalGenerators.load());
	post("stream generators %qd\n", vm.totalStreamGenerators.load());
	post("objects live %qd\n", vm.totalObjectsAllocated.load() - vm.totalObjectsFreed.load());
	post("objects allocated %qd\n", vm.totalObjectsAllocated.load());
	post("objects freed %qd\n", vm.totalObjectsFreed.load());
	post("retains %qd\n", vm.totalRetains.load());
	post("releases %qd\n", vm.totalReleases.load());
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma mark SAMPLE RATES

static void sr_(Thread& th, Prim* prim)
{	
	th.push(th.rate.sampleRate);
}

static void nyq_(Thread& th, Prim* prim)
{	
	th.push(th.rate.sampleRate * .5);
}

static void isr_(Thread& th, Prim* prim)
{	
	th.push(th.rate.invSampleRate);
}

static void rps_(Thread& th, Prim* prim)
{	
	th.push(th.rate.radiansPerSample);
}

static void inyq_(Thread& th, Prim* prim)
{	
	th.push(th.rate.invNyquistRate);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma mark HELP

static void listdump_(Thread& th, Prim* prim)
{	
	P<List> list = th.popList("listdump : seq");
	
	post("[\n");
	while (list()) {
		post("list %p %p %d\n", list(), list->mArray(), list->mArray() ? (int)list->mArray->size() : -1);
		list = list->next();
	}
	post("]\n");
}

static void help_(Thread& th, Prim* prim)
{
	V v = th.pop();

	const char* mask = v.GetAutoMapMask();
	const char* help = v.OneLineHelp();
	
	if (mask) {
		post("@%s ", mask);
	}
	if (help) {
		post("%s\n", help);
	} else {
		post("no help available.\n");
	}

}

static void helpbifs_(Thread& th, Prim* prim)
{
    post("\nBUILT IN FUNCTIONS\n\n");

	for (size_t i = 0; i < vm.bifHelp.size(); ++i) {
		std::string& s = vm.bifHelp[i];
		post(" %s\n", s.c_str());
	}
}

static void helpLine_(Thread& th, Prim* prim)
{
	P<String> str = th.popString("helpLine : string");
	vm.addUdfHelp(str->s);
}

static void helpudfs_(Thread& th, Prim* prim)
{
    post("\nUSER DEFINED FUNCTIONS\n\n");

	for (size_t i = 0; i < vm.udfHelp.size(); ++i) {
		std::string& s = vm.udfHelp[i];
		post(" %s\n", s.c_str());
	}
}

static void helpall_(Thread& th, Prim* prim)
{
    helpbifs_(th, prim);
    helpudfs_(th, prim);
}


static void prelude_(Thread& th, Prim* prim)
{
    static const size_t cmdMaxLen = 2048;
	char cmd[cmdMaxLen];
	
	if (vm.prelude_file) {
		snprintf(cmd, cmdMaxLen, "open %s\n", vm.prelude_file);
		system(cmd);
	} else {
		printf("no prelude file.\n");
	}
}

static void examples_(Thread& th, Prim* prim)
{
    static const size_t cmdMaxLen = 2048;
	char cmd[cmdMaxLen];
	
	const char* examples_file = getenv("SAPF_EXAMPLES");
	if (examples_file) {
		snprintf(cmd, cmdMaxLen, "open %s\n", examples_file);
		system(cmd);
	} else {
		printf("no examples file.\n");
	}
}

static void readme_(Thread& th, Prim* prim)
{
    static const size_t cmdMaxLen = 2048;
	char cmd[cmdMaxLen];
	
	const char* readme_file = getenv("SAPF_README");
	if (readme_file) {
		snprintf(cmd, cmdMaxLen, "open %s\n", readme_file);
		system(cmd);
	} else {
		printf("no readme file.\n");
	}
}

static void logfile_(Thread& th, Prim* prim)
{
    static const size_t cmdMaxLen = 2048;
	char cmd[cmdMaxLen];
	
	if (vm.log_file) {
		snprintf(cmd, cmdMaxLen, "open %s\n", vm.log_file);
		system(cmd);
	} else {
		printf("no log file.\n");
	}
}

static void trace_(Thread& th, Prim* prim)
{
	vm.traceon = th.pop().isTrue();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma mark PLUGS

struct PlugOut : Gen
{
	P<Plug> _plug;
	
	PlugOut(Thread& th, P<Plug>& inPlug) : Gen(th, itemTypeV, false), _plug(inPlug)
	{
	}
	virtual const char* TypeName() const override { return "PlugOut"; }
    	
	virtual void pull(Thread& th) override {
		VIn in;
		int changeCount;
		_plug->getPlug(in, changeCount);
		int framesToFill = mBlockSize;
		V* out = mOut->fulfill(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride;
			V *a;
			if (in(th, n, astride, a)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = *a;
					a += astride;
				}
				in.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
		_plug->setPlug(in, changeCount);
	}
};

struct ZPlugOut : Gen
{
	P<ZPlug> _plug;
	
	ZPlugOut(Thread& th, P<ZPlug>& inPlug) : Gen(th, itemTypeZ, false), _plug(inPlug)
	{
	}
	virtual const char* TypeName() const override { return "ZPlugOut"; }
    	
	virtual void pull(Thread& th) override {
		ZIn in;
		int changeCount;
		_plug->getPlug(in, changeCount);
		int framesToFill = mBlockSize;
		Z* out = mOut->fulfillz(framesToFill);
		while (framesToFill) {
			int n = framesToFill;
			int astride;
			Z *a;
			if (in(th, n, astride, a)) {
				setDone();
				break;
			} else {
				for (int i = 0; i < n; ++i) {
					out[i] = *a;
					a += astride;
				}
				in.advance(n);
				framesToFill -= n;
				out += n;
			}
		}
		produce(framesToFill);
		_plug->setPlug(in, changeCount);
	}
};


static void plug_(Thread& th, Prim* prim)
{
	V in = th.pop();
	P<Plug> plug = new Plug(in);
	th.push(new List(new PlugOut(th, plug)));
	th.push(plug);
}

static void zplug_(Thread& th, Prim* prim)
{
	V value = th.pop();
	if (value.isVList() && value.isFinite()) {
		P<List> valueList = ((List*)value.o())->pack(th);
		P<Array> valueArray = valueList->mArray;
		V* vals = valueArray->v();
		size_t n = valueArray->size();
		
		P<List> plugList = new List(itemTypeV, n);
		P<List> outList = new List(itemTypeV, n);
		
		P<Array> plugArray = plugList->mArray;
		P<Array> outArray = outList->mArray;
		
		plugArray->setSize(n);
		outArray->setSize(n);
		
		V* plugItems = plugArray->v();
		V* outItems = outArray->v();
		
		for (size_t i = 0; i < n; ++i) {
			SaveStack ss(th);
			th.push(vals[i]);
			zplug_(th, prim);
			plugItems[i] = th.pop();
			outItems[i] = th.pop();
		}
		
		th.push(outList);
		th.push(plugList);
	} else if (value.isZIn()) {
		P<ZPlug> plug = new ZPlug(value);
		th.push(new List(new ZPlugOut(th, plug)));
		th.push(plug);
	} else {
		wrongType("zplug : ref", "VList or UGen input", value);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef _WIN32
#pragma mark GLOB

#include <glob.h>

static void glob_(Thread& th, Prim* prim)
{
	P<String> pat = th.popString("glob : pattern");
	
	glob_t g = {};
	glob(pat->s, GLOB_MARK, nullptr, &g);

        size_t count;
#ifdef GLOB_NOMATCH
        // TODO is this correct?
        count = (size_t) g.gl_pathc;
#else
        count = (size_t) g.gl_matchc;
#endif
        
	P<Array> a = new Array(itemTypeV, count);
	for (int i = 0; i < count; ++i) {
		a->add(new String(g.gl_pathv[i]));
	}
	globfree(&g);
	
	th.push(new List(a));
}
#else
#include <windows.h>
#include <vector>
#include <iostream>

// TODO: not sure if this works as good a the non-windows one...
static void glob_(Thread& th, Prim* prim) {
	P<String> pat = th.popString("glob : pattern");

	std::vector<std::string> results;
	WIN32_FIND_DATA findFileData;
	HANDLE hFind = FindFirstFile(pat.get()->cstr(), &findFileData);

	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			results.push_back(findFileData.cFileName);
		} while (FindNextFile(hFind, &findFileData) != 0);
		FindClose(hFind);
	}

	P<Array> a = new Array(itemTypeV, results.size());
	for (int i = 0; i < results.size(); ++i) {
		a->add(new String(results[i].c_str()));
	}
	th.push(new List(a));
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma mark ADD CORE OPS


#define DEFN(NAME, N, FUN, HELP) 	vm.def(NAME, N, 1, FUN, HELP);
#define DEF(NAME, N, HELP) 	vm.def(#NAME, N, 1, NAME##_, HELP);
#define DEF2(NAME, TAKES, LEAVES, HELP) 	vm.def(#NAME, TAKES, LEAVES, NAME##_, HELP);
#define DEFnoeach(NAME, TAKES, LEAVES, HELP) 	vm.def(#NAME, TAKES, LEAVES, NAME##_, HELP, V(0.), true);
#define DEFAM(NAME, MASK, HELP) 	vm.defautomap(#NAME, #MASK, NAME##_, HELP);

void AddCoreOps();
void AddCoreOps()
{
	// stack ops
	vm.addBifHelp("\n*** stack ops ***");
	DEFnoeach(clear, 0, 0, "(... -->) clears everything off the stack.");
	DEFnoeach(cleard, 0, 1, "(... a --> a) clears all but the top item from the stack.")
	DEFnoeach(stackDepth, 0, 1, "(--> n) returns the size of the stack.")
	
	DEFnoeach(aa, 1, 2, "(a --> a a) push the top item on stack again.")
	DEFnoeach(aaa, 1, 3, "(a --> a a a) push the top item on stack two more times.")
	DEFnoeach(ba, 2, 2, "(a b --> b a) swap top two items.")
	
	DEFnoeach(bac, 3, 3, "(a b c --> b a c) reorder items on stack.")
	DEFnoeach(cba, 3, 3, "(a b c --> c b a) reorder items on stack.")
	DEFnoeach(bca, 3, 3, "(a b c --> b c a) reorder items on stack.")
	DEFnoeach(cab, 3, 3, "(a b c --> c a b) reorder items on stack.")

	DEFnoeach(bab, 2, 3, "(a b --> b a b) reorder items on stack.")
	DEFnoeach(aba, 2, 3, "(a b --> a b a) reorder items on stack.")

	DEFnoeach(aab, 2, 3, "(a b --> a a b) reorder items on stack.")
	DEFnoeach(aabb, 2, 4, "(a b --> a a b b) reorder items on stack.")
	DEFnoeach(abab, 2, 4, "(a b --> a b a b) reorder items on stack.")

	DEFnoeach(nip, 2, 1, "(a b --> b) remove second item on stack.")
	DEFnoeach(pop, 1, 0, "(a -->) remove top item on stack.")
	
	// loops
	vm.addBifHelp("\n*** loops ***");
	//DEFnoeach(while, 2, "(A B --> ..) While applying A returns true, apply B.")
	DEFnoeach(do, 2, 0, "(list \\item[..] -->) applies the function to each item of a finite list. Useful for side effects like printing or file writing.")

	// conditional ops
	vm.addBifHelp("\n*** conditional ops ***");
	DEF(equals, 2, "(a b --> bool) returns 1 if a and b are structurally equivalent. If the data structures are cyclic then this may never terminate.")
	DEF(less, 2, "(a b --> bool) returns 1 if a is less than b structurally. If the data structures are cyclic then this may never terminate.")
	DEF(greater, 2, "(a b --> bool) returns 1 if a is greater than b structurally. If the data structures are cyclic then this may never terminate.")
	DEF2(if, 3, -1, "(A B C --> ..) if A is true then apply B else apply C.")

	DEF(not, 1, "(A --> bool) returns 0 if A is true and 1 if A is false.")
	//DEF2(dip, 1, -1, "(x A --> ..) pops x from stack, applies A, pushes x back on stack.")

	DEFnoeach(try, 2, -1, "(A B --> ..) apply function A. if an exception is thrown, function B is applied.")
	DEFnoeach(throw, 0, 0, "(a -->) throw an exception.")
	DEFnoeach(protect, 2, -1, "(A B --> ..) apply function A. if an exception is thrown, function B is applied and the exception is rethrown. Otherwise function B is applied and control continues as normal.")

	// form ops
	vm.addBifHelp("\n*** form ops ***");
	DEFAM(has, kk, "(form key --> bool) return whether a form contains the key.")
    
	DEFAM(keys, k, "(form --> keys) return an array of the keys of the form.")
	DEFAM(values, k, "(form --> values) return an array of the values of the form.")
	DEFAM(kv, k, "(form --> keys values) return two arrays of the keys and values of the form.") /// !!!! returns two values. can't be auto mapped.
	DEFAM(local, k, "(form --> local) return the head of the prototype inheritance list.")
	DEFAM(parent, k, "(form --> parent) return the tail of the prototype inheritance list.")
	DEFAM(dot, ka, "(form key --> item) return the value for the key.")
    
    DEFnoeach(pushWorkspace, 0, 0, "(-->) pushes a new outer scope onto the workspace. New bindings will be made in the new outer scope.");
    DEFnoeach(popWorkspace, 0, 0, "(-->) pops a scope from the workspace. All bindings in the outer scope will be forgotten.");
	
	vm.addBifHelp("\n*** ref ops ***");
	DEFAM(get, k, "(r --> a) return the value store in a ref.")
	DEFnoeach(set, 1, 0, "(a r -->) store the value a in the ref r.")
	vm.def("R", 1, 1, ref_, "(a --> r) create a new Ref with the inital value a");
	vm.def("ZR", 1, 1, zref_, "(z --> r) create a new ZRef with the inital value z. A ZRefs is a mutable reference to a real number.");
	vm.def("P", 1, 2, plug_, "(a --> out in) create a new stream plug pair with the inital value a");
	vm.def("ZP", 1, 2, zplug_, "(a --> out in) create a new signal plug pair with the inital value a.");
	
	
	//DEF(bind, 2, "deprecated")
	
	// apply ops
	vm.addBifHelp("\n*** function ops ***");
	DEF(Y, 1, "(funA --> funB) Y combinator. funB calls funA with the last argument being funB itself. Currently the only way to do recursion. \n\t\te.g. \\x f [x 2 < \\[1] \\[ x x -- f *] if] Y = factorial    7 factorial --> 5040")
	DEF(noeach, 1, "(fun --> fun) sets a flag in the function so that it will pass through arguments with @ operators without mapping them.")
	vm.def("!", 1, -1, apply_, "(... f --> ...) apply the function to its arguments, observing @ arguments as appropriate.");
	vm.def("!e", 2, -1, applyEvent_, "(form fun --> ...) for each argument in the function, find the same named fields in the form and push those values as arguments to the function.");
	DEF(compile, 1, "(string --> fun) compile the string and return a function.")
	
	vm.addBifHelp("\n*** printing ops ***");
	DEFnoeach(printLength, 0, 1, "(--> length) return the number of items printed for lists.");
	DEFnoeach(printDepth, 0, 1, "(--> depth) return the number of levels of nesting printed for lists.");
	DEFnoeach(setPrintLength, 1, 0, "(length --> ) set the number of items printed for lists.");
	DEFnoeach(setPrintDepth, 1, 0, "(depth -->) set the number of levels of nesting printed for lists.");
	
	DEFnoeach(pr, 1, 0, "(A -->) print the top item on the stack. (no space or carriage return is printed)")
	DEFnoeach(prdebug, 1, 0, "(A -->) print debug version of the top item on the stack. (no space or carriage return is printed)")
	DEFnoeach(cr, 0, 0, "(-->) print a carriage return.")
	DEFnoeach(sp, 0, 0, "(-->) print a space character.")
	DEFnoeach(tab, 0, 0, "(-->) print a tab.")
	DEFnoeach(prstk, 0, 0, "(-->) print the stack.")

#if COLLECT_MINFO
	DEFnoeach(minfo, 0, 0, "(-->) print memory management info.")
#endif
	DEFnoeach(listdump, 1, 0, "(list -->) prints information about a list.");

	vm.addBifHelp("\n*** string ops ***");
	DEF(str, 1, "(x --> string) convert x to a string.");
	DEF(debugstr, 1, "(x --> string) convert x to a debug string.");
	DEFAM(strcat, ak, "(list separator --> string) convert elements of list to a string with separator string between each.");
	DEF(strlines, 1, "(list --> string) convert elements of list to a newline separated string.");
	DEFAM(glob, k, "(pattern --> paths) return a list of file path names that match.");

	vm.addBifHelp("\n*** sample rate ops ***");
	DEFnoeach(sr, 0, 1, "(--> sampleRate) returns the sample rate. samples per second. ")
	DEFnoeach(nyq, 0, 1, "(--> sampleRate/2) returns the nyquist rate")
	DEFnoeach(isr, 0, 1, "(--> 1/sampleRate) returns the inverse sample rate")
	DEFnoeach(inyq, 0, 1, "(--> 2/sampleRate) returns the inverse nyquist rate.")
	DEFnoeach(rps, 0, 1, "(--> 2pi/sampleRate) returns the radians per sample")


	vm.addBifHelp("\n*** help ops ***");

	DEFnoeach(help, 1, 0, "(fun -->) prints help for a function.");
	DEFnoeach(helpbifs, 0, 0, "(-->) prints help for all built in functions.");
	DEFnoeach(helpudfs, 0, 0, "(-->) prints help for all user defined functions.");
	DEFnoeach(helpall, 0, 0, "(-->) prints help for all built in and user defined functions.");
    DEF(helpLine, 1, "(string -->) add a line to the user defined function help.");
	
	vm.addBifHelp("\n*** thread ops ***");
    DEFnoeach(go, 1, 0, "(fun -->) launches the function in a new thread.");
    DEFnoeach(sleep, 1, 0, "(seconds -->) sleeps the current thread for the time given.");

	vm.addBifHelp("\n*** misc ***");
	DEF(type, 1, "(a --> symbol) return a symbol naming the type of the value a.")
	DEFnoeach(trace, 1, 0, "(bool -->) turn tracing on/off in the interpreter.")

	vm.addBifHelp("\n*** text files ***");
	DEFnoeach(load, 1, 0, "(filename -->) compiles and executes a text file.")	
	DEFnoeach(prelude, 0, 0, "(-->) opens the prelude file in the default text editor.")
	DEFnoeach(examples, 0, 0, "(-->) opens the examples file in the default text editor.")
	DEFnoeach(logfile, 0, 0, "(-->) opens the log file in the default text editor.")
	DEFnoeach(readme, 0, 0, "(-->) opens the README file in the default text editor.")

}

