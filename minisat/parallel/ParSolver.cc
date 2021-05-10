/************************************************************************************[ParSolver.cc]
MergeSat -- Copyright (c) 2021,      Norbert Manthey

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**************************************************************************************************/

#include "parallel/ParSolver.h"
#include "mtl/Sort.h"
#include "utils/Options.h"
#include "utils/System.h"

using namespace MERGESAT_NSPACE;

//=================================================================================================
// Options:


static const char *_cat = "PAR";

static IntOption opt_cores(_cat, "cores", "Number of solvers to use, 0 means each CPU, -1 every 2nd CPU", 0, IntRange(-1, INT32_MAX));

//=================================================================================================
// Constructor/Destructor:

ParSolver::ParSolver()
  : par_reparsed_options(updateOptions()), parsing(false), verbosity(0), cores(opt_cores), initialized(false), primary_modified(false)
{
    // Get number of cores, and allocate arrays
    init_solvers();
}

ParSolver::~ParSolver() { tear_down_solvers(); }

int ParSolver::nVars() const
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    return solvers[0]->nVars();
}

int ParSolver::nClauses() const
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    return solvers[0]->nClauses();
}

Var ParSolver::newVar(bool polarity, bool dvar)
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    primary_modified = true;
    return solvers[0]->newVar(polarity, dvar);
}

void ParSolver::reserveVars(Var vars)
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    solvers[0]->reserveVars(vars);
}

bool ParSolver::addClause_(vec<Lit> &ps)
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    primary_modified = true;
    return solvers[0]->addClause_(ps);
}

void ParSolver::addInputClause_(vec<Lit> &ps)
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    primary_modified = true;
    solvers[0]->addInputClause_(ps);
}

// Variable mode:
//
void ParSolver::setFrozen(Var v, bool b)
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    primary_modified = true;
    solvers[0]->setFrozen(v, b);
}

bool ParSolver::isEliminated(Var v) const
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    return solvers[0]->isEliminated(v);
}

bool ParSolver::eliminate(bool turn_off_elim)
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    primary_modified = true;
    return solvers[0]->eliminate(turn_off_elim);
}

// Solving:
//
bool ParSolver::solve(const vec<Lit> &assumps, bool do_simp, bool turn_off_simp)
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    return solvers[0]->solve(assumps, do_simp, turn_off_simp);
}

lbool ParSolver::solveLimited(const vec<Lit> &assumps, bool do_simp, bool turn_off_simp)
{
    assert(solvers[0] != nullptr && "there has to be one working solver");

    assert((!primary_modified || solvers.size() == 1) && "sync solvers before solving");

    lbool ret = l_Undef;
    model.swap(solvers[0]->model);
    conflict.swap(solvers[0]->conflict);
    assert(solvers.size() == 1 && "actually implement parallel case");
    ret = solvers[0]->solveLimited(assumps, do_simp, turn_off_simp);
    conflict.swap(solvers[0]->conflict);
    model.swap(solvers[0]->model);
    return ret;
}

void ParSolver::interrupt()
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    for (int i = 0; i < solvers.size(); ++i) solvers[i]->interrupt();
}

bool ParSolver::okay() const
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    for (int i = 0; i < solvers.size(); ++i) {
        if (!solvers[i]->okay()) return false;
    }
    return true;
}

int ParSolver::max_simp_cls()
{
    assert(solvers[0] != nullptr && "there has to be one working solver");
    return solvers[0]->max_simp_cls();
}


void ParSolver::init_solvers()
{
    assert(solvers.size() == 0 && "do not allocate solvers multiple times");

    // auto detect cores
    if (cores <= 0) {
        if (cores == 0)
            cores = nrCores();
        else
            cores = (nrCores() + 1) / 2;
    }

    /* make sure we have at least one solver */
    cores = cores <= 1 ? 1 : cores;

    solvers.growTo(cores, nullptr);
    for (int i = 0; i < solvers.size(); ++i) {
        solvers[i] = new SimpSolver();
        solvers[i]->diversify(i, 32);
    }

    assert(solvers[0] != nullptr && "there has to be one working solver");
    initialized = true;
}

void ParSolver::tear_down_solvers()
{
    if (solvers.size() > 0) {
        for (int i = 0; i < solvers.size(); ++i) {
            if (solvers[i] != nullptr) delete solvers[i];
            solvers[i] = nullptr;
        }
    }
    solvers.clear();
    initialized = false;
}
