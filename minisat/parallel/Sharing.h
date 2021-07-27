/*************************************************************************************[Sharing.h]
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

#ifndef MergeSat_Sharing_h
#define MergeSat_Sharing_h

#include "core/SolverTypes.h"
#include "mtl/Vec.h"

#include <vector>

namespace MERGESAT_NSPACE
{
//=================================================================================================

/** Object that memorizes clauses, re-using known object types. */
class ClausePool
{
    vec<CRef> clauses;
    AccessCounter counter;
    ClauseAllocator ca;

    public:
    ClausePool() : ca(counter) {}

    int size() const { return clauses.size(); }

    void reset()
    {
        clauses.clear();
        ca.clear();
    }

    void add_shared_clause(const std::vector<int> &c, int glueValue)
    {

        CRef cr = ca.alloc_placeholder(c.size(), true);
        clauses.push(cr);
        Clause &d = ca[cr];
        d.set_lbd(glueValue);

        // actually copy literals over
        for (int i = 0; i < d.size(); i++) d[i] = fromFormal(c[i]);
    }

    const Clause &getClause(int index) const { return ca[clauses[index]]; }
};


//=================================================================================================
} // namespace MERGESAT_NSPACE

#endif
